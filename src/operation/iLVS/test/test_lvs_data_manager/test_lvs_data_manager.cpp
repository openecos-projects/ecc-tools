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
#include "Config.hpp"
#include "Database.hpp"
#include "LVSHeader.hpp"

ilvs::RoutingShape makeRoutingShape()
{
  ilvs::Shape shape;
  shape.set_layer_idx(3);
  shape.set_ll_x(10);
  shape.set_ll_y(20);
  shape.set_ur_x(30);
  shape.set_ur_y(40);
  ilvs::RoutingShape routing_shape;
  routing_shape.set_shape(shape);
  return routing_shape;
}

int main()
{
  ilvs::Config config;
  assert(config.thread_number == 0);

  ilvs::Database database;

  ilvs::NetlistData netlist_data;
  netlist_data.set_design_name("netlist_top");
  netlist_data.get_io_terminal_name_list() = {"PIN/B", "PIN/A", "PIN/A"};
  ilvs::Net net;
  net.set_terminal_name_list({"u2/Z", "u1/A", "u1/A"});
  netlist_data.get_net_map()["n1"] = net;
  const ilvs::Net* net_before_move = &netlist_data.get_net_map().at("n1");
  database.set_netlist_data(std::move(netlist_data));
  assert(&database.get_netlist_data().get_net_map().at("n1") == net_before_move);

  ilvs::DefData def_data;
  def_data.set_design_name("def_top");
  def_data.get_io_terminal_name_list() = {"PIN/B", "PIN/A"};
  def_data.get_def_routing_data().get_net_routing_data_map()["n1"];
  const ilvs::NetRoutingData* routing_before_move = &def_data.get_def_routing_data().get_net_routing_data_map().at("n1");
  database.set_def_data(std::move(def_data));
  assert(&database.get_def_data().get_def_routing_data().get_net_routing_data_map().at("n1") == routing_before_move);

  database.get_netlist_data().normalize();
  assert(database.get_netlist_data().get_io_terminal_name_list() == std::vector<std::string>({"PIN/A", "PIN/B"}));
  assert(database.get_netlist_data().get_net_map().at("n1").get_terminal_name_list() == std::vector<std::string>({"u1/A", "u2/Z"}));

  ilvs::PhysicalGraph& physical_graph = database.get_def_data().get_physical_graph();
  int32_t first_net_id = physical_graph.getOrCreateNetId("n1");
  int32_t second_net_id = physical_graph.getOrCreateNetId("n2");
  assert(first_net_id == physical_graph.getOrCreateNetId("n1"));
  physical_graph.get_net_routing_graph_map()["n1"].get_routing_shape_list().push_back(makeRoutingShape());
  physical_graph.get_component_net_id_list() = {{first_net_id, second_net_id}};
  physical_graph.get_component_shape_ref_list() = {{{first_net_id, 0}}};
  physical_graph.set_optimized_component_data_valid(true);
  assert(physical_graph.get_component_net_name_list(0) == std::vector<std::string>({"n1", "n2"}));
  assert(physical_graph.get_component_shape_map().empty());
  std::vector<ilvs::Shape> component_shape_list = physical_graph.get_component_shape_list(0);
  assert(component_shape_list.size() == 1);
  assert(component_shape_list.front().get_layer_idx() == 3);

  ilvs::Summary& summary = database.get_summary();
  summary.ec_summary.netlist_io_num = 1;
  summary.rc_summary.open_net_num = 1;
  summary.pc_summary.open_vdd_num = 1;

  database.reset();
  assert(database.get_summary().ec_summary.netlist_io_num == 0);
  assert(database.get_summary().rc_summary.open_net_num == 0);
  assert(database.get_summary().pc_summary.open_vdd_num == 0);
  assert(database.get_netlist_data().get_io_terminal_name_list().empty());
  assert(database.get_def_data().get_def_routing_data().get_net_routing_data_map().empty());
  assert(database.get_def_data().get_physical_graph().get_net_routing_graph_map().empty());
  return 0;
}
