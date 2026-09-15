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

namespace idb {
class IdbLayerRouting;
class IdbLayerCut;
enum class IdbLayerDirection : uint8_t;
}  // namespace idb

namespace idrc {

class DRCInterface;

#define DRCDM (idrc::DataManager::getInst())

class DataManager
{
 public:
  static void initInst();
  static DataManager& getInst();
  static void destroyInst();
  // function
  void input(std::map<std::string, std::any>& config_map);

#if 1  // 获得唯一的pitch
  int32_t getOnlyPitch();
#endif

  const std::vector<int32_t>& getAdjacentCutLayerIdxList(int32_t routing_layer_idx);
  const std::vector<int32_t>& getAdjacentRoutingLayerIdxList(int32_t cut_layer_idx);

  Config& getConfig() { return _config; }
  Database& getDatabase() { return _database; }

 private:
  friend class DRCInterface;

  static DataManager* _dm_instance;
  // config & database
  Config _config;
  Database _database;
  int32_t _only_pitch = -1;

  DataManager() = default;
  DataManager(const DataManager& other) = delete;
  DataManager(DataManager&& other) = delete;
  ~DataManager() = default;
  DataManager& operator=(const DataManager& other) = delete;
  DataManager& operator=(DataManager&& other) = delete;

  void wrapConfig(std::map<std::string, std::any>& config_map);
  void wrapDatabase();
  void wrapDBInfo();
  void wrapMicronDBU();
  void wrapManufactureGrid();
  void wrapDie();
  void wrapDesignRule();
  void wrapLayerList();
  void wrapTrackAxis(RoutingLayer& routing_layer, idb::IdbLayerRouting* idb_layer);
  void wrapRoutingDesignRule(RoutingLayer& routing_layer, idb::IdbLayerRouting* idb_layer);
  void wrapCutDesignRule(CutLayer& cut_layer, idb::IdbLayerCut* idb_layer);
  void wrapLayerInfo();
  Direction getDRCDirectionByDB(idb::IdbLayerDirection idb_direction);

#if 1  // build
  void buildConfig();
  void buildDatabase();
  void buildDie();
  void checkDie();
  void buildDesignRule();
  void buildLayerList();
  void transLayerList();
  void makeLayerList();
  void makeRoutingLayerList();
  void makeCutLayerList();
  void checkLayerList();
  void buildLayerInfo();
  void printConfig();
  void printDatabase();
#endif
};

}  // namespace idrc
