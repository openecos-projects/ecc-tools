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
  database.set_netlist_data(std::move(netlist_data));

  ilvs::DefData def_data;
  def_data.set_design_name("def_top");
  def_data.get_io_terminal_name_list() = {"PIN/B", "PIN/A"};
  def_data.get_def_routing_data().get_net_routing_data_map()["n1"];
  database.set_def_data(std::move(def_data));

  database.get_netlist_data().normalize();
  assert(database.get_netlist_data().get_io_terminal_name_list() == std::vector<std::string>({"PIN/A", "PIN/B"}));
  assert(database.get_netlist_data().get_net_map().at("n1").get_terminal_name_list()
         == std::vector<std::string>({"u1/A", "u2/Z"}));

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
