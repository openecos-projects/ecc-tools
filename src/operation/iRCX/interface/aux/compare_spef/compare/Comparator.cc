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
 * @file Comparator.cc
 * @brief compare_spef implementation detail.
 */
#include "compare/Comparator.hh"

#include "ParallelUtils.hh"
#include "RCXHeader.hpp"
#include "utils/CompareMath.hh"
#include "utils/CompareMode.hh"

namespace ircx {
namespace compare_spef {
namespace {

struct NetCompareJob
{
  std::string net_name;
  const Net* reference_net = nullptr;
  const Net* test_net = nullptr;
};

void appendRows(Result& result, Result&& thread_result)
{
  result.tcap_rows.insert(result.tcap_rows.end(), std::make_move_iterator(thread_result.tcap_rows.begin()),
                          std::make_move_iterator(thread_result.tcap_rows.end()));
  result.gcap_rows.insert(result.gcap_rows.end(), std::make_move_iterator(thread_result.gcap_rows.begin()),
                          std::make_move_iterator(thread_result.gcap_rows.end()));
  result.p2p_rows.insert(result.p2p_rows.end(), std::make_move_iterator(thread_result.p2p_rows.begin()), std::make_move_iterator(thread_result.p2p_rows.end()));
  result.reference_only_nets.insert(result.reference_only_nets.end(), std::make_move_iterator(thread_result.reference_only_nets.begin()),
                                    std::make_move_iterator(thread_result.reference_only_nets.end()));
}

void reserveRows(Result& result, const std::vector<Result>& partial_results)
{
  Size tcap_count = result.tcap_rows.size();
  Size gcap_count = result.gcap_rows.size();
  Size p2p_count = result.p2p_rows.size();
  Size reference_only_net_count = result.reference_only_nets.size();
  for (const auto& partial_result : partial_results) {
    tcap_count += partial_result.tcap_rows.size();
    gcap_count += partial_result.gcap_rows.size();
    p2p_count += partial_result.p2p_rows.size();
    reference_only_net_count += partial_result.reference_only_nets.size();
  }
  result.tcap_rows.reserve(tcap_count);
  result.gcap_rows.reserve(gcap_count);
  result.p2p_rows.reserve(p2p_count);
  result.reference_only_nets.reserve(reference_only_net_count);
}

auto hasDisconnectedPinComponents(const Net& net) -> bool
{
  std::unordered_map<std::string, Size> node_to_index;
  node_to_index.reserve(net.resistors.size() * 2);
  auto get_node_index = [&](const std::string& node) -> Size {
    const auto [it, inserted] = node_to_index.emplace(node, node_to_index.size());
    return it->second;
  };

  std::vector<std::pair<Size, Size>> edges;
  edges.reserve(net.resistors.size());
  for (const auto& resistor : net.resistors) {
    if (resistor.resistance <= math::kEpsilon || resistor.node1.empty() || resistor.node2.empty()) {
      continue;
    }
    edges.emplace_back(get_node_index(resistor.node1), get_node_index(resistor.node2));
  }

  std::vector<Size> pin_indices;
  pin_indices.reserve(net.pins.size());
  for (const auto& pin : net.pins) {
    if (pin.name.empty() || pin.direction == "N") {
      continue;
    }
    const auto node_it = node_to_index.find(pin.name);
    if (node_it == node_to_index.end()) {
      return true;
    }
    pin_indices.push_back(node_it->second);
  }
  if (pin_indices.size() <= 1) {
    return false;
  }

  std::vector<std::vector<Size>> adjacency(node_to_index.size());
  for (const auto& [node1, node2] : edges) {
    adjacency[node1].push_back(node2);
    adjacency[node2].push_back(node1);
  }

  std::vector<int> component(node_to_index.size(), -1);
  int next_component = 0;
  std::vector<Size> stack;
  for (Size index = 0; index < component.size(); ++index) {
    if (component[index] >= 0) {
      continue;
    }
    component[index] = next_component;
    stack.push_back(index);
    while (!stack.empty()) {
      const Size node = stack.back();
      stack.pop_back();
      for (Size adjacent : adjacency[node]) {
        if (component[adjacent] < 0) {
          component[adjacent] = next_component;
          stack.push_back(adjacent);
        }
      }
    }
    next_component++;
  }

  const int first_component = component[pin_indices.front()];
  return std::any_of(pin_indices.begin() + 1, pin_indices.end(), [&](Size index) { return component[index] != first_component; });
}

// Use reference SPEF name-map order for default report orientation. Explicit
// user-configured from/to directions are preserved.
class ReferenceNameMapOrder
{
 public:
  explicit ReferenceNameMapOrder(const Net& reference_net)
  {
    _pin_by_name.reserve(reference_net.pins.size());
    for (const auto& pin : reference_net.pins) {
      _pin_by_name.try_emplace(pin.name, &pin);
    }
  }

  auto reportPair(const NodePair& pair) const -> NodePair
  {
    const auto first_it = _pin_by_name.find(pair.first);
    const auto second_it = _pin_by_name.find(pair.second);
    if (first_it == _pin_by_name.end() || second_it == _pin_by_name.end()) {
      return pair;
    }

    const Pin& first_pin = *first_it->second;
    const Pin& second_pin = *second_it->second;
    if (!first_pin.has_name_map_index || !second_pin.has_name_map_index || first_pin.name_map_index == second_pin.name_map_index) {
      return pair;
    }

    return first_pin.name_map_index < second_pin.name_map_index ? pair : NodePair{pair.second, pair.first};
  }

 private:
  std::unordered_map<std::string, const Pin*> _pin_by_name;
};

auto makeNetCompareJobs(const Data& test, const Data& reference) -> std::vector<NetCompareJob>
{
  std::vector<NetCompareJob> jobs;
  jobs.reserve(reference.nets.size());
  for (const Net& reference_net : reference.nets) {
    jobs.push_back(NetCompareJob{
        .net_name = reference_net.name,
        .reference_net = &reference_net,
        .test_net = test.findNet(reference_net.name),
    });
  }
  return jobs;
}

class MatchedNetComparator
{
 public:
  MatchedNetComparator(const Config& config, const NetSelector& net_selector, const PathPairGenerator& path_pair_generator,
                       const ResistanceSolver& resistance_solver, bool compare_capacitance, bool compare_resistance)
      : _config(config),
        _net_selector(net_selector),
        _path_pair_generator(path_pair_generator),
        _resistance_solver(resistance_solver),
        _compare_capacitance(compare_capacitance),
        _compare_resistance(compare_resistance)
  {
  }

  auto compare(const std::vector<NetCompareJob>& jobs) const -> Result
  {
    Result result;
    std::vector<Result> job_results(jobs.size());
    const int thread_count = parallel::threadCount(jobs.size(), _config.cores);
#pragma omp parallel for schedule(dynamic, 64) num_threads(thread_count)
    for (I64 index = 0; index < static_cast<I64>(jobs.size()); ++index) {
      const NetCompareJob& job = jobs[index];
      Result& job_result = job_results[index];
      if (job.test_net == nullptr) {
        job_result.reference_only_nets.push_back(job.net_name);
        continue;
      }
      compareMatchedNet(job.net_name, *job.reference_net, *job.test_net, job_result);
    }

    reserveRows(result, job_results);
    for (auto& job_result : job_results) {
      result.summary.matched_net_count += job_result.summary.matched_net_count;
      appendRows(result, std::move(job_result));
    }
    return result;
  }

 private:
  void compareMatchedNet(const std::string& net_name, const Net& reference_net, const Net& test_net, Result& result) const
  {
    result.summary.matched_net_count++;
    if (!_net_selector.selected(reference_net)) {
      return;
    }

    if (_compare_capacitance) {
      if (reference_net.total_cap >= _config.tcap_threshold) {
        addTotalCapRow(net_name, reference_net, test_net, result);
      }
      addGroundCapRow(net_name, reference_net, test_net, result);
    }

    if (_compare_resistance) {
      addResistanceRows(net_name, reference_net, test_net, result);
    }
  }

  void addTotalCapRow(const std::string& net_name, const Net& reference_net, const Net& test_net, Result& result) const
  {
    ValueRow row;
    row.net = net_name;
    row.reference = reference_net.total_cap;
    row.test = test_net.total_cap;
    row.delta = row.test - row.reference;
    row.relative_delta = math::capacitanceRelativeDelta(row.test, row.reference);
    result.tcap_rows.push_back(std::move(row));
  }

  void addGroundCapRow(const std::string& net_name, const Net& reference_net, const Net& test_net, Result& result) const
  {
    const auto sum_caps = [](const NodeGroundCapMap& caps) {
      return std::accumulate(caps.begin(), caps.end(), 0.0, [](F64 total, const auto& entry) { return total + entry.second; });
    };
    const F64 reference_cap = sum_caps(reference_net.node_ground_caps);
    const F64 test_cap = sum_caps(test_net.node_ground_caps);
    if (std::abs(reference_cap) < _config.ccap_abs_threshold && std::abs(test_cap) < _config.ccap_abs_threshold) {
      return;
    }

    GcapRow row;
    row.net = net_name;
    row.reference = reference_cap;
    row.test = test_cap;
    row.delta = row.test - row.reference;
    row.relative_delta = math::capacitanceRelativeDelta(row.test, row.reference);
    result.gcap_rows.push_back(std::move(row));
  }

  void addResistanceRows(const std::string& net_name, const Net& reference_net, const Net& test_net, Result& result) const
  {
    if (hasDisconnectedPinComponents(test_net)) {
      return;
    }

    const auto pairs = _path_pair_generator.generate(reference_net);
    const auto reference_resistances = _resistance_solver.equivalentResistances(reference_net, pairs);
    std::vector<Size> compared_indices;
    compared_indices.reserve(reference_resistances.size());
    for (Size index = 0; index < reference_resistances.size(); ++index) {
      const auto& reference_res = reference_resistances[index];
      if (reference_res.has_value() && *reference_res >= _config.res_threshold) {
        compared_indices.push_back(index);
      }
    }

    const auto test_resistances = _resistance_solver.equivalentResistances(test_net, pairs, compared_indices);
    const bool configured_paths
        = !_config.from_pin.empty() || !_config.to_pin.empty() || !_config.from_pins.empty() || !_config.to_pins.empty() || !_config.from_to_pins.empty();
    const ReferenceNameMapOrder reference_name_map_order(reference_net);

    for (Size output_index = 0; output_index < compared_indices.size(); ++output_index) {
      const Size index = compared_indices[output_index];
      const auto& pair = pairs[index];
      const auto& reference_res = reference_resistances[index];
      const auto& test_res = test_resistances[output_index];
      const NodePair report_pair = configured_paths ? pair : reference_name_map_order.reportPair(pair);

      ResistanceRow row;
      row.net = net_name;
      row.from_pin = report_pair.first;
      row.to_pin = report_pair.second;
      row.reference_valid = true;
      row.reference = *reference_res;
      row.test_valid = test_res.has_value();
      row.test = test_res.value_or(std::numeric_limits<F64>::quiet_NaN());
      row.delta = test_res.has_value() ? *test_res - *reference_res : std::numeric_limits<F64>::quiet_NaN();
      row.relative_delta = test_res.has_value() ? math::absoluteRelativeDelta(*test_res, *reference_res) : std::nullopt;
      result.p2p_rows.push_back(std::move(row));
    }
  }

  const Config& _config;
  const NetSelector& _net_selector;
  const PathPairGenerator& _path_pair_generator;
  const ResistanceSolver& _resistance_solver;
  bool _compare_capacitance = false;
  bool _compare_resistance = false;
};

}  // namespace

Comparator::Comparator(const Config& config)
    : _config(config),
      _net_selector(config),
      _coupling_cap_comparator(config),
      _path_pair_generator(config),
      _compare_capacitance(compare_mode::compareCapacitance(config)),
      _compare_resistance(compare_mode::compareResistance(config))
{
}

auto Comparator::compare(const Data& test, const Data& reference) const -> Result
{
  Result result;
  initializeSummary(test, reference, result);
  compareMatchedNets(test, reference, result);
  collectTestOnlyNets(test, reference, result);
  if (_compare_capacitance) {
    _coupling_cap_comparator.compare(test, reference, result);
  }
  finishSummary(test, reference, result);
  return result;
}

void Comparator::initializeSummary(const Data& test, const Data& reference, Result& result) const
{
  result.summary.reference_net_count = reference.nets.size();
  result.summary.test_net_count = test.nets.size();
  result.summary.reference_coupling_count = reference.coupling_caps.size();
  result.summary.test_coupling_count = test.coupling_caps.size();
}

void Comparator::compareMatchedNets(const Data& test, const Data& reference, Result& result) const
{
  const auto jobs = makeNetCompareJobs(test, reference);
  MatchedNetComparator matched_net_comparator(_config, _net_selector, _path_pair_generator, _resistance_solver, _compare_capacitance, _compare_resistance);
  Result matched_result = matched_net_comparator.compare(jobs);
  result.summary.matched_net_count += matched_result.summary.matched_net_count;
  appendRows(result, std::move(matched_result));
}

void Comparator::collectTestOnlyNets(const Data& test, const Data& reference, Result& result) const
{
  for (const Net& net : test.nets) {
    const std::string& net_name = net.name;
    if (!reference.index.containsNet(net_name)) {
      result.test_only_nets.push_back(net_name);
    }
  }
}

void Comparator::finishSummary(const Data& test, const Data& reference, Result& result) const
{
  result.summary.reference_only_net_count = result.reference_only_nets.size();
  result.summary.test_only_net_count = result.test_only_nets.size();
  result.summary.reference_only_coupling_count = result.reference_only_couplings.size();
  result.summary.test_only_coupling_count = result.test_only_couplings.size();
  result.summary.tcap_row_count = result.tcap_rows.size();
  result.summary.gcap_row_count = result.gcap_rows.size();
  result.summary.ccap_row_count = result.ccap_rows.size();
  result.summary.p2p_row_count = result.p2p_rows.size();
  _result_sorter.sort(result, test, reference);
}

}  // namespace compare_spef
}  // namespace ircx
