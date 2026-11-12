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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "RcTreeApprox.hpp"

#include <algorithm>
#include <cmath>

#include "DataManager.hpp"
#include "Database.hpp"
#include "DelayCache.hpp"
#include "OutputLoadEstimator.hpp"
#include "ParasiticGraph.hpp"
#include "ParasiticNet.hpp"
#include "PWHeader.hpp"

namespace ipw {

namespace {

// 四个遍历全部按节点 id 走稠密数组（visited 用 byte、状态用 double）：
// - 邻接表插入序 == 旧 buildParasiticResistorMap 的顺序
// - 遍历/累加顺序、每处 ×1E-3 的位置都与旧实现一致，保证结果逐位相同
// - 数组默认 0.0 等价于旧代码 std::map::operator[] 的"读即插入 0.0"

double updateParasiticLoadMap(
    const ParasiticGraph& graph,
    const std::vector<double>& pin_cap_list,
    uint32_t node_id,
    uint32_t parent_node_id,
    std::vector<uint8_t>& visited_node_list,
    std::vector<double>& node_load_map
)
{
  if (visited_node_list[node_id] != 0) {
    return 0.0;
  }
  visited_node_list[node_id] = 1;

  double subtree_load = parent_node_id == kInvalidParasiticNode ? graph.node_capacitance[node_id]
                                                                : parasiticNodeLoad(graph, pin_cap_list, node_id);
  const std::vector<ParasiticGraphEdge>& edge_list = graph.adjacency[node_id];
  if (edge_list.empty()) {
    node_load_map[node_id] = subtree_load;
    return subtree_load;
  }

  for (const ParasiticGraphEdge& next_edge : edge_list) {
    if (next_edge.node == parent_node_id) {
      continue;
    }
    subtree_load += updateParasiticLoadMap(
        graph,
        pin_cap_list,
        next_edge.node,
        node_id,
        visited_node_list,
        node_load_map
    );
  }
  node_load_map[node_id] = subtree_load;
  return subtree_load;
}

void updateParasiticDelayMap(
    const ParasiticGraph& graph,
    uint32_t node_id,
    uint32_t parent_node_id,
    std::vector<uint8_t>& visited_node_list,
    const std::vector<double>& node_load_map,
    std::vector<double>& node_delay_map,
    std::vector<uint8_t>& node_delay_valid_list
)
{
  if (visited_node_list[node_id] != 0) {
    return;
  }
  visited_node_list[node_id] = 1;
  const std::vector<ParasiticGraphEdge>& edge_list = graph.adjacency[node_id];
  if (edge_list.empty()) {
    return;
  }

  for (const ParasiticGraphEdge& next_edge : edge_list) {
    if (next_edge.node == parent_node_id || visited_node_list[next_edge.node] != 0) {
      continue;
    }
    double node_delay = node_delay_map[node_id];
    double next_node_load = node_load_map[next_edge.node];
    node_delay_map[next_edge.node] = node_delay + next_edge.resistance * next_node_load * 1E-3;
    node_delay_valid_list[next_edge.node] = 1;
    updateParasiticDelayMap(
        graph,
        next_edge.node,
        node_id,
        visited_node_list,
        node_load_map,
        node_delay_map,
        node_delay_valid_list
    );
  }
}

double updateParasiticLoadDelayMap(
    const ParasiticGraph& graph,
    const std::vector<double>& pin_cap_list,
    uint32_t node_id,
    uint32_t parent_node_id,
    std::vector<uint8_t>& visited_node_list,
    const std::vector<double>& node_delay_map,
    std::vector<double>& load_delay_map
)
{
  if (visited_node_list[node_id] != 0) {
    return 0.0;
  }
  visited_node_list[node_id] = 1;

  double load_delay = parasiticNodeLoad(graph, pin_cap_list, node_id) * node_delay_map[node_id];
  const std::vector<ParasiticGraphEdge>& edge_list = graph.adjacency[node_id];
  if (edge_list.empty()) {
    load_delay_map[node_id] = load_delay;
    return load_delay;
  }

  for (const ParasiticGraphEdge& next_edge : edge_list) {
    if (next_edge.node == parent_node_id) {
      continue;
    }
    load_delay += updateParasiticLoadDelayMap(
        graph,
        pin_cap_list,
        next_edge.node,
        node_id,
        visited_node_list,
        node_delay_map,
        load_delay_map
    );
  }
  load_delay_map[node_id] = load_delay;
  return load_delay;
}

void updateParasiticImpulseMap(
    const ParasiticGraph& graph,
    uint32_t node_id,
    uint32_t parent_node_id,
    std::vector<uint8_t>& visited_node_list,
    const std::vector<double>& node_delay_map,
    const std::vector<double>& load_delay_map,
    std::vector<double>& beta_map,
    std::vector<double>& node_impulse_map,
    std::vector<uint8_t>& node_impulse_valid_list
)
{
  if (visited_node_list[node_id] != 0) {
    return;
  }
  visited_node_list[node_id] = 1;

  const std::vector<ParasiticGraphEdge>& edge_list = graph.adjacency[node_id];
  if (!edge_list.empty()) {
    for (const ParasiticGraphEdge& next_edge : edge_list) {
      if (next_edge.node == parent_node_id || visited_node_list[next_edge.node] != 0) {
        continue;
      }
      beta_map[next_edge.node] = beta_map[node_id] + next_edge.resistance * load_delay_map[next_edge.node] * 1E-3;
      updateParasiticImpulseMap(
          graph,
          next_edge.node,
          node_id,
          visited_node_list,
          node_delay_map,
          load_delay_map,
          beta_map,
          node_impulse_map,
          node_impulse_valid_list
      );
    }
  }

  double node_delay = node_delay_map[node_id];
  double impulse = 2.0 * beta_map[node_id] - std::pow(node_delay, 2);
  node_impulse_map[node_id] = std::max(0.0, impulse);
  node_impulse_valid_list[node_id] = 1;
}

}  // namespace

void buildParasiticDelayMap(
    const ParasiticGraph& graph,
    ParasiticCornerState& corner_state,
    uint32_t source_node_id,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  if (corner_state.node_load.size() != graph.size()) {
    corner_state.node_load.assign(graph.size(), 0.0);
    corner_state.node_delay.assign(graph.size(), 0.0);
    corner_state.node_impulse.assign(graph.size(), 0.0);
    corner_state.load_delay.assign(graph.size(), 0.0);
    corner_state.beta.assign(graph.size(), 0.0);
    corner_state.node_delay_valid.assign(graph.size(), 0);
    corner_state.node_impulse_valid.assign(graph.size(), 0);
    corner_state.visited_scratch.assign(graph.size(), 0);
  }
  if (corner_state.source_node_idx == source_node_id && corner_state.node_delay_valid[source_node_id] != 0
      && corner_state.node_impulse_valid[source_node_id] != 0) {
    return;
  }
  corner_state.source_node_idx = source_node_id;
  std::fill(corner_state.node_load.begin(), corner_state.node_load.end(), 0.0);
  std::fill(corner_state.node_delay_valid.begin(), corner_state.node_delay_valid.end(), 0);
  std::fill(corner_state.node_impulse_valid.begin(), corner_state.node_impulse_valid.end(), 0);

  // pin 电容表只在真正要算的时候才建（早退路径以前已经白付了这张表的探测成本）。
  const std::vector<double>& pin_cap_list
      = getParasiticPinCapList(graph, corner_state, analysis_type, trans_type);

  // 旧实现里 load_delay / beta 是每次调用新建的空 map，这里每轮清零复刻该语义。
  std::fill(corner_state.load_delay.begin(), corner_state.load_delay.end(), 0.0);
  std::fill(corner_state.beta.begin(), corner_state.beta.end(), 0.0);

  std::fill(corner_state.visited_scratch.begin(), corner_state.visited_scratch.end(), 0);
  updateParasiticLoadMap(
      graph,
      pin_cap_list,
      source_node_id,
      kInvalidParasiticNode,
      corner_state.visited_scratch,
      corner_state.node_load
  );

  std::fill(corner_state.visited_scratch.begin(), corner_state.visited_scratch.end(), 0);
  corner_state.node_delay[source_node_id] = 0.0;
  corner_state.node_delay_valid[source_node_id] = 1;
  updateParasiticDelayMap(
      graph,
      source_node_id,
      kInvalidParasiticNode,
      corner_state.visited_scratch,
      corner_state.node_load,
      corner_state.node_delay,
      corner_state.node_delay_valid
  );

  std::fill(corner_state.visited_scratch.begin(), corner_state.visited_scratch.end(), 0);
  updateParasiticLoadDelayMap(
      graph,
      pin_cap_list,
      source_node_id,
      kInvalidParasiticNode,
      corner_state.visited_scratch,
      corner_state.node_delay,
      corner_state.load_delay
  );

  std::fill(corner_state.visited_scratch.begin(), corner_state.visited_scratch.end(), 0);
  corner_state.beta[source_node_id] = 0.0;
  updateParasiticImpulseMap(
      graph,
      source_node_id,
      kInvalidParasiticNode,
      corner_state.visited_scratch,
      corner_state.node_delay,
      corner_state.load_delay,
      corner_state.beta,
      corner_state.node_impulse,
      corner_state.node_impulse_valid
  );
}

}  // namespace ipw
