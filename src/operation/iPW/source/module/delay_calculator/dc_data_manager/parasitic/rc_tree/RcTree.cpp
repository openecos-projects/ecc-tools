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
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "RcTree.hpp"

#include "DataManager.hpp"
#include "Database.hpp"
#include "DelayCache.hpp"
#include "OutputLoadEstimator.hpp"
#include "ParasiticDmpModel.hpp"
#include "ParasiticGraph.hpp"
#include "ParasiticResistor.hpp"
#include "Pin.hpp"
#include "ParasiticNet.hpp"
#include "PWHeader.hpp"
#include <algorithm>

namespace ipw {

void flattenRcTree(
    ParasiticNet& parasitic_net,
    uint32_t source_node_id,
    AnalysisType analysis_type,
    TransType trans_type,
    std::vector<uint32_t>& node_id_list,
    std::vector<int32_t>& parent_idx_list,
    std::vector<double>& resistance_list,
    std::vector<double>& capacitance_list
)
{
  const ParasiticGraph& graph = getParasiticGraph(parasitic_net);
  const std::vector<double>& pin_cap_list = getParasiticPinCapList(parasitic_net, analysis_type, trans_type);

  node_id_list.clear();
  parent_idx_list.clear();
  resistance_list.clear();
  capacitance_list.clear();

  std::vector<uint8_t> visited_node_list(graph.size(), 0);
  node_id_list.push_back(source_node_id);
  parent_idx_list.push_back(-1);
  resistance_list.push_back(0.0);
  capacitance_list.push_back(graph.node_capacitance[source_node_id]);
  visited_node_list[source_node_id] = 1;

  std::vector<std::size_t> stack_node_idx_list;
  std::vector<std::size_t> stack_next_edge_idx_list;
  stack_node_idx_list.push_back(0);
  stack_next_edge_idx_list.push_back(0);
  while (!stack_node_idx_list.empty()) {
    std::size_t node_idx = stack_node_idx_list.back();
    const std::vector<ParasiticGraphEdge>& edge_list = graph.adjacency[node_id_list[node_idx]];
    if (edge_list.empty()) {
      stack_node_idx_list.pop_back();
      stack_next_edge_idx_list.pop_back();
      continue;
    }

    std::size_t& edge_idx = stack_next_edge_idx_list.back();
    if (edge_idx >= edge_list.size()) {
      stack_node_idx_list.pop_back();
      stack_next_edge_idx_list.pop_back();
      continue;
    }

    const ParasiticGraphEdge& next_edge = edge_list[edge_idx];
    edge_idx++;
    if (visited_node_list[next_edge.node] != 0) {
      continue;
    }

    visited_node_list[next_edge.node] = 1;
    node_id_list.push_back(next_edge.node);
    parent_idx_list.push_back(static_cast<int32_t>(node_idx));
    resistance_list.push_back(next_edge.resistance * 1E-3);
    capacitance_list.push_back(parasiticNodeLoad(graph, pin_cap_list, next_edge.node));
    stack_node_idx_list.push_back(node_id_list.size() - 1);
    stack_next_edge_idx_list.push_back(0);
  }
}

void buildPiModel(
    ParasiticDmpModel& dmp_model,
    std::vector<int32_t>& parent_idx_list,
    std::vector<double>& resistance_list,
    std::vector<double>& capacitance_list
)
{
  if (parent_idx_list.size() != resistance_list.size() || resistance_list.size() != capacitance_list.size()
      || capacitance_list.empty()) {
    return;
  }

  std::vector<double> admittance1_list = capacitance_list;
  std::vector<double> admittance2_list(capacitance_list.size(), 0.0);
  std::vector<double> admittance3_list(capacitance_list.size(), 0.0);
  for (std::size_t node_idx = capacitance_list.size() - 1; node_idx > 0; node_idx--) {
    int32_t parent_idx = parent_idx_list[node_idx];
    if (parent_idx < 0) {
      return;
    }
    double resistance = resistance_list[node_idx];
    double admittance1 = admittance1_list[node_idx];
    double admittance2 = admittance2_list[node_idx];
    double admittance3 = admittance3_list[node_idx];
    admittance1_list[parent_idx] += admittance1;
    admittance2_list[parent_idx] += admittance2 - resistance * admittance1 * admittance1;
    admittance3_list[parent_idx] += admittance3 - 2.0 * resistance * admittance1 * admittance2
                                    + resistance * resistance * admittance1 * admittance1 * admittance1;
  }

  double admittance1 = admittance1_list.front();
  double admittance2 = admittance2_list.front();
  double admittance3 = admittance3_list.front();
  double driver_capacitance = 0.0;
  double pi_resistance = 0.0;
  double load_capacitance = 0.0;
  if (admittance2 == 0.0 && admittance3 == 0.0) {
    load_capacitance = admittance1;
  } else {
    if (admittance3 == 0.0 || admittance2 == 0.0) {
      return;
    }
    load_capacitance = admittance2 * admittance2 / admittance3;
    driver_capacitance = admittance1 - load_capacitance;
    pi_resistance = -admittance3 * admittance3 / (admittance2 * admittance2 * admittance2);
  }
  if (!std::isfinite(driver_capacitance) || !std::isfinite(pi_resistance) || !std::isfinite(load_capacitance)
      || driver_capacitance < 0.0 || pi_resistance < 0.0 || load_capacitance < 0.0
      || driver_capacitance + load_capacitance <= 0.0) {
    return;
  }

  dmp_model.set_is_valid(true);
  dmp_model.set_driver_capacitance(driver_capacitance);
  dmp_model.set_pi_resistance(pi_resistance);
  dmp_model.set_load_capacitance(load_capacitance);
}





namespace {

// 与旧 getPinNameByParasiticNodeName() 一致：先按原名查 pin，再把 '/' 换成 ':' 查一次。
const std::string* findPinName(const std::string& node_name)
{
  Database& database = PWDM.getDatabase();
  auto pin_iter = database.get_pin_map().find(node_name);
  if (pin_iter != database.get_pin_map().end()) {
    return &pin_iter->first;
  }
  std::string converted_name = node_name;
  std::replace(converted_name.begin(), converted_name.end(), '/', ':');
  pin_iter = database.get_pin_map().find(converted_name);
  if (pin_iter != database.get_pin_map().end()) {
    return &pin_iter->first;
  }
  return nullptr;
}

void buildParasiticGraph(ParasiticNet& parasitic_net, ParasiticGraph& graph)
{
  Database& database = PWDM.getDatabase();
  const std::string* net_name = &parasitic_net.get_net_name();

  graph.node_name.clear();
  graph.adjacency.clear();
  graph.node_capacitance.clear();
  graph.node_pin_index.clear();
  graph.node_pin_name.clear();
  graph.node_id_map.clear();

  auto add_node = [&](const std::string& node_name) -> uint32_t {
    auto iter = graph.node_id_map.find(std::string_view(node_name));
    if (iter != graph.node_id_map.end()) {
      return iter->second;
    }
    const uint32_t node_id = graph.size();
    graph.node_name.push_back(&node_name);
    graph.adjacency.emplace_back();
    graph.node_capacitance.push_back(0.0);
    graph.node_pin_index.push_back(kInvalidParasiticNode);
    graph.node_pin_name.push_back(nullptr);
    graph.node_id_map.emplace(std::string_view(node_name), node_id);
    const std::string* pin_name = findPinName(node_name);
    if (pin_name != nullptr) {
      auto pin_iter = database.get_pin_map().find(*pin_name);
      if (pin_iter != database.get_pin_map().end()) {
        graph.node_pin_index[node_id] = static_cast<uint32_t>(pin_iter->second.get_index());
        graph.node_pin_name[node_id] = pin_name;
      }
    }
    return node_id;
  };

  // 先按 node_capacitance_map 的迭代序建点（id 序 == map 序，保持按 id 求和的顺序与旧代码一致）
  for (std::pair<const std::string, double>& node_pair : parasitic_net.get_node_capacitance_map()) {
    const uint32_t node_id = add_node(node_pair.first);
    graph.node_capacitance[node_id] = node_pair.second;
  }
  graph.capacitance_node_num = graph.size();
  // 再追加只出现在 resistor_list 里的节点（顺序 = resistor_list 迭代序）
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    add_node(parasitic_resistor.get_source_node());
    add_node(parasitic_resistor.get_sink_node());
  }
  // 邻接表：与旧 buildParasiticResistorMap 同样的插入序
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    const uint32_t source_id = graph.find(parasitic_resistor.get_source_node());
    const uint32_t sink_id = graph.find(parasitic_resistor.get_sink_node());
    graph.adjacency[source_id].push_back({sink_id, parasitic_resistor.get_resistance()});
    graph.adjacency[sink_id].push_back({source_id, parasitic_resistor.get_resistance()});
  }
  (void) net_name;
}

}  // namespace

const ParasiticGraph& getParasiticGraph(ParasiticNet& parasitic_net)
{
  auto [graph, is_inserted] = DelayCache::threadCache().parasiticGraph(parasitic_net.get_net_name());
  if (is_inserted) {
    buildParasiticGraph(parasitic_net, graph);
  }
  return graph;
}

uint32_t getParasiticNodeId(const ParasiticGraph& graph, const std::string& pin_name)
{
  uint32_t node_id = graph.find(pin_name);
  if (node_id != kInvalidParasiticNode) {
    return node_id;
  }
  std::string spef_pin_name = pin_name;
  std::replace(spef_pin_name.begin(), spef_pin_name.end(), ':', '/');
  return graph.find(spef_pin_name);
}

uint32_t getParasiticNodeId(ParasiticNet& parasitic_net, const std::string& pin_name)
{
  return getParasiticNodeId(getParasiticGraph(parasitic_net), pin_name);
}

const std::vector<double>& getParasiticPinCapList(
    const ParasiticGraph& graph,
    ParasiticCornerState& state,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  if (!state.pin_cap_ready) {
    state.node_pin_cap.assign(graph.size(), 0.0);
    state.pin_cap_ready = true;
    for (uint32_t node_id = 0; node_id < graph.size(); node_id++) {
      if (graph.is_pin(node_id)) {
        state.node_pin_cap[node_id] = getPinCapacitance(graph.node_pin_index[node_id], analysis_type, trans_type);
      }
    }
  }
  return state.node_pin_cap;
}

const std::vector<double>& getParasiticPinCapList(
    ParasiticNet& parasitic_net,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  const ParasiticGraph& graph = getParasiticGraph(parasitic_net);
  ParasiticCornerState& state
      = DelayCache::threadCache().parasiticCornerState(parasitic_net.get_net_name(), analysis_type, trans_type);
  return getParasiticPinCapList(graph, state, analysis_type, trans_type);
}

}  // namespace ipw
