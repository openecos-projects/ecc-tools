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

#include "LVSHeader.hpp"
#include "Logger.hpp"
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

  Database& database = LVSDM.getDatabase();
  database.get_summary().pc_summary.reset();

  PCModel pc_model = initPCModel();
  buildSupplyPoint(pc_model);
  PhysicalGraph& physical_graph = database.get_def_data().get_physical_graph();
  checkSupplyConnectivity(pc_model, physical_graph.get_power_instance_pin_net_map(), ConnectType::kPower);
  checkSupplyConnectivity(pc_model, physical_graph.get_ground_instance_pin_net_map(), ConnectType::kGround);

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

PCModel PDNChecker::initPCModel()
{
  PCModel pc_model;
  return pc_model;
}

void PDNChecker::buildSupplyPoint(PCModel& pc_model)
{
  PhysicalGraph& physical_graph = LVSDM.getDatabase().get_def_data().get_physical_graph();
  std::vector<SupplyPoint>& supply_point_list = pc_model.get_supply_point_list();
  supply_point_list = getSupplyPointList(physical_graph);
}

std::vector<SupplyPoint> PDNChecker::getSupplyPointList(const PhysicalGraph& physical_graph)
{
  int32_t highest_layer_order = -1;
  for (const SupplyRouteShape& route_shape : physical_graph.get_supply_route_shape_list()) {
    bool is_power = LVSUTIL.exist(physical_graph.get_power_net_name_set(), route_shape.get_net_name());
    bool is_ground = LVSUTIL.exist(physical_graph.get_ground_net_name_set(), route_shape.get_net_name());
    if (is_power || is_ground) {
      highest_layer_order = std::max(highest_layer_order, route_shape.get_layer_order());
    }
  }
  if (highest_layer_order < 0) {
    return {};
  }

  int64_t horizontal_span = 0;
  int64_t vertical_span = 0;
  for (const SupplyRouteShape& route_shape : physical_graph.get_supply_route_shape_list()) {
    if (route_shape.get_layer_order() != highest_layer_order) {
      continue;
    }
    bool is_power = LVSUTIL.exist(physical_graph.get_power_net_name_set(), route_shape.get_net_name());
    bool is_ground = LVSUTIL.exist(physical_graph.get_ground_net_name_set(), route_shape.get_net_name());
    if (!is_power && !is_ground) {
      continue;
    }
    const Shape& shape = route_shape.get_shape();
    int64_t span_x = static_cast<int64_t>(shape.get_ur_x()) - shape.get_ll_x();
    int64_t span_y = static_cast<int64_t>(shape.get_ur_y()) - shape.get_ll_y();
    if (span_x > span_y) {
      horizontal_span += span_x;
    } else if (span_y > span_x) {
      vertical_span += span_y;
    }
  }
  if (horizontal_span == 0 && vertical_span == 0) {
    return {};
  }
  bool is_horizontal = horizontal_span >= vertical_span;

  std::map<SupplyTrackKey, SupplyTrack> supply_track_map;
  for (const SupplyRouteShape& route_shape : physical_graph.get_supply_route_shape_list()) {
    if (route_shape.get_layer_order() != highest_layer_order) {
      continue;
    }
    ConnectType connect_type = ConnectType::kNone;
    if (LVSUTIL.exist(physical_graph.get_power_net_name_set(), route_shape.get_net_name())) {
      connect_type = ConnectType::kPower;
    } else if (LVSUTIL.exist(physical_graph.get_ground_net_name_set(), route_shape.get_net_name())) {
      connect_type = ConnectType::kGround;
    }
    if (!isPowerGround(connect_type)) {
      continue;
    }
    const Shape& shape = route_shape.get_shape();
    int64_t span_x = static_cast<int64_t>(shape.get_ur_x()) - shape.get_ll_x();
    int64_t span_y = static_cast<int64_t>(shape.get_ur_y()) - shape.get_ll_y();
    if ((is_horizontal && span_x <= span_y) || (!is_horizontal && span_y <= span_x)) {
      continue;
    }
    int32_t position = is_horizontal ? getMidpoint(shape.get_ll_y(), shape.get_ur_y()) : getMidpoint(shape.get_ll_x(), shape.get_ur_x());
    SupplyTrackKey key = {connect_type, route_shape.get_net_name(), route_shape.get_component_id(), shape.get_layer_idx(),
                                route_shape.get_layer_order(), position};
    SupplyTrack supply_track;
    supply_track.set_connect_type(connect_type);
    supply_track.set_net_name(route_shape.get_net_name());
    supply_track.set_component_id(route_shape.get_component_id());
    supply_track.set_position(position);
    supply_track_map.emplace(key, std::move(supply_track));
  }

  std::vector<SupplyTrack> supply_track_list;
  supply_track_list.reserve(supply_track_map.size());
  for (auto& [key, supply_track] : supply_track_map) {
    (void) key;
    supply_track_list.push_back(supply_track);
  }
  std::sort(supply_track_list.begin(), supply_track_list.end(), [](const SupplyTrack& first_track, const SupplyTrack& second_track) {
    return std::tuple{first_track.get_position(), first_track.get_connect_type(), first_track.get_net_name(), first_track.get_component_id()}
           < std::tuple{second_track.get_position(), second_track.get_connect_type(), second_track.get_net_name(), second_track.get_component_id()};
  });
  if (supply_track_list.empty()) {
    return {};
  }

  std::vector<SupplyPoint> supply_point_list;
  SupplyTrack& first_track = supply_track_list.front();
  supply_point_list.push_back(makeSupplyPoint(first_track));
  for (auto track_iter = supply_track_list.rbegin(); track_iter != supply_track_list.rend(); ++track_iter) {
    if (track_iter->get_connect_type() != first_track.get_connect_type()) {
      supply_point_list.push_back(makeSupplyPoint(*track_iter));
      break;
    }
  }
  std::sort(supply_point_list.begin(), supply_point_list.end(), [](const SupplyPoint& first_point, const SupplyPoint& second_point) {
    if (first_point.get_connect_type() != second_point.get_connect_type()) {
      return first_point.get_connect_type() == ConnectType::kPower;
    }
    return first_point.get_component_id() < second_point.get_component_id();
  });
  return supply_point_list;
}

bool PDNChecker::isPowerGround(const ConnectType connect_type)
{
  return connect_type == ConnectType::kPower || connect_type == ConnectType::kGround;
}

int32_t PDNChecker::getMidpoint(const int32_t first_coordinate, const int32_t second_coordinate)
{
  return static_cast<int32_t>((static_cast<int64_t>(first_coordinate) + second_coordinate) / 2);
}

SupplyPoint PDNChecker::makeSupplyPoint(const SupplyTrack& supply_track)
{
  SupplyPoint supply_point;
  supply_point.set_component_id(supply_track.get_component_id());
  supply_point.set_connect_type(supply_track.get_connect_type());
  return supply_point;
}

void PDNChecker::checkSupplyConnectivity(PCModel& pc_model, const std::map<std::string, std::string>& instance_pin_net_map,
                                         const ConnectType connect_type)
{
  std::vector<SupplyPoint>& supply_point_list = pc_model.get_supply_point_list();
  PCSummary& pc_summary = LVSDM.getDatabase().get_summary().pc_summary;
  PhysicalGraph& physical_graph = LVSDM.getDatabase().get_def_data().get_physical_graph();
  if (instance_pin_net_map.empty()) {
    return;
  }

  auto supply_point_iter = std::find_if(supply_point_list.begin(), supply_point_list.end(),
                                               [connect_type](const SupplyPoint& supply_point) {
                                                 return supply_point.get_connect_type() == connect_type;
                                               });
  if (supply_point_iter == supply_point_list.end()) {
    if (connect_type == ConnectType::kPower) {
      pc_summary.open_vdd_num = instance_pin_net_map.size();
    } else {
      pc_summary.open_vss_num = instance_pin_net_map.size();
    }
    Violation violation;
    violation.set_violation_type(connect_type == ConnectType::kPower ? ViolationType::kPowerOpenVDD : ViolationType::kPowerOpenVSS);
    violation.set_terminal_name_list(LVSUTIL.getSortedKeyNameList(instance_pin_net_map));
    for (auto& [terminal_name, net_name] : instance_pin_net_map) {
      (void) terminal_name;
      violation.get_related_net_name_list().push_back(net_name);
    }
    violation.set_related_net_name_list(LVSUTIL.getSortedUniqueList(violation.get_related_net_name_list()));
    pc_summary.violation_list.push_back(std::move(violation));
    return;
  }

  std::map<std::pair<std::string, int32_t>, std::vector<std::string>> disconnected_terminal_map;
  for (auto& [terminal_name, net_name] : instance_pin_net_map) {
    auto component_iter = physical_graph.get_terminal_component_map().find(terminal_name);
    if (component_iter != physical_graph.get_terminal_component_map().end()
        && component_iter->second == supply_point_iter->get_component_id()) {
      continue;
    }
    if (connect_type == ConnectType::kPower) {
      pc_summary.open_vdd_num++;
    } else {
      pc_summary.open_vss_num++;
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
    pc_summary.violation_list.push_back(std::move(violation));
  }
}

// private

PDNChecker* PDNChecker::_pc_instance = nullptr;

}  // namespace ilvs
