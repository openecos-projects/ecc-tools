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
 * @file TopologyBuilderOperator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include "TopologyBuilderOperator.hh"

#include <ranges>
#include <sstream>

#include "BalanceClustering.hh"
#include "TimingPropagator.hh"
#include "TreeBuilder.hh"
#include "log/Log.hh"

namespace icts {

namespace {
constexpr double kCapEpsilon = 1e-12;

double calcPinCapSum(const std::vector<Pin*>& loads)
{
  double pin_cap_sum = 0.0;
  std::ranges::for_each(loads, [&](Pin* load) { pin_cap_sum += load == nullptr ? 0.0 : load->get_cap_load(); });
  return pin_cap_sum;
}
}  // namespace

void TopologyBuilderOperator::run(SolverPipelineState& state) const
{
  if (state.leaf_load_pins.empty()) {
    return;
  }

  // Preserve a dedicated H-tree root buffer so the top-level source->root segment
  // stays explicit for downstream long-wire buffering and level-based sizing.
  auto root_name = ComposeSolverName(state.net_name, "_root_", _runtime.genId());
  auto root_loc = BalanceClustering::calcCentroid(state.sink_pins);
  auto* root_buf = TreeBuilder::genBufInst(root_name, root_loc);
  root_buf->set_cell_master(TimingPropagator::getMinSizeCell());
  _net_builder.registerBuffer(state, root_buf, 0);
  _net_builder.connectNamedNet(state, state.net_name, state.driver, {root_buf->get_load_pin()}, "root");
  buildSubTree(state, root_buf->get_driver_pin(), state.leaf_load_pins, 1);
}

bool TopologyBuilderOperator::tryBuildSteinerSubTree(SolverPipelineState& state, Pin* parent_driver, const std::vector<Pin*>& subtree_loads,
                                                     int depth) const
{
  const double max_cap = TimingPropagator::getMaxCap();
  const int max_fanout = TimingPropagator::getMaxFanout();
  if (parent_driver == nullptr || subtree_loads.size() <= 1 || max_cap <= 0.0 || max_fanout <= 1) {
    return false;
  }
  if (subtree_loads.size() >= static_cast<size_t>(max_fanout)) {
    return false;
  }

  const double pin_cap_sum = calcPinCapSum(subtree_loads);
  if (pin_cap_sum >= max_cap - kCapEpsilon) {
    return false;
  }

  const double steiner_wire_length = TreeBuilder::estimateFluteWireLength(parent_driver, subtree_loads);
  const double steiner_wire_cap = steiner_wire_length * TimingPropagator::getUnitCap();
  if (pin_cap_sum + steiner_wire_cap >= max_cap - kCapEpsilon) {
    return false;
  }

  std::ranges::for_each(subtree_loads, [&](Pin* load) { _net_builder.finalizeLeafDepth(state, load, depth); });

  auto steiner_name = ComposeSolverName(state.net_name, "_steiner_", depth, "_", _runtime.genId());
  TreeBuilder::fluteTree(steiner_name, parent_driver, subtree_loads);
  _net_builder.createNetRecord(state, parent_driver, subtree_loads, "steiner", false);

  std::ostringstream stop_summary;
  stop_summary << "Net [" << state.net_name << "] stop H-tree split at depth " << depth << " with " << subtree_loads.size()
               << " loads; max_fanout=" << max_fanout << ", pin_cap=" << pin_cap_sum << ", steiner_wire_cap=" << steiner_wire_cap
               << ", max_cap=" << max_cap;
  LOG_INFO << stop_summary.str();
  _runtime.saveToLog(stop_summary.str());
  return true;
}

void TopologyBuilderOperator::buildSubTree(SolverPipelineState& state, Pin* parent_driver, const std::vector<Pin*>& subtree_loads,
                                           int depth) const
{
  if (subtree_loads.empty()) {
    return;
  }

  if (subtree_loads.size() == 1) {
    _net_builder.finalizeLeafDepth(state, subtree_loads.front(), depth);
    _net_builder.connectNet(state, parent_driver, subtree_loads, "attach");
    return;
  }

  if (tryBuildSteinerSubTree(state, parent_driver, subtree_loads, depth)) {
    return;
  }

  auto child_clusters = BalanceClustering::balancedBiPartition(subtree_loads, 0.1, 8, false);
  LOG_FATAL_IF(child_clusters.size() != 2) << "Net [" << state.net_name << "] failed to generate a binary H-tree split.";
  std::vector<Point> fixed_locs;
  fixed_locs.reserve(subtree_loads.size());
  std::ranges::for_each(subtree_loads, [&](Pin* pin) { fixed_locs.push_back(pin->get_location()); });

  std::vector<Point> branch_locs;
  for (size_t i = 0; i < child_clusters.size(); ++i) {
    if (child_clusters[i].size() > 1) {
      branch_locs.push_back(BalanceClustering::calcCentroid(child_clusters[i]));
    }
  }
  if (!branch_locs.empty()) {
    TreeBuilder::localPlace(branch_locs, fixed_locs);
  }

  struct RecursiveTask
  {
    Pin* driver = nullptr;
    std::vector<Pin*> loads;
    int depth = 0;
  };

  std::vector<Pin*> child_load_pins(child_clusters.size(), nullptr);
  std::vector<RecursiveTask> recursive_tasks;
  size_t branch_loc_idx = 0;

  for (size_t i = 0; i < child_clusters.size(); ++i) {
    auto& cluster = child_clusters[i];
    if (cluster.size() == 1) {
      _net_builder.finalizeLeafDepth(state, cluster.front(), depth);
      child_load_pins[i] = cluster.front();
      continue;
    }

    auto branch_name = ComposeSolverName(state.net_name, "_branch_", depth, "_", i, "_", _runtime.genId());
    auto* branch_buf = TreeBuilder::genBufInst(branch_name, branch_locs[branch_loc_idx++]);
    branch_buf->set_cell_master(TimingPropagator::getMinSizeCell());
    _net_builder.registerBuffer(state, branch_buf, depth);
    child_load_pins[i] = branch_buf->get_load_pin();
    recursive_tasks.push_back({branch_buf->get_driver_pin(), cluster, depth + 1});
  }

  _net_builder.connectNet(state, parent_driver, child_load_pins, "branch");

  std::ranges::for_each(recursive_tasks, [&](const RecursiveTask& task) { buildSubTree(state, task.driver, task.loads, task.depth); });
}

}  // namespace icts
