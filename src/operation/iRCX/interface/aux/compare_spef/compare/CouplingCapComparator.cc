// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file CouplingCapComparator.cc
 * @brief compare_spef implementation detail.
 */
#include "compare/CouplingCapComparator.hh"

#include <omp.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ParallelUtils.hh"
#include "utils/CompareMath.hh"

namespace ircx {
namespace compare_spef {
namespace {

struct NetMeta
{
  const Net* net = nullptr;
  Size order = 0;
  bool external = false;
  bool selected = false;
};

class NetMetaIndex
{
 public:
  NetMetaIndex(const Data& data, const NetSelector& selector)
  {
    _meta.reserve(data.nets.size());
    for (const Net& net : data.nets) {
      _meta.emplace(
          net.name,
          NetMeta{
              .net = &net,
              .order = data.index.orderOf(net.name),
              .external = std::any_of(
                  net.pins.begin(),
                  net.pins.end(),
                  [](const Pin& pin) { return pin.is_external; }),
              .selected = selector.selected(net)});
    }
  }

  auto find(const std::string& net_name) const -> const NetMeta*
  {
    const auto meta_it = _meta.find(net_name);
    return meta_it == _meta.end() ? nullptr : &meta_it->second;
  }

  auto isExternal(const std::string& net_name) const -> bool
  {
    const NetMeta* net_meta = find(net_name);
    return net_meta != nullptr && net_meta->external;
  }

  auto orderOf(const std::string& net_name) const -> Size
  {
    const NetMeta* net_meta = find(net_name);
    return net_meta == nullptr ? 0 : net_meta->order;
  }

 private:
  std::unordered_map<std::string, NetMeta> _meta;
};

auto makeCcapMismatch(const NetMetaIndex& meta,
                      const NodePair& key,
                      F64 capacitance) -> CcapMismatch
{
  CcapMismatch mismatch;
  mismatch.nets = key;
  mismatch.report_nets = key;
  mismatch.first_order = meta.orderOf(mismatch.report_nets.first);
  mismatch.second_order = meta.orderOf(mismatch.report_nets.second);
  mismatch.first_external = meta.isExternal(mismatch.report_nets.first);
  mismatch.second_external = meta.isExternal(mismatch.report_nets.second);
  mismatch.capacitance = capacitance;
  return mismatch;
}

void appendRows(Result& result,
                Result&& thread_result)
{
  result.ccap_rows.insert(
      result.ccap_rows.end(),
      std::make_move_iterator(thread_result.ccap_rows.begin()),
      std::make_move_iterator(thread_result.ccap_rows.end()));
  result.reference_only_couplings.insert(
      result.reference_only_couplings.end(),
      std::make_move_iterator(thread_result.reference_only_couplings.begin()),
      std::make_move_iterator(thread_result.reference_only_couplings.end()));
  result.test_only_couplings.insert(
      result.test_only_couplings.end(),
      std::make_move_iterator(thread_result.test_only_couplings.begin()),
      std::make_move_iterator(thread_result.test_only_couplings.end()));
}

void reserveRows(Result& result,
                 const std::vector<Result>& partial_results)
{
  Size ccap_count = result.ccap_rows.size();
  Size reference_only_count = result.reference_only_couplings.size();
  Size test_only_count = result.test_only_couplings.size();
  for (const auto& partial_result : partial_results) {
    ccap_count += partial_result.ccap_rows.size();
    reference_only_count += partial_result.reference_only_couplings.size();
    test_only_count += partial_result.test_only_couplings.size();
  }
  result.ccap_rows.reserve(ccap_count);
  result.reference_only_couplings.reserve(reference_only_count);
  result.test_only_couplings.reserve(test_only_count);
}

void addRow(const Config& config,
            const NetMetaIndex& reference_meta,
            const std::string& victim,
            const std::string& aggressor,
            F64 reference_cap,
            F64 test_cap,
            Result& result)
{
  const NetMeta* victim_meta = reference_meta.find(victim);
  if (victim_meta == nullptr || victim_meta->net == nullptr || !victim_meta->selected) {
    return;
  }

  const F64 cc_abs = std::abs(reference_cap);
  const F64 lumpC_abs = std::abs(victim_meta->net->total_cap);
  const F64 cc_rel = lumpC_abs <= math::kEpsilon ? 0.0 : cc_abs / lumpC_abs;
  if (cc_abs < config.ccap_abs_threshold || cc_rel < config.ccap_rel_threshold) {
    return;
  }

  CcapRow row;
  row.victim = victim;
  row.aggressor = aggressor;
  row.reference = reference_cap;
  row.test = test_cap;
  row.delta = row.test - row.reference;
  row.reference_victim_total_cap = lumpC_abs;
  row.relative_delta = math::couplingRelativeDelta(row.test, row.reference, row.reference);
  result.ccap_rows.push_back(std::move(row));
}

}  // namespace

CouplingCapComparator::CouplingCapComparator(const Config& config)
    : _config(config), _net_selector(config)
{
}

void CouplingCapComparator::compare(const Data& test,
                                    const Data& reference,
                                    Result& result) const
{
  const NetMetaIndex reference_meta(reference, _net_selector);
  const NetMetaIndex test_meta(test, _net_selector);

  if (!reference.coupling_caps.empty()) {
    const int reference_thread_count = parallel::threadCount(
        reference.coupling_caps.entries.size(),
        _config.cores);
    std::vector<Result> reference_thread_results(reference_thread_count);
#pragma omp parallel num_threads(reference_thread_count)
    {
      Result local_result;
#pragma omp for schedule(dynamic, 256) nowait
      for (I64 entry_index = 0;
           entry_index < static_cast<I64>(reference.coupling_caps.entries.size());
           ++entry_index) {
        const auto& entry = reference.coupling_caps.entries[entry_index];
        const auto* test_cap = test.coupling_caps.find(entry.nets);
        if (test_cap == nullptr) {
          local_result.reference_only_couplings.push_back(
              makeCcapMismatch(reference_meta, entry.nets, entry.capacitance));
          continue;
        }

        addRow(
            _config,
            reference_meta,
            entry.nets.first,
            entry.nets.second,
            entry.capacitance,
            test_cap->capacitance,
            local_result);
      }
      reference_thread_results[omp_get_thread_num()] = std::move(local_result);
    }

    reserveRows(result, reference_thread_results);
    for (auto& thread_result : reference_thread_results) {
      appendRows(result, std::move(thread_result));
    }
  }

  if (!test.coupling_caps.empty()) {
    const int test_thread_count = parallel::threadCount(
        test.coupling_caps.entries.size(),
        _config.cores);
    std::vector<Result> test_thread_results(test_thread_count);
#pragma omp parallel num_threads(test_thread_count)
    {
      Result local_result;
#pragma omp for schedule(dynamic, 256) nowait
      for (I64 entry_index = 0;
           entry_index < static_cast<I64>(test.coupling_caps.entries.size());
           ++entry_index) {
        const auto& entry = test.coupling_caps.entries[entry_index];
        if (!reference.coupling_caps.contains(entry.nets)) {
          local_result.test_only_couplings.push_back(
              makeCcapMismatch(test_meta, entry.nets, entry.capacitance));
        }
      }
      test_thread_results[omp_get_thread_num()] = std::move(local_result);
    }

    reserveRows(result, test_thread_results);
    for (auto& thread_result : test_thread_results) {
      appendRows(result, std::move(thread_result));
    }
  }
}

}  // namespace compare_spef
}  // namespace ircx
