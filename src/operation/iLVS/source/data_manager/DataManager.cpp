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
#include "DataManager.hpp"

#include "DisjointSet.hpp"
#include "LVSInterface.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PhysicalGraphBuildData.hpp"
#include "Utility.hpp"

namespace ilvs {

// public

void DataManager::initInst()
{
  if (_dm_instance == nullptr) {
    _dm_instance = new DataManager();
  }
}

DataManager& DataManager::getInst()
{
  if (_dm_instance == nullptr) {
    LVSLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_dm_instance;
}

void DataManager::destroyInst()
{
  if (_dm_instance != nullptr) {
    delete _dm_instance;
    _dm_instance = nullptr;
  }
}

// function

void DataManager::input(std::map<std::string, std::any>& config_map)
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  LVSI.input(config_map);
  buildConfig();
  buildDatabase();
  printConfig();
  printDatabase();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  LVSI.output();
  destroyDatabase();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // 构建

void DataManager::buildConfig()
{
  /////////////////////////////////////////////
  // **********       LVS        ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "lvs.log";
  // **********   DataManager    ********** //
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  // **********  EntityChecker   ********** //
  _config.ec_temp_directory_path = _config.temp_directory_path + "entity_checker/";
  // **********  RoutingChecker  ********** //
  _config.rc_temp_directory_path = _config.temp_directory_path + "routing_checker/";
  // **********    PDNChecker    ********** //
  _config.pc_temp_directory_path = _config.temp_directory_path + "pdn_checker/";
  // **********   LVSReporter    ********** //
  _config.lr_temp_directory_path = _config.temp_directory_path + "lvs_reporter/";
  /////////////////////////////////////////////
  // **********       LVS        ********** //
  LVSUTIL.removeDir(_config.temp_directory_path);
  LVSUTIL.createDir(_config.temp_directory_path);
  LVSUTIL.createDirByFile(_config.log_file_path);
  // **********   DataManager    ********** //
  LVSUTIL.createDir(_config.dm_temp_directory_path);
  // **********  EntityChecker   ********** //
  LVSUTIL.createDir(_config.ec_temp_directory_path);
  // **********  RoutingChecker  ********** //
  LVSUTIL.createDir(_config.rc_temp_directory_path);
  // **********    PDNChecker    ********** //
  LVSUTIL.createDir(_config.pc_temp_directory_path);
  // **********   LVSReporter    ********** //
  LVSUTIL.createDir(_config.lr_temp_directory_path);
  /////////////////////////////////////////////
  LVSLOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  buildNetlistData();
  buildDefData();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::buildNetlistData()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  _database.get_netlist_data().normalize();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::buildDefData()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  _database.get_def_data().normalize();
  buildNetRoutingGraph();
  buildPhysicalGraph();

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::buildNetRoutingGraph()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  DefRoutingData& def_routing_data = _database.get_def_data().get_def_routing_data();
  PhysicalGraph& physical_graph = _database.get_def_data().get_physical_graph();
  physical_graph.reset();

  physical_graph.get_power_net_name_set() = def_routing_data.get_power_net_name_set();
  physical_graph.get_ground_net_name_set() = def_routing_data.get_ground_net_name_set();
  physical_graph.get_power_instance_pin_net_map() = def_routing_data.get_power_instance_pin_net_map();
  physical_graph.get_ground_instance_pin_net_map() = def_routing_data.get_ground_instance_pin_net_map();
  for (auto& [net_name, net_routing_data] : def_routing_data.get_net_routing_data_map()) {
    NetRoutingGraph& net_routing_graph = physical_graph.get_net_routing_graph_map()[net_name];
    buildNetRoutingGraph(net_routing_data, net_routing_graph);
  }

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::buildNetRoutingGraph(const NetRoutingData& net_routing_data, NetRoutingGraph& net_routing_graph)
{
  net_routing_graph.set_driver_terminal_name(net_routing_data.get_driver_terminal_name());
  for (auto& [terminal_name, routing_shape_list] : net_routing_data.get_terminal_routing_shape_map()) {
    std::vector<int32_t>& shape_idx_list = net_routing_graph.get_terminal_shape_idx_map()[terminal_name];
    for (const RoutingShape& routing_shape : routing_shape_list) {
      shape_idx_list.push_back(buildRoutingGraphShape(net_routing_graph, routing_shape));
    }
  }
  net_routing_graph.set_terminal_routing_shape_num(static_cast<int32_t>(net_routing_graph.get_routing_shape_list().size()));
  for (const RoutingShape& routing_shape : net_routing_data.get_wire_routing_shape_list()) {
    buildRoutingGraphShape(net_routing_graph, routing_shape);
  }
  for (const RoutingVia& routing_via : net_routing_data.get_routing_via_list()) {
    int32_t bottom_shape_idx = buildRoutingGraphShape(net_routing_graph, routing_via.get_bottom_routing_shape());
    int32_t top_shape_idx = buildRoutingGraphShape(net_routing_graph, routing_via.get_top_routing_shape());
    net_routing_graph.get_via_shape_idx_pair_list().emplace_back(bottom_shape_idx, top_shape_idx);
  }
}

int32_t DataManager::buildRoutingGraphShape(NetRoutingGraph& net_routing_graph, const RoutingShape& routing_shape)
{
  net_routing_graph.get_routing_shape_list().push_back(routing_shape);
  return static_cast<int32_t>(net_routing_graph.get_routing_shape_list().size()) - 1;
}

void DataManager::buildPhysicalGraph()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  PhysicalGraph& physical_graph = _database.get_def_data().get_physical_graph();
  physical_graph.resetDerivedData();

  PhysicalGraphBuildData physical_graph_build_data;
  std::set<std::string>& power_net_name_set = physical_graph.get_power_net_name_set();
  std::set<std::string>& ground_net_name_set = physical_graph.get_ground_net_name_set();
  for (auto& [net_name, routing_graph] : physical_graph.get_net_routing_graph_map()) {
    bool build_terminal_shape = LVSUTIL.exist(power_net_name_set, net_name) || LVSUTIL.exist(ground_net_name_set, net_name);
    if (!build_terminal_shape && routing_graph.get_terminal_routing_shape_num() >= static_cast<int32_t>(routing_graph.get_routing_shape_list().size())) {
      continue;
    }
    buildPhysicalGraphNode(physical_graph_build_data, net_name, routing_graph, build_terminal_shape);
  }
  buildPhysicalGraphComponent(physical_graph_build_data);

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::buildPhysicalGraphNode(PhysicalGraphBuildData& physical_graph_build_data, const std::string& net_name, const NetRoutingGraph& routing_graph,
                                         bool build_terminal_shape)
{
  PhysicalGraph& physical_graph = _database.get_def_data().get_physical_graph();
  const std::vector<RoutingShape>& routing_shape_list = routing_graph.get_routing_shape_list();
  int32_t terminal_routing_shape_num = routing_graph.get_terminal_routing_shape_num();
  std::vector<int32_t> graph_node_idx_list(routing_shape_list.size(), -1);
  physical_graph.get_net_routing_shape_component_id_list_map()[net_name].assign(routing_shape_list.size(), -1);
  for (int32_t routing_shape_idx = 0; routing_shape_idx < static_cast<int32_t>(routing_shape_list.size()); routing_shape_idx++) {
    bool is_terminal = routing_shape_idx < terminal_routing_shape_num;
    if (!build_terminal_shape && is_terminal) {
      continue;
    }
    const RoutingShape& routing_shape = routing_shape_list[routing_shape_idx];
    PhysicalGraphBuildNode graph_node;
    graph_node.set_net_name(net_name);
    graph_node.set_shape(routing_shape.get_shape());
    graph_node.set_is_terminal(is_terminal);
    graph_node.set_routing_shape_idx(routing_shape_idx);
    graph_node.set_layer_order(routing_shape.get_layer_order());
    physical_graph_build_data.get_graph_node_list().push_back(std::move(graph_node));
    graph_node_idx_list[routing_shape_idx] = static_cast<int32_t>(physical_graph_build_data.get_graph_node_list().size()) - 1;
  }
  for (auto& [bottom_shape_idx, top_shape_idx] : routing_graph.get_via_shape_idx_pair_list()) {
    if (bottom_shape_idx < 0 || top_shape_idx < 0 || bottom_shape_idx >= static_cast<int32_t>(graph_node_idx_list.size())
        || top_shape_idx >= static_cast<int32_t>(graph_node_idx_list.size())) {
      continue;
    }
    int32_t bottom_node_idx = graph_node_idx_list[bottom_shape_idx];
    int32_t top_node_idx = graph_node_idx_list[top_shape_idx];
    if (bottom_node_idx < 0 || top_node_idx < 0) {
      continue;
    }
    physical_graph_build_data.get_via_node_pair_list().emplace_back(bottom_node_idx, top_node_idx);
  }
  for (auto& [terminal_name, shape_idx_list] : routing_graph.get_terminal_shape_idx_map()) {
    if (!build_terminal_shape) {
      continue;
    }
    std::vector<int32_t> terminal_node_idx_list;
    for (int32_t shape_idx : shape_idx_list) {
      if (shape_idx < 0 || shape_idx >= static_cast<int32_t>(graph_node_idx_list.size())) {
        continue;
      }
      int32_t node_idx = graph_node_idx_list[shape_idx];
      if (node_idx >= 0) {
        terminal_node_idx_list.push_back(node_idx);
      }
    }
    if (terminal_node_idx_list.empty()) {
      continue;
    }
    PhysicalGraphBuildTerminal build_terminal;
    build_terminal.set_terminal_name(terminal_name);
    build_terminal.set_node_idx_list(terminal_node_idx_list);
    physical_graph_build_data.get_net_terminal_build_data_map()[net_name].push_back(std::move(build_terminal));
  }
}

void DataManager::buildPhysicalGraphComponent(PhysicalGraphBuildData& physical_graph_build_data)
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  PhysicalGraph& physical_graph = _database.get_def_data().get_physical_graph();
  std::vector<PhysicalGraphBuildNode>& graph_node_list = physical_graph_build_data.get_graph_node_list();
  std::vector<std::pair<int32_t, int32_t>>& via_node_pair_list = physical_graph_build_data.get_via_node_pair_list();
  std::map<std::string, std::vector<PhysicalGraphBuildTerminal>>& net_terminal_build_data_map = physical_graph_build_data.get_net_terminal_build_data_map();

  DisjointSet graph(static_cast<int32_t>(graph_node_list.size()));
  std::map<int32_t, std::vector<int32_t>> layer_node_idx_map;
  for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(graph_node_list.size()); node_idx++) {
    layer_node_idx_map[graph_node_list[node_idx].get_shape().get_layer_idx()].push_back(node_idx);
  }

  LVSLOG.info(Loc::current(), "Building same-layer routing connectivity...");
  for (auto& [layer_idx, node_idx_list] : layer_node_idx_map) {
    LVSLOG.info(Loc::current(), "Building layer ", layer_idx, " routing connectivity: shape_num=", node_idx_list.size(), ".");
    std::vector<std::pair<BGRectInt, int32_t>> bg_rect_node_pair_list;
    bg_rect_node_pair_list.reserve(node_idx_list.size());
    for (int32_t node_idx : node_idx_list) {
      if (graph_node_list[node_idx].get_is_terminal()) {
        continue;
      }
      Shape& node_shape = graph_node_list[node_idx].get_shape();
      bg_rect_node_pair_list.emplace_back(convertToBGRectInt(node_shape), node_idx);
    }
    bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>> bg_rtree(bg_rect_node_pair_list);

    LVSLOG.info(Loc::current(), "Querying layer ", layer_idx, " routing connectivity.");
    for (int32_t node_idx : node_idx_list) {
      Shape& node_shape = graph_node_list[node_idx].get_shape();
      for (bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>::const_query_iterator query_iter
           = bg_rtree.qbegin(bgi::intersects(convertToBGRectInt(node_shape)));
           query_iter != bg_rtree.qend(); ++query_iter) {
        const std::pair<BGRectInt, int32_t>& candidate_rect_node_pair = *query_iter;
        int32_t active_node_idx = candidate_rect_node_pair.second;
        if (!graph_node_list[node_idx].get_is_terminal() && active_node_idx >= node_idx) {
          continue;
        }
        Shape& active_shape = graph_node_list[active_node_idx].get_shape();
        if (active_shape.get_ll_x() <= node_shape.get_ur_x() && node_shape.get_ll_x() <= active_shape.get_ur_x()
            && active_shape.get_ll_y() <= node_shape.get_ur_y() && node_shape.get_ll_y() <= active_shape.get_ur_y()) {
          graph.unite(active_node_idx, node_idx);
        }
      }
    }
    LVSLOG.info(Loc::current(), "Completed layer ", layer_idx, " routing connectivity.");
  }

  LVSLOG.info(Loc::current(), "Building via connectivity...");
  for (auto& [bottom_node_idx, top_node_idx] : via_node_pair_list) {
    graph.unite(bottom_node_idx, top_node_idx);
  }

  LVSLOG.info(Loc::current(), "Building terminal connectivity...");
  for (auto& [net_name, terminal_build_data_list] : net_terminal_build_data_map) {
    (void) net_name;
    for (PhysicalGraphBuildTerminal& build_terminal : terminal_build_data_list) {
      std::vector<int32_t>& node_idx_list = build_terminal.get_node_idx_list();
      for (int32_t node_idx = 1; node_idx < static_cast<int32_t>(node_idx_list.size()); node_idx++) {
        graph.unite(node_idx_list.front(), node_idx_list[node_idx]);
      }
    }
  }

  LVSLOG.info(Loc::current(), "Collecting physical graph components...");
  std::map<int32_t, std::set<std::string>> component_net_name_set_map;
  for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(graph_node_list.size()); node_idx++) {
    int32_t root = graph.find(node_idx);
    component_net_name_set_map[root].insert(graph_node_list[node_idx].get_net_name());
  }
  std::map<int32_t, int32_t> component_id_map;
  int32_t component_id = 0;
  for (auto& [root, net_name_set] : component_net_name_set_map) {
    component_id_map[root] = component_id;
    physical_graph.get_component_net_name_map()[component_id] = {net_name_set.begin(), net_name_set.end()};
    component_id++;
  }

  LVSLOG.info(Loc::current(), "Uploading physical graph components...");
  for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(graph_node_list.size()); node_idx++) {
    PhysicalGraphBuildNode& graph_node = graph_node_list[node_idx];
    int32_t node_component_id = component_id_map[graph.find(node_idx)];
    physical_graph.get_component_shape_map()[node_component_id].push_back(graph_node.get_shape());
    std::map<std::string, std::vector<int32_t>>::iterator component_id_list_iter
        = physical_graph.get_net_routing_shape_component_id_list_map().find(graph_node.get_net_name());
    if (component_id_list_iter == physical_graph.get_net_routing_shape_component_id_list_map().end()) {
      continue;
    }
    std::vector<int32_t>& component_id_list = component_id_list_iter->second;
    int32_t routing_shape_idx = graph_node.get_routing_shape_idx();
    if (routing_shape_idx >= 0 && routing_shape_idx < static_cast<int32_t>(component_id_list.size())) {
      component_id_list[routing_shape_idx] = node_component_id;
    }
  }
  for (auto& [net_name, terminal_build_data_list] : net_terminal_build_data_map) {
    (void) net_name;
    for (PhysicalGraphBuildTerminal& build_terminal : terminal_build_data_list) {
      std::vector<int32_t>& node_idx_list = build_terminal.get_node_idx_list();
      int32_t root = graph.find(node_idx_list.front());
      int32_t terminal_component_id = component_id_map[root];
      physical_graph.get_terminal_component_map()[build_terminal.get_terminal_name()] = terminal_component_id;
    }
  }

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

BGRectInt DataManager::convertToBGRectInt(const Shape& shape)
{
  return BGRectInt(BGPointInt(shape.get_ll_x(), shape.get_ll_y()), BGPointInt(shape.get_ur_x(), shape.get_ur_y()));
}

void DataManager::printConfig()
{
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(0), "LVS_CONFIG_INPUT");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(1), "temp_directory_path");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(1), "thread_number");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(2), _config.thread_number);
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(0), "LVS_CONFIG_BUILD");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(1), "log_file_path");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(2), _config.log_file_path);
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(1), "DataManager");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(2), "dm_temp_directory_path");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(3), _config.dm_temp_directory_path);
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(1), "EntityChecker");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(2), "ec_temp_directory_path");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(3), _config.ec_temp_directory_path);
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(1), "RoutingChecker");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(2), "rc_temp_directory_path");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(3), _config.rc_temp_directory_path);
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(1), "PDNChecker");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(2), "pc_temp_directory_path");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(3), _config.pc_temp_directory_path);
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(1), "LVSReporter");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(2), "lr_temp_directory_path");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(3), _config.lr_temp_directory_path);
}

void DataManager::printDatabase()
{
  LVSLOG.info(Loc::current(), "LVS_DATABASE");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(1), "netlist_design_name");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(2), _database.get_netlist_data().get_design_name());
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(1), "def_design_name");
  LVSLOG.info(Loc::current(), LVSUTIL.getSpaceByTabNum(2), _database.get_def_data().get_design_name());
}

#endif

#if 1  // 销毁

void DataManager::destroyDatabase()
{
  _database.reset();
}

#endif

}  // namespace ilvs
