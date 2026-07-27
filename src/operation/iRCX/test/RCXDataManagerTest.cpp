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
#include <cassert>
#include <cstdlib>

#include "Database.hpp"

int main()
{
  ircx::LayerTable layer_table;
  layer_table.register_design_layer(1, "M1");
  layer_table.register_process_layer(10, "metal1");
  layer_table.register_mapping("M1", "metal1");
  assert(layer_table.get_design_idx("M1") == 1);
  assert(layer_table.get_process_idx_by_design_idx(1) == 10);

  ircx::Pin pin;
  pin.set_pin_name("u0:A");
  assert(!pin.get_is_port());
  assert(pin.get_instance_name() == "u0");
  assert(pin.get_instance_pin_name() == "A");

  std::vector<ircx::TopoNode> node_list;
  node_list.emplace_back(0);
  node_list.emplace_back(0);
  node_list[0].set_layer_idx(1);
  node_list[1].set_layer_idx(1);
  node_list[0].set_point(GTLPointInt(0, 5));
  node_list[1].set_point(GTLPointInt(100, 5));

  std::vector<ircx::TopoEdge> edge_list;
  edge_list.emplace_back(0);
  edge_list[0].set_start_node_idx(0);
  edge_list[0].set_end_node_idx(1);
  edge_list[0].set_layer_idx(1);
  edge_list[0].set_shape(GTLRectInt(0, 0, 100, 10));

  ircx::TopoPool topo_pool;
  topo_pool.reserve(1, node_list.size(), edge_list.size());
  topo_pool.add_net(std::move(node_list), std::move(edge_list));
  assert(topo_pool.get_net_node_list(0).size() == 2);
  assert(topo_pool.get_net_edge_list(0).size() == 1);
  assert(topo_pool.get_net_edge_list(0)[0].get_line_segment().get_is_horizontal());
  assert(!topo_pool.get_net_edge_list(0)[0].get_is_special_net());

  std::vector<ircx::TopoEdge> special_edge_list(1);
  topo_pool.add_special_edge_list(std::move(special_edge_list));
  assert(topo_pool.get_special_edge_pool()[0].get_is_special_net());

  ircx::RCData rc_table;
  rc_table.init(2, 1, topo_pool);
  ircx::CornerNetIdx corner_net_idx(1, 0);
  std::vector<double>& resistance_list = rc_table.get_corner_net_resistance_list(corner_net_idx);
  std::vector<double>& ground_capacitance_list = rc_table.get_corner_net_ground_capacitance_list(corner_net_idx);
  assert(resistance_list.size() == 1);
  assert(ground_capacitance_list.size() == 1);
  resistance_list[0] = 1.25;
  ground_capacitance_list[0] = 0.75;
  rc_table.append_net_coupling_cap_entry(0, 0, 0, 1, 0.5);
  rc_table.merge_net_coupling_cap_entry_list();
  ircx::CouplingKey coupling_key(0, 0);
  assert(rc_table.get_merged_coupling_capacitance_map()[coupling_key][1] == 0.5);

  ircx::NetEnv net_env;
  std::vector<ircx::EdgeEnvInterval> env_interval_list(1);
  net_env.append_edge_interval_list(std::move(env_interval_list));
  assert(net_env.get_edge_interval_list(0).size() == 1);

  ircx::NetEtchProfile net_etch_profile;
  std::vector<ircx::EdgeEtchInterval> etch_interval_list(1);
  net_etch_profile.append_edge_interval_list(std::move(etch_interval_list));
  assert(net_etch_profile.get_edge_interval_list(0).size() == 1);

  ircx::Database database;
  database.set_design_name("rcx_test");
  database.set_layer_table(layer_table);
  database.set_topo_pool(topo_pool);
  database.set_rc_data(rc_table);
  assert(database.get_design_name() == "rcx_test");
  assert(database.get_topo_pool().get_net_edge_list(0).size() == 1);
  assert(database.get_rc_data().get_corner_num() == 2);

  return EXIT_SUCCESS;
}
