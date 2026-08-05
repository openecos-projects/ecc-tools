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

#include "Config.hpp"
#include "Database.hpp"
#include "LVSHeader.hpp"
#include "PhysicalGraphBuildData.hpp"
#include "NetRoutingData.hpp"
#include "NetRoutingGraph.hpp"
#include "RoutingShape.hpp"

namespace ilvs {

#define LVSDM (ilvs::DataManager::getInst())

class DataManager
{
 public:
  static void initInst();
  static DataManager& getInst();
  static void destroyInst();

  // function
  void input(std::map<std::string, std::any>& config_map);
  void output();

  // getter
  Config& getConfig() { return _config; }
  Database& getDatabase() { return _database; }

 private:
  static DataManager* _dm_instance;
  // config & database
  Config _config;
  Database _database;

  DataManager() = default;
  DataManager(const DataManager& other) = delete;
  DataManager(DataManager&& other) = delete;
  ~DataManager() = default;
  DataManager& operator=(const DataManager& other) = delete;
  DataManager& operator=(DataManager&& other) = delete;

#if 1  // 构建
  void buildConfig();
  void buildDatabase();
  void buildNetlistData();
  void buildDefData();
  void buildNetRoutingGraph();
  void buildNetRoutingGraph(const NetRoutingData& net_routing_data, NetRoutingGraph& net_routing_graph);
  int32_t buildRoutingGraphShape(NetRoutingGraph& net_routing_graph, const RoutingShape& routing_shape);
  void buildPhysicalGraph();
  void buildPhysicalGraphNode(PhysicalGraphBuildData& physical_graph_build_data, const std::string& net_name,
                              const NetRoutingGraph& routing_graph, bool build_terminal_shape);
  void buildPhysicalGraphComponent(PhysicalGraphBuildData& physical_graph_build_data);
  BGRectInt convertToBGRectInt(const Shape& shape);
  void printConfig();
  void printDatabase();
#endif

#if 1  // 销毁
  void destroyDatabase();
#endif
};

}  // namespace ilvs
