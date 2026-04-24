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
 * @file SolverNetBuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include "SolverNetBuilder.hh"

#include <algorithm>
#include <ranges>

#include "TimingPropagator.hh"
#include "TreeBuilder.hh"
#include "log/Log.hh"

namespace icts {

Net* SolverNetBuilder::connectNet(SolverPipelineState& state, Pin* driver, const std::vector<Pin*>& loads, const std::string& stage_tag,
                                  bool allow_long_wire_buffering) const
{
  auto net_name = ComposeSolverName(state.net_name, "_", stage_tag, "_", _runtime.genId());
  return connectNamedNet(state, net_name, driver, loads, stage_tag, allow_long_wire_buffering);
}

Net* SolverNetBuilder::connectNamedNet(SolverPipelineState& state, const std::string& net_name, Pin* driver,
                                       const std::vector<Pin*>& loads, const std::string& stage_tag,
                                       bool allow_long_wire_buffering) const
{
  LOG_FATAL_IF(driver == nullptr) << "Net [" << state.net_name << "] " << stage_tag << " has null driver.";
  LOG_FATAL_IF(loads.empty()) << "Net [" << state.net_name << "] " << stage_tag << " has empty loads.";

  std::ranges::for_each(loads, [&](Pin* load) {
    LOG_FATAL_IF(load == nullptr) << "Net [" << state.net_name << "] " << stage_tag << " has null load.";
    TreeBuilder::directConnectTree(driver, load);
  });

  return createNamedNetRecord(state, net_name, driver, loads, stage_tag, allow_long_wire_buffering);
}

Net* SolverNetBuilder::createNetRecord(SolverPipelineState& state, Pin* driver, const std::vector<Pin*>& loads,
                                       const std::string& stage_tag, bool allow_long_wire_buffering) const
{
  auto net_name = ComposeSolverName(state.net_name, "_", stage_tag, "_", _runtime.genId());
  return createNamedNetRecord(state, net_name, driver, loads, stage_tag, allow_long_wire_buffering);
}

Net* SolverNetBuilder::createNamedNetRecord(SolverPipelineState& state, const std::string& net_name, Pin* driver,
                                            const std::vector<Pin*>& loads, const std::string& stage_tag,
                                            bool allow_long_wire_buffering) const
{
  LOG_FATAL_IF(driver == nullptr) << "Net [" << state.net_name << "] " << stage_tag << " has null driver.";
  LOG_FATAL_IF(loads.empty()) << "Net [" << state.net_name << "] " << stage_tag << " has empty loads.";

  auto* net = TimingPropagator::genNet(net_name, driver, loads);
  TimingPropagator::update(net);
  state.net_records.push_back({net, -1, allow_long_wire_buffering});
  state.nets.push_back(net);
  return net;
}

void SolverNetBuilder::registerBuffer(SolverPipelineState& state, Inst* buffer, int depth) const
{
  LOG_FATAL_IF(buffer == nullptr) << "Net [" << state.net_name << "] tried to register a null buffer.";
  auto [it, inserted] = state.buffer_depths.emplace(buffer, depth);
  LOG_FATAL_IF(!inserted && it->second != depth)
      << "Net [" << state.net_name << "] buffer " << buffer->get_name() << " depth mismatch: " << it->second << " vs " << depth;

  if (depth >= static_cast<int>(state.buffers_by_depth.size())) {
    state.buffers_by_depth.resize(depth + 1);
  }
  if (inserted) {
    state.buffers_by_depth[depth].push_back(buffer);
  }
  state.max_depth = std::max(state.max_depth, depth);
}

void SolverNetBuilder::finalizeLeafDepth(SolverPipelineState& state, Pin* leaf_load, int depth) const
{
  auto* leaf_inst = leaf_load == nullptr ? nullptr : leaf_load->get_inst();
  registerBuffer(state, leaf_inst, depth);
}

int SolverNetBuilder::childDepth(const SolverPipelineState& state, Pin* child) const
{
  if (child == nullptr) {
    return -1;
  }
  auto* inst = child->get_inst();
  if (inst == nullptr) {
    return -1;
  }
  auto it = state.buffer_depths.find(inst);
  return it == state.buffer_depths.end() ? -1 : it->second;
}

}  // namespace icts
