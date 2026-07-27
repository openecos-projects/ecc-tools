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
#include "RoutingChecker.hpp"

#include "DisjointSet.hpp"
#include "LVSHeader.hpp"
#include "Logger.hpp"
#include "Utility.hpp"

namespace ilvs {

// public

void RoutingChecker::initInst()
{
  if (_rc_instance == nullptr) {
    _rc_instance = new RoutingChecker();
  }
}

RoutingChecker& RoutingChecker::getInst()
{
  if (_rc_instance == nullptr) {
    LVSLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_rc_instance;
}

void RoutingChecker::destroyInst()
{
  if (_rc_instance != nullptr) {
    delete _rc_instance;
    _rc_instance = nullptr;
  }
}

// function

void RoutingChecker::check()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  Database& database = LVSDM.getDatabase();
  database.get_summary().rc_summary.reset();

  RCModel rc_model = initRCModel();
  checkRouting(rc_model);
  checkShort(rc_model);
  updateSummary(rc_model);

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

RCModel RoutingChecker::initRCModel()
{
  std::map<std::string, Net>& net_map = LVSDM.getDatabase().get_def_data().get_net_map();

  RCModel rc_model;
  std::vector<std::string>& net_name_list = rc_model.get_net_name_list();
  net_name_list.reserve(net_map.size());
  for (auto& [net_name, net] : net_map) {
    (void) net;
    net_name_list.push_back(net_name);
  }
  return rc_model;
}

void RoutingChecker::checkRouting(RCModel& rc_model)
{
  DefData& def_data = LVSDM.getDatabase().get_def_data();
  std::map<std::string, Net>& net_map = def_data.get_net_map();
  std::map<std::string, NetRoutingGraph>& net_routing_graph_map = def_data.get_physical_graph().get_net_routing_graph_map();
  std::vector<std::string>& net_name_list = rc_model.get_net_name_list();
  std::vector<RoutingCheck>& routing_check_list = rc_model.get_routing_check_list();
  routing_check_list.resize(net_name_list.size());

#pragma omp parallel for schedule(dynamic)
  for (int32_t net_idx = 0; net_idx < static_cast<int32_t>(net_name_list.size()); net_idx++) {
    std::string& net_name = net_name_list[net_idx];
    auto routing_graph_iter = net_routing_graph_map.find(net_name);
    NetRoutingGraph* routing_graph = routing_graph_iter == net_routing_graph_map.end() ? nullptr : &routing_graph_iter->second;
    routing_check_list[net_idx] = checkNetRoutingConnectivity(net_name, net_map.at(net_name), routing_graph);
  }
}

RoutingCheck RoutingChecker::checkNetRoutingConnectivity(const std::string& net_name, const Net& net,
                                                         const NetRoutingGraph* routing_graph)
{
  RoutingCheck routing_check;
  routing_check.set_net_name(net_name);
  const std::vector<std::string>& terminal_name_list = net.get_terminal_name_list();
  if (terminal_name_list.size() <= 1) {
    return routing_check;
  }
  if (routing_graph == nullptr) {
    routing_check.set_disconnected_terminal_name_list(terminal_name_list);
    return routing_check;
  }

  routing_check.set_driver_terminal_name(routing_graph->get_driver_terminal_name());
  const std::vector<RoutingShape>& routing_shape_list = routing_graph->get_routing_shape_list();
  DisjointSet graph(static_cast<int32_t>(routing_shape_list.size()));
  std::map<int32_t, std::vector<int32_t>> layer_shape_idx_map;
  for (int32_t shape_idx = 0; shape_idx < static_cast<int32_t>(routing_shape_list.size()); shape_idx++) {
    layer_shape_idx_map[routing_shape_list[shape_idx].get_shape().get_layer_idx()].push_back(shape_idx);
  }
  for (auto& [layer_idx, shape_idx_list] : layer_shape_idx_map) {
    (void) layer_idx;
    std::sort(shape_idx_list.begin(), shape_idx_list.end(), [&routing_shape_list](const int32_t first_shape_idx, const int32_t second_shape_idx) {
      const Shape& first_shape = routing_shape_list[first_shape_idx].get_shape();
      const Shape& second_shape = routing_shape_list[second_shape_idx].get_shape();
      if (first_shape.get_ll_x() != second_shape.get_ll_x()) {
        return first_shape.get_ll_x() < second_shape.get_ll_x();
      }
      if (first_shape.get_ll_y() != second_shape.get_ll_y()) {
        return first_shape.get_ll_y() < second_shape.get_ll_y();
      }
      return first_shape_idx < second_shape_idx;
    });
    std::vector<int32_t> active_shape_idx_list;
    for (int32_t shape_idx : shape_idx_list) {
      const Shape& shape = routing_shape_list[shape_idx].get_shape();
      active_shape_idx_list.erase(
          std::remove_if(active_shape_idx_list.begin(), active_shape_idx_list.end(), [&routing_shape_list, &shape](const int32_t active_shape_idx) {
            return routing_shape_list[active_shape_idx].get_shape().get_ur_x() < shape.get_ll_x();
          }),
          active_shape_idx_list.end());
      for (int32_t active_shape_idx : active_shape_idx_list) {
        if (isIntersected(routing_shape_list[active_shape_idx].get_shape(), shape)) {
          graph.unite(active_shape_idx, shape_idx);
        }
      }
      active_shape_idx_list.push_back(shape_idx);
    }
  }
  for (auto& [bottom_shape_idx, top_shape_idx] : routing_graph->get_via_shape_idx_pair_list()) {
    if (bottom_shape_idx >= 0 && top_shape_idx >= 0 && bottom_shape_idx < static_cast<int32_t>(routing_shape_list.size())
        && top_shape_idx < static_cast<int32_t>(routing_shape_list.size())) {
      graph.unite(bottom_shape_idx, top_shape_idx);
    }
  }
  for (auto& [terminal_name, shape_idx_list] : routing_graph->get_terminal_shape_idx_map()) {
    (void) terminal_name;
    int32_t first_shape_idx = -1;
    for (int32_t shape_idx : shape_idx_list) {
      if (shape_idx < 0 || shape_idx >= static_cast<int32_t>(routing_shape_list.size())) {
        continue;
      }
      if (first_shape_idx == -1) {
        first_shape_idx = shape_idx;
      } else {
        graph.unite(first_shape_idx, shape_idx);
      }
    }
  }

  int32_t driver_root = getTerminalRoot(*routing_graph, routing_check.get_driver_terminal_name(), graph);
  if (routing_check.get_driver_terminal_name().empty() || driver_root == -1) {
    routing_check.set_disconnected_terminal_name_list(terminal_name_list);
    for (const RoutingShape& routing_shape : routing_shape_list) {
      routing_check.get_disconnected_shape_list().push_back(routing_shape.get_shape());
    }
    return routing_check;
  }

  std::unordered_set<int32_t> disconnected_root_set;
  for (const std::string& terminal_name : terminal_name_list) {
    if (terminal_name == routing_check.get_driver_terminal_name()) {
      continue;
    }
    int32_t terminal_root = getTerminalRoot(*routing_graph, terminal_name, graph);
    if (terminal_root == -1 || terminal_root != driver_root) {
      routing_check.get_disconnected_terminal_name_list().push_back(terminal_name);
      if (terminal_root != -1) {
        disconnected_root_set.insert(terminal_root);
      }
    }
  }
  if (routing_check.get_disconnected_terminal_name_list().empty()) {
    return routing_check;
  }
  for (int32_t shape_idx = 0; shape_idx < static_cast<int32_t>(routing_shape_list.size()); shape_idx++) {
    if (LVSUTIL.exist(disconnected_root_set, graph.find(shape_idx))) {
      routing_check.get_disconnected_shape_list().push_back(routing_shape_list[shape_idx].get_shape());
    }
  }
  return routing_check;
}

bool RoutingChecker::isIntersected(const Shape& first_shape, const Shape& second_shape)
{
  return first_shape.get_ll_x() <= second_shape.get_ur_x() && second_shape.get_ll_x() <= first_shape.get_ur_x()
         && first_shape.get_ll_y() <= second_shape.get_ur_y() && second_shape.get_ll_y() <= first_shape.get_ur_y();
}

int32_t RoutingChecker::getTerminalRoot(const NetRoutingGraph& routing_graph, const std::string& terminal_name, DisjointSet& graph)
{
  auto terminal_iter = routing_graph.get_terminal_shape_idx_map().find(terminal_name);
  if (terminal_iter == routing_graph.get_terminal_shape_idx_map().end()) {
    return -1;
  }
  const std::vector<RoutingShape>& routing_shape_list = routing_graph.get_routing_shape_list();
  for (int32_t shape_idx : terminal_iter->second) {
    if (shape_idx >= 0 && shape_idx < static_cast<int32_t>(routing_shape_list.size())) {
      return graph.find(shape_idx);
    }
  }
  return -1;
}

void RoutingChecker::checkShort(RCModel& rc_model)
{
  std::map<int32_t, std::vector<std::string>>& component_net_name_map =
      LVSDM.getDatabase().get_def_data().get_physical_graph().get_component_net_name_map();
  std::vector<int32_t>& short_component_id_list = rc_model.get_short_component_id_list();
  for (auto& [component_id, net_name_list] : component_net_name_map) {
    if (LVSUTIL.getSortedUniqueList(net_name_list).size() > 1) {
      short_component_id_list.push_back(component_id);
    }
  }
}

void RoutingChecker::updateSummary(RCModel& rc_model)
{
  RCSummary& rc_summary = LVSDM.getDatabase().get_summary().rc_summary;
  PhysicalGraph& physical_graph = LVSDM.getDatabase().get_def_data().get_physical_graph();
  std::vector<RoutingCheck>& routing_check_list = rc_model.get_routing_check_list();

  for (RoutingCheck& routing_check : routing_check_list) {
    if (routing_check.get_disconnected_terminal_name_list().empty()) {
      continue;
    }
    rc_summary.open_net_num++;
    Violation violation;
    violation.set_violation_type(ViolationType::kRoutingOpen);
    violation.set_net_name(routing_check.get_net_name());
    violation.set_driver_terminal_name(routing_check.get_driver_terminal_name());
    violation.set_terminal_name_list(routing_check.get_disconnected_terminal_name_list());
    violation.set_shape_list(routing_check.get_disconnected_shape_list());
    rc_summary.violation_list.push_back(std::move(violation));
  }
  for (int32_t component_id : rc_model.get_short_component_id_list()) {
    auto component_iter = physical_graph.get_component_net_name_map().find(component_id);
    if (component_iter == physical_graph.get_component_net_name_map().end()) {
      continue;
    }
    Violation violation;
    violation.set_violation_type(ViolationType::kRoutingShort);
    violation.get_component_id_list().push_back(component_id);
    violation.set_related_net_name_list(LVSUTIL.getSortedUniqueList(component_iter->second));
    rc_summary.short_net_num++;
    rc_summary.violation_list.push_back(std::move(violation));
  }
}

// private

RoutingChecker* RoutingChecker::_rc_instance = nullptr;

}  // namespace ilvs
