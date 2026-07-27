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
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

#include "LVSChecker.hpp"

int main()
{
  ilvs::LVSChecker::initInst();

  ilvs::Netlist netlist;
  netlist.net_map["n1"] = {"n1", {"u1/A", "u2/Z"}};
  netlist.net_map["n2"] = {"n2", {"u3/A", "u4/Z"}};
  netlist.net_map["n_missing"] = {"n_missing", {"u_missing/A"}};
  netlist.logical_graph.io_pin_list = {"PIN/clk", "PIN/VDD", "PIN/missing"};
  netlist.logical_graph.instance_map["u1"] = {"u1", {}, "INV_X1"};
  netlist.logical_graph.instance_map["u_missing"] = {"u_missing", {}, "AOI21_X1"};
  netlist.logical_graph.instance_map["u_master"] = {"u_master", {}, "NAND2_X1"};

  ilvs::Netlist def;
  def.net_map = netlist.net_map;
  def.net_map.erase("n_missing");
  def.net_map["n2"].terminal_list = {"u3/A", "u4/Y"};
  def.net_map["n_extra"] = {"n_extra", {"u5/A", "u5/B"}};
  def.physical_graph.io_pin_list = {"PIN/clk", "PIN/VDD", "PIN/extra"};
  def.physical_graph.instance_map["u1"] = {"u1", {}, "INV_X1"};
  def.physical_graph.instance_map["u_extra"] = {"u_extra", {}, "BUF_X1"};
  def.physical_graph.instance_map["u_master"] = {"u_master", {}, "NOR2_X1"};
  def.physical_graph.power_net_set.insert("VDD");
  def.physical_graph.ground_net_set.insert("VSS");
  def.physical_graph.component_net_map[7] = {"n2", "n1"};
  def.physical_graph.power_instance_pin_net_map = {{"u_power_ok/VDD", "VDD"}, {"u_power_open/VDD", "VDD"}};
  def.physical_graph.ground_instance_pin_net_map = {{"u_ground_ok/VSS", "VSS"}, {"u_ground_open/VSS", "VSS"}};
  def.physical_graph.terminal_component_map = {{"u_power_ok/VDD", 20}, {"u_power_open/VDD", 21}, {"u_ground_ok/VSS", 30}};
  def.physical_graph.supply_route_shape_list = {{"VDD", 20, 5, {5, 0, 0, 100, 10}},
                                                 {"VSS", 30, 5, {5, 0, 50, 100, 60}},
                                                 {"VDD", 20, 5, {5, 0, 100, 100, 110}}};

  ilvs::NetRoutingGraph& n1_routing_graph = def.physical_graph.net_routing_graph_map["n1"];
  n1_routing_graph.driver_terminal_name = "u1/A";
  n1_routing_graph.shape_list = {{0, 0, 0, 1, 1}, {1, 10, 0, 11, 1}, {1, 11, 0, 20, 1},
                                  {1, 20, 0, 21, 1}, {2, 20, 0, 21, 1}, {2, 21, 0, 22, 1},
                                  {1, 0, 10, 11, 11}};
  n1_routing_graph.via_shape_pair_list = {{3, 4}};
  n1_routing_graph.terminal_shape_map = {{"u1/A", {0, 1}}, {"u2/Z", {5}}};

  ilvs::NetRoutingGraph& n2_routing_graph = def.physical_graph.net_routing_graph_map["n2"];
  n2_routing_graph.driver_terminal_name = "u3/A";
  n2_routing_graph.shape_list = {{1, 0, 10, 1, 11}, {1, 10, 10, 11, 11}};
  n2_routing_graph.terminal_shape_map = {{"u3/A", {0}}, {"u4/Y", {1}}};

  ilvs::NetRoutingGraph& n_extra_routing_graph = def.physical_graph.net_routing_graph_map["n_extra"];
  n_extra_routing_graph.shape_list = {{1, 0, 20, 1, 21}, {1, 2, 20, 3, 21}};
  n_extra_routing_graph.terminal_shape_map = {{"u5/A", {0}}, {"u5/B", {1}}};

  ilvs::CheckResult result = LVSLC.check(netlist, def);
  assert(result.netlist_io_num == 2);
  assert(result.def_io_num == 2);
  assert(result.netlist_power_ground_io_num == 1);
  assert(result.def_power_ground_io_num == 1);
  assert(result.missing_io_num == 1);
  assert(result.unexpected_io_num == 1);
  assert(result.netlist_instance_num == 3);
  assert(result.def_instance_num == 3);
  assert(result.missing_instance_num == 1);
  assert(result.unexpected_instance_num == 1);
  assert(result.netlist_net_num == 3);
  assert(result.def_net_num == 3);
  assert(result.missing_net_num == 1);
  assert(result.unexpected_net_num == 1);
  assert(result.net_pin_mismatch_num == 1);
  assert(result.routing_checked_net_num == 3);
  assert(result.routing_connected_net_num == 1);
  assert(result.routing_open_net_num == 1);
  assert(result.routing_open_load_pin_num == 1);
  assert(result.routing_missing_driver_num == 1);
  assert(result.routing_short_component_num == 1);
  assert(result.power_supply_point_num == 1);
  assert(result.ground_supply_point_num == 1);
  assert(result.power_instance_pin_num == 2);
  assert(result.ground_instance_pin_num == 2);
  assert(result.connected_power_instance_pin_num == 1);
  assert(result.connected_ground_instance_pin_num == 1);
  assert(result.disconnected_power_instance_pin_num == 1);
  assert(result.disconnected_ground_instance_pin_num == 1);
  assert(std::any_of(result.supply_point_list.begin(), result.supply_point_list.end(), [](const ilvs::SupplyPoint& point) {
    return point.is_power && point.net_name == "VDD" && point.component_id == 20 && point.x == 50 && point.y == 5;
  }));
  assert(std::any_of(result.supply_point_list.begin(), result.supply_point_list.end(), [](const ilvs::SupplyPoint& point) {
    return !point.is_power && point.net_name == "VSS" && point.component_id == 30 && point.x == 50 && point.y == 55;
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "MissingNet" && violation.net_name == "n_missing";
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "UnexpectedNet" && violation.net_name == "n_extra";
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "NetPinMismatch" && violation.net_name == "n2"
           && violation.terminal_list == std::vector<std::string>{"NETLIST/u4/Z", "DEF/u4/Y"};
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "RoutingOpen" && violation.net_name == "n2" && violation.driver_terminal_name == "u3/A"
           && violation.terminal_list == std::vector<std::string>{"u4/Y"} && violation.shape_list.size() == 1
           && violation.shape_list.front().ll_x == 10;
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "RoutingDriverMissing" && violation.net_name == "n_extra"
           && violation.terminal_list == std::vector<std::string>{"u5/A", "u5/B"};
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "RoutingShort" && violation.component_id_list == std::vector<uint64_t>{7}
           && violation.related_net_name_list == std::vector<std::string>{"n1", "n2"};
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "PowerDisconnected" && violation.net_name == "VDD" && violation.component_id_list == std::vector<uint64_t>{21}
           && violation.terminal_list == std::vector<std::string>{"u_power_open/VDD"};
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "GroundDisconnected" && violation.net_name == "VSS" && violation.component_id_list.empty()
           && violation.terminal_list == std::vector<std::string>{"u_ground_open/VSS"};
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "MissingIO" && violation.terminal_list == std::vector<std::string>{"PIN/missing"};
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "UnexpectedIO" && violation.terminal_list == std::vector<std::string>{"PIN/extra"};
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "MissingInstance" && violation.instance_name == "u_missing";
  }));
  assert(std::any_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "UnexpectedInstance" && violation.instance_name == "u_extra";
  }));
  assert(std::none_of(result.violation_list.begin(), result.violation_list.end(), [](const ilvs::Violation& violation) {
    return violation.type == "InstanceMasterMismatch" || violation.type == "Open" || violation.type == "Short" || violation.type == "Unrouted";
  }));

  ilvs::Netlist vertical_def;
  vertical_def.physical_graph.power_net_set.insert("VDD");
  vertical_def.physical_graph.ground_net_set.insert("VSS");
  vertical_def.physical_graph.supply_route_shape_list = {{"VDD", 40, 6, {6, 0, 0, 10, 100}},
                                                               {"VSS", 50, 6, {6, 50, 0, 60, 100}},
                                                               {"VDD", 40, 6, {6, 100, 0, 110, 100}}};
  const ilvs::CheckResult vertical_result = LVSLC.check({}, vertical_def);
  assert(vertical_result.power_supply_point_num == 1);
  assert(vertical_result.ground_supply_point_num == 1);
  assert(std::any_of(vertical_result.supply_point_list.begin(), vertical_result.supply_point_list.end(),
                     [](const ilvs::SupplyPoint& point) {
                       return point.is_power && point.component_id == 40 && point.x == 5 && point.y == 50;
                     }));
  assert(std::any_of(vertical_result.supply_point_list.begin(), vertical_result.supply_point_list.end(),
                     [](const ilvs::SupplyPoint& point) {
                       return !point.is_power && point.component_id == 50 && point.x == 55 && point.y == 50;
                     }));

  ilvs::Netlist missing_supply_def;
  missing_supply_def.physical_graph.power_net_set.insert("VDD");
  missing_supply_def.physical_graph.ground_net_set.insert("VSS");
  missing_supply_def.physical_graph.ground_instance_pin_net_map = {{"u_missing/VSS", "VSS"}};
  missing_supply_def.physical_graph.supply_route_shape_list = {{"VDD", 60, 7, {7, 0, 0, 100, 10}}};
  const ilvs::CheckResult missing_supply_result = LVSLC.check({}, missing_supply_def);
  assert(missing_supply_result.ground_supply_point_num == 0);
  assert(missing_supply_result.ground_instance_pin_num == 1);
  assert(missing_supply_result.disconnected_ground_instance_pin_num == 1);
  assert(std::any_of(missing_supply_result.violation_list.begin(), missing_supply_result.violation_list.end(),
                     [](const ilvs::Violation& violation) {
                       return violation.type == "GroundSupplyPointMissing" && violation.terminal_list == std::vector<std::string>{"u_missing/VSS"};
                     }));
  ilvs::LVSChecker::destroyInst();
  return 0;
}
