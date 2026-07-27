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
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ilvs {

struct Net
{
  std::string name;
  std::vector<std::string> terminal_list;
  uint64_t wire_segment_num = 0;
  uint64_t via_num = 0;
  uint64_t terminal_component_num = 0;
  uint64_t floating_terminal_num = 0;
};

struct Instance
{
  std::string name;
  std::vector<std::string> pin_list;
  std::string master_name;
};

struct ShapeLocation
{
  int32_t layer_id = -1;
  int32_t ll_x = 0;
  int32_t ll_y = 0;
  int32_t ur_x = 0;
  int32_t ur_y = 0;
};

struct NetRoutingGraph
{
  std::string driver_terminal_name;
  std::vector<ShapeLocation> shape_list;
  std::vector<std::pair<uint64_t, uint64_t>> via_shape_pair_list;
  std::unordered_map<std::string, std::vector<uint64_t>> terminal_shape_map;
};

struct SupplyRouteShape
{
  std::string net_name;
  uint64_t component_id = 0;
  int32_t layer_order = -1;
  ShapeLocation shape;
};

struct SupplyPoint
{
  std::string net_name;
  uint64_t component_id = 0;
  int32_t layer_id = -1;
  int32_t layer_order = -1;
  int32_t x = 0;
  int32_t y = 0;
  bool is_power = false;
};

struct PhysicalGraph
{
  uint64_t node_num = 0;
  uint64_t edge_num = 0;
  uint64_t candidate_pair_num = 0;
  uint64_t max_active_shape_num = 0;
  uint64_t component_num = 0;
  uint64_t short_component_num = 0;
  uint64_t power_port_num = 0;
  uint64_t floating_power_port_num = 0;
  uint64_t ground_port_num = 0;
  uint64_t floating_ground_port_num = 0;
  uint64_t power_pin_num = 0;
  uint64_t floating_power_pin_num = 0;
  uint64_t ground_pin_num = 0;
  uint64_t floating_ground_pin_num = 0;
  std::vector<std::string> floating_power_port_list;
  std::vector<std::string> floating_ground_port_list;
  std::vector<std::string> floating_power_pin_list;
  std::vector<std::string> floating_ground_pin_list;
  std::unordered_map<uint64_t, std::vector<std::string>> component_terminal_map;
  std::unordered_map<uint64_t, std::vector<std::string>> component_net_map;
  std::unordered_map<uint64_t, std::vector<ShapeLocation>> component_shape_map;
  std::unordered_map<std::string, uint64_t> terminal_component_map;
  std::unordered_map<std::string, NetRoutingGraph> net_routing_graph_map;
  std::unordered_set<std::string> power_net_set;
  std::unordered_set<std::string> ground_net_set;
  std::unordered_map<std::string, std::string> power_instance_pin_net_map;
  std::unordered_map<std::string, std::string> ground_instance_pin_net_map;
  std::vector<SupplyRouteShape> supply_route_shape_list;
  std::unordered_map<std::string, Instance> instance_map;
  std::vector<std::string> io_pin_list;
};

struct Violation
{
  std::string type;
  std::string net_name;
  std::vector<std::string> terminal_list;
  std::vector<uint64_t> component_id_list;
  std::vector<std::string> related_net_name_list;
  std::string instance_name;
  std::string driver_terminal_name;
  std::vector<ShapeLocation> shape_list;
};

struct LogicalGraph
{
  std::unordered_map<std::string, Instance> instance_map;
  std::vector<std::string> io_pin_list;
  uint64_t net_edge_num = 0;
};

struct Netlist
{
  std::string design_name;
  std::unordered_map<std::string, Net> net_map;
  LogicalGraph logical_graph;
  PhysicalGraph physical_graph;
};

struct CheckResult
{
  uint64_t netlist_io_num = 0;
  uint64_t def_io_num = 0;
  uint64_t netlist_power_ground_io_num = 0;
  uint64_t def_power_ground_io_num = 0;
  uint64_t missing_io_num = 0;
  uint64_t unexpected_io_num = 0;
  uint64_t netlist_instance_num = 0;
  uint64_t def_instance_num = 0;
  uint64_t missing_instance_num = 0;
  uint64_t unexpected_instance_num = 0;
  uint64_t netlist_net_num = 0;
  uint64_t def_net_num = 0;
  uint64_t missing_net_num = 0;
  uint64_t unexpected_net_num = 0;
  uint64_t net_pin_mismatch_num = 0;
  uint64_t routing_checked_net_num = 0;
  uint64_t routing_connected_net_num = 0;
  uint64_t routing_open_net_num = 0;
  uint64_t routing_open_load_pin_num = 0;
  uint64_t routing_missing_driver_num = 0;
  uint64_t routing_short_component_num = 0;
  uint64_t power_supply_point_num = 0;
  uint64_t ground_supply_point_num = 0;
  uint64_t power_instance_pin_num = 0;
  uint64_t ground_instance_pin_num = 0;
  uint64_t connected_power_instance_pin_num = 0;
  uint64_t connected_ground_instance_pin_num = 0;
  uint64_t disconnected_power_instance_pin_num = 0;
  uint64_t disconnected_ground_instance_pin_num = 0;
  std::vector<SupplyPoint> supply_point_list;
  std::vector<Violation> violation_list;
};

class Database
{
 public:
  Database() = default;
  ~Database() = default;
  // getter
#if 1  // database
  Netlist& get_netlist() { return _netlist; }
  const Netlist& get_netlist() const { return _netlist; }
  Netlist& get_def() { return _def; }
  const Netlist& get_def() const { return _def; }
  bool has_netlist() const { return _has_netlist; }
  bool has_def() const { return _has_def; }
#endif
#if 1  // check and report
  CheckResult& get_check_result() { return _check_result; }
  const CheckResult& get_check_result() const { return _check_result; }
  const std::string& get_report_directory_path() const { return _report_directory_path; }
#endif

  // setter
#if 1  // database
  void set_netlist(Netlist netlist)
  {
    _netlist = std::move(netlist);
    _has_netlist = true;
  }
  void set_def(Netlist netlist)
  {
    _def = std::move(netlist);
    _has_def = true;
  }
#endif
#if 1  // check and report
  void set_report_directory_path(std::string path) { _report_directory_path = std::move(path); }
#endif

  // function
  void reset()
  {
    _netlist = {};
    _def = {};
    _check_result = {};
    _has_netlist = false;
    _has_def = false;
  }

 private:
  Netlist _netlist;
  Netlist _def;
  CheckResult _check_result;
  bool _has_netlist = false;
  bool _has_def = false;
  std::string _report_directory_path = ".";
};

}  // namespace ilvs
