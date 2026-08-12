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
 * @file ClockTraceOwnership.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Clock declaration and net ownership resolution for SDC import.
 */

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "IdbNet.h"
#include "SDCClockReader.hh"
#include "SDCClockTraceAlgorithm.hh"

namespace icts::clock_trace {

auto BuildClockDeclViews(idb::IdbDesign* idb_design, const SdcClockData& clock_data) -> std::map<std::string, ClockDeclView>
{
  std::map<std::string, ClockDeclView> clock_view_by_name;
  for (const auto& clock : clock_data.clocks) {
    auto& view = clock_view_by_name[clock.clock_name];
    view.clock_kind = ClockKindName(clock);
    view.master_clock_name = MasterClockName(clock);
    for (const auto& target : clock.targets) {
      for (auto* net : ResolveRefNets(idb_design, target)) {
        if (net != nullptr) {
          view.sdc_target_net_names.insert(net->get_net_name());
        }
      }
    }
  }
  return clock_view_by_name;
}

auto AnnotateRecordOwnership(ClockTraceRecord& record, const std::map<std::string, ClockDeclView>& clock_view_by_name) -> void
{
  const auto view_iter = clock_view_by_name.find(record.clock_name);
  if (view_iter == clock_view_by_name.end()) {
    return;
  }
  record.clock_kind = view_iter->second.clock_kind;
  record.master_clock_name = view_iter->second.master_clock_name;
  record.dominance = DominanceForRecord(record, record.clock_kind);
}

auto CollectTracedNetNames(const std::vector<ClockTraceRecord>& records) -> std::set<std::string>
{
  std::set<std::string> traced_net_names;
  for (const auto& record : records) {
    if (!record.net_name.empty() && record.status != "rejected" && record.status != "skipped") {
      traced_net_names.insert(record.net_name);
    }
  }
  return traced_net_names;
}

auto CollectUnownedClockLikeRecords(const SdcLibertyCellLookup& liberty_cell_lookup, idb::IdbDesign* idb_design, const std::vector<ClockTraceRecord>& records)
    -> std::vector<ClockTraceRecord>
{
  std::vector<ClockTraceRecord> unowned_records;
  auto* net_list = idb_design == nullptr ? nullptr : idb_design->get_net_list();
  if (net_list == nullptr) {
    return unowned_records;
  }

  const auto traced_net_names = CollectTracedNetNames(records);
  for (auto* net : net_list->get_net_list()) {
    if (net == nullptr || traced_net_names.contains(net->get_net_name())) {
      continue;
    }
    const auto stats = CountDirectClockSinksForOwnership(liberty_cell_lookup, net);
    if (!IsClockTarget(stats)) {
      continue;
    }
    ClockTraceRecord record;
    record.clock_name = "unowned";
    record.net_name = net->get_net_name();
    record.status = "warning";
    record.target_kind = TargetKind(stats);
    record.sequential_clock_sinks = stats.sequential_clock_sinks;
    record.macro_clock_sinks = stats.macro_clock_sinks;
    record.reason = "no_sdc_clock_ownership";
    record.clock_kind = "unowned";
    record.master_clock_name = "n/a";
    record.dominance = "unowned_clock_like_net";
    unowned_records.push_back(std::move(record));
  }

  std::ranges::sort(unowned_records, [](const auto& lhs, const auto& rhs) -> bool { return lhs.net_name < rhs.net_name; });
  return unowned_records;
}

}  // namespace icts::clock_trace
