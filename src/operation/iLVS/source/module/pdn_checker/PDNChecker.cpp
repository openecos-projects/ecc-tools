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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "PDNChecker.hpp"

#include "Die.hpp"
#include "LVSHeader.hpp"
#include "Logger.hpp"
#include "PCSummary.hpp"
#include "PhysicalGraph.hpp"
#include "Utility.hpp"

namespace ilvs {

// public

void PDNChecker::initInst()
{
  if (_pc_instance == nullptr) {
    _pc_instance = new PDNChecker();
  }
}

PDNChecker& PDNChecker::getInst()
{
  if (_pc_instance == nullptr) {
    LVSLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_pc_instance;
}

void PDNChecker::destroyInst()
{
  if (_pc_instance != nullptr) {
    delete _pc_instance;
    _pc_instance = nullptr;
  }
}

// function

void PDNChecker::check()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  PCModel pc_model = initPCModel();
  buildSupplyPoint(pc_model);
  checkSupplyConnectivity(pc_model, ConnectType::kPower);
  checkSupplyConnectivity(pc_model, ConnectType::kGround);
  updateSummary(pc_model);

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

PCModel PDNChecker::initPCModel()
{
  PCModel pc_model;
  return pc_model;
}

void PDNChecker::buildSupplyPoint(PCModel& pc_model)
{
  std::vector<SupplyPoint>& supply_point_list = pc_model.get_supply_point_list();
  supply_point_list = getSupplyPointList();
}

std::vector<SupplyPoint> PDNChecker::getSupplyPointList()
{
  std::vector<SupplyPoint> supply_point_list;
  int32_t top_layer_order = 0;
  int32_t second_top_layer_order = 0;
  if (!getSupplyRoutingLayerOrder(top_layer_order, second_top_layer_order)) {
    return supply_point_list;
  }

  Die& die = LVSDM.getDatabase().get_def_data().get_die();
  if (die.get_real_ll_x() >= die.get_real_ur_x() || die.get_real_ll_y() >= die.get_real_ur_y()) {
    return supply_point_list;
  }
  int32_t center_x = static_cast<int32_t>((static_cast<int64_t>(die.get_real_ll_x()) + die.get_real_ur_x()) / 2);
  int32_t center_y = static_cast<int32_t>((static_cast<int64_t>(die.get_real_ll_y()) + die.get_real_ur_y()) / 2);
  addCenterSupplyPoint(supply_point_list, center_x, center_y, top_layer_order, second_top_layer_order);
  return supply_point_list;
}

bool PDNChecker::getSupplyRoutingLayerOrder(int32_t& top_layer_order, int32_t& second_top_layer_order)
{
  std::set<int32_t> layer_order_set;
  addSupplyViaLayerOrder(layer_order_set, ConnectType::kPower);
  addSupplyViaLayerOrder(layer_order_set, ConnectType::kGround);
  if (layer_order_set.size() < 2) {
    return false;
  }

  std::set<int32_t>::reverse_iterator layer_order_iter = layer_order_set.rbegin();
  top_layer_order = *layer_order_iter;
  layer_order_iter++;
  second_top_layer_order = *layer_order_iter;
  return true;
}

void PDNChecker::addSupplyViaLayerOrder(std::set<int32_t>& layer_order_set, const ConnectType connect_type)
{
  if (!isPowerGround(connect_type)) {
    return;
  }

  PhysicalGraph& physical_graph = LVSDM.getDatabase().get_def_data().get_physical_graph();
  std::set<std::string>& net_name_set =
      connect_type == ConnectType::kPower ? physical_graph.get_power_net_name_set() : physical_graph.get_ground_net_name_set();
  for (const std::string& net_name : net_name_set) {
    std::map<std::string, NetRoutingGraph>::iterator routing_graph_iter = physical_graph.get_net_routing_graph_map().find(net_name);
    if (routing_graph_iter == physical_graph.get_net_routing_graph_map().end()) {
      continue;
    }
    NetRoutingGraph& routing_graph = routing_graph_iter->second;
    std::vector<RoutingShape>& routing_shape_list = routing_graph.get_routing_shape_list();
    for (std::pair<int32_t, int32_t>& via_shape_idx_pair : routing_graph.get_via_shape_idx_pair_list()) {
      if (!isValidRoutingShapeIdx(via_shape_idx_pair.first, routing_shape_list)
          || !isValidRoutingShapeIdx(via_shape_idx_pair.second, routing_shape_list)) {
        continue;
      }
      layer_order_set.insert(routing_shape_list[via_shape_idx_pair.first].get_layer_order());
      layer_order_set.insert(routing_shape_list[via_shape_idx_pair.second].get_layer_order());
    }
  }
}

bool PDNChecker::isPowerGround(const ConnectType connect_type)
{
  return connect_type == ConnectType::kPower || connect_type == ConnectType::kGround;
}

void PDNChecker::addCenterSupplyPoint(std::vector<SupplyPoint>& supply_point_list, const int32_t center_x, const int32_t center_y,
                                      const int32_t top_layer_order, const int32_t second_top_layer_order)
{
  SupplyPoint power_supply_point =
      getCenterSupplyPoint(ConnectType::kPower, center_x, center_y, top_layer_order, second_top_layer_order);
  if (power_supply_point.get_component_id() >= 0) {
    supply_point_list.push_back(std::move(power_supply_point));
  }
  SupplyPoint ground_supply_point =
      getCenterSupplyPoint(ConnectType::kGround, center_x, center_y, top_layer_order, second_top_layer_order);
  if (ground_supply_point.get_component_id() >= 0) {
    supply_point_list.push_back(std::move(ground_supply_point));
  }
}

SupplyPoint PDNChecker::getCenterSupplyPoint(const ConnectType connect_type, const int32_t center_x, const int32_t center_y,
                                             const int32_t top_layer_order, const int32_t second_top_layer_order)
{
  SupplyPoint supply_point;
  if (!isPowerGround(connect_type)) {
    return supply_point;
  }

  PhysicalGraph& physical_graph = LVSDM.getDatabase().get_def_data().get_physical_graph();
  std::set<std::string>& net_name_set =
      connect_type == ConnectType::kPower ? physical_graph.get_power_net_name_set() : physical_graph.get_ground_net_name_set();
  bool has_supply_point = false;
  int64_t shortest_distance = 0;
  for (const std::string& net_name : net_name_set) {
    std::map<std::string, NetRoutingGraph>::iterator routing_graph_iter = physical_graph.get_net_routing_graph_map().find(net_name);
    if (routing_graph_iter == physical_graph.get_net_routing_graph_map().end()) {
      continue;
    }
    std::map<std::string, std::vector<int32_t>>::iterator component_id_list_iter =
        physical_graph.get_net_routing_shape_component_id_list_map().find(net_name);
    if (component_id_list_iter == physical_graph.get_net_routing_shape_component_id_list_map().end()) {
      continue;
    }
    NetRoutingGraph& routing_graph = routing_graph_iter->second;
    std::vector<RoutingShape>& routing_shape_list = routing_graph.get_routing_shape_list();
    std::vector<int32_t>& component_id_list = component_id_list_iter->second;
    for (std::pair<int32_t, int32_t>& via_shape_idx_pair : routing_graph.get_via_shape_idx_pair_list()) {
      int32_t top_routing_shape_idx =
          getTopRoutingShapeIdx(routing_graph, via_shape_idx_pair, top_layer_order, second_top_layer_order);
      if (top_routing_shape_idx < 0 || top_routing_shape_idx >= static_cast<int32_t>(component_id_list.size())) {
        continue;
      }
      int32_t component_id = component_id_list[top_routing_shape_idx];
      if (component_id < 0) {
        continue;
      }
      int64_t distance = getShapeCenterDistance(routing_shape_list[top_routing_shape_idx].get_shape(), center_x, center_y);
      if (!has_supply_point || distance < shortest_distance
          || (distance == shortest_distance && component_id < supply_point.get_component_id())) {
        supply_point.set_component_id(component_id);
        supply_point.set_connect_type(connect_type);
        shortest_distance = distance;
        has_supply_point = true;
      }
    }
  }
  return supply_point;
}

int32_t PDNChecker::getTopRoutingShapeIdx(const NetRoutingGraph& routing_graph,
                                          const std::pair<int32_t, int32_t>& via_shape_idx_pair,
                                          const int32_t top_layer_order, const int32_t second_top_layer_order)
{
  const std::vector<RoutingShape>& routing_shape_list = routing_graph.get_routing_shape_list();
  int32_t first_routing_shape_idx = via_shape_idx_pair.first;
  int32_t second_routing_shape_idx = via_shape_idx_pair.second;
  if (!isValidRoutingShapeIdx(first_routing_shape_idx, routing_shape_list)
      || !isValidRoutingShapeIdx(second_routing_shape_idx, routing_shape_list)) {
    return -1;
  }

  const RoutingShape& first_routing_shape = routing_shape_list[first_routing_shape_idx];
  const RoutingShape& second_routing_shape = routing_shape_list[second_routing_shape_idx];
  if (first_routing_shape.get_layer_order() == top_layer_order
      && second_routing_shape.get_layer_order() == second_top_layer_order) {
    return first_routing_shape_idx;
  }
  if (second_routing_shape.get_layer_order() == top_layer_order
      && first_routing_shape.get_layer_order() == second_top_layer_order) {
    return second_routing_shape_idx;
  }
  return -1;
}

bool PDNChecker::isValidRoutingShapeIdx(const int32_t routing_shape_idx, const std::vector<RoutingShape>& routing_shape_list)
{
  return routing_shape_idx >= 0 && routing_shape_idx < static_cast<int32_t>(routing_shape_list.size());
}

int64_t PDNChecker::getShapeCenterDistance(const Shape& shape, const int32_t point_x, const int32_t point_y)
{
  int64_t shape_center_x = (static_cast<int64_t>(shape.get_ll_x()) + shape.get_ur_x()) / 2;
  int64_t shape_center_y = (static_cast<int64_t>(shape.get_ll_y()) + shape.get_ur_y()) / 2;
  int64_t delta_x = shape_center_x - point_x;
  int64_t delta_y = shape_center_y - point_y;
  return delta_x * delta_x + delta_y * delta_y;
}

void PDNChecker::checkSupplyConnectivity(PCModel& pc_model, const ConnectType connect_type)
{
  std::vector<SupplyPoint>& supply_point_list = pc_model.get_supply_point_list();
  std::vector<Violation>& violation_list = pc_model.get_violation_list();
  if (!isPowerGround(connect_type)) {
    return;
  }
  PhysicalGraph& physical_graph = LVSDM.getDatabase().get_def_data().get_physical_graph();
  std::map<std::string, std::string>& instance_pin_net_map = connect_type == ConnectType::kPower
                                                                 ? physical_graph.get_power_instance_pin_net_map()
                                                                 : physical_graph.get_ground_instance_pin_net_map();
  if (instance_pin_net_map.empty()) {
    return;
  }

  std::set<int32_t> supply_component_id_set;
  for (SupplyPoint& supply_point : supply_point_list) {
    if (supply_point.get_connect_type() == connect_type) {
      supply_component_id_set.insert(supply_point.get_component_id());
    }
  }
  if (supply_component_id_set.empty()) {
    Violation violation;
    violation.set_violation_type(connect_type == ConnectType::kPower ? ViolationType::kPowerOpenVDD : ViolationType::kPowerOpenVSS);
    violation.set_terminal_name_list(LVSUTIL.getSortedKeyNameList(instance_pin_net_map));
    for (auto& [terminal_name, net_name] : instance_pin_net_map) {
      (void) terminal_name;
      violation.get_related_net_name_list().push_back(net_name);
    }
    violation.set_related_net_name_list(LVSUTIL.getSortedUniqueList(violation.get_related_net_name_list()));
    violation_list.push_back(std::move(violation));
    return;
  }

  std::map<std::pair<std::string, int32_t>, std::vector<std::string>> disconnected_terminal_map;
  for (auto& [terminal_name, net_name] : instance_pin_net_map) {
    std::map<std::string, int32_t>::iterator component_iter = physical_graph.get_terminal_component_map().find(terminal_name);
    if (component_iter != physical_graph.get_terminal_component_map().end()
        && LVSUTIL.exist(supply_component_id_set, component_iter->second)) {
      continue;
    }
    int32_t component_id = -1;
    if (component_iter != physical_graph.get_terminal_component_map().end()) {
      component_id = component_iter->second;
    }
    disconnected_terminal_map[{net_name, component_id}].push_back(terminal_name);
  }
  for (auto& [key, terminal_name_list] : disconnected_terminal_map) {
    std::sort(terminal_name_list.begin(), terminal_name_list.end());
    Violation violation;
    violation.set_violation_type(connect_type == ConnectType::kPower ? ViolationType::kPowerOpenVDD : ViolationType::kPowerOpenVSS);
    violation.set_net_name(key.first);
    violation.set_terminal_name_list(terminal_name_list);
    if (key.second != -1) {
      violation.get_component_id_list().push_back(key.second);
    }
    violation_list.push_back(std::move(violation));
  }
}

void PDNChecker::updateSummary(PCModel& pc_model)
{
  PCSummary& pc_summary = LVSDM.getDatabase().get_summary().pc_summary;
  pc_summary.reset();

  std::vector<Violation>& violation_list = pc_model.get_violation_list();
  for (Violation& violation : violation_list) {
    if (violation.get_violation_type() == ViolationType::kPowerOpenVDD) {
      pc_summary.open_vdd_num += violation.get_terminal_name_list().size();
    } else if (violation.get_violation_type() == ViolationType::kPowerOpenVSS) {
      pc_summary.open_vss_num += violation.get_terminal_name_list().size();
    }
  }
  pc_summary.violation_list = std::move(violation_list);
}

// private

PDNChecker* PDNChecker::_pc_instance = nullptr;

}  // namespace ilvs
