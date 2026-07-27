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
#pragma once

#include "Config.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "LVSHeader.hpp"
#include "Monitor.hpp"
#include "PCModel.hpp"
#include "PhysicalGraph.hpp"
#include "SupplyTrack.hpp"

namespace ilvs {

#define LVSPC (ilvs::PDNChecker::getInst())

class PDNChecker
{
 public:
  static void initInst();
  static PDNChecker& getInst();
  static void destroyInst();
  // function
  void check();

 private:
  // self
  static PDNChecker* _pc_instance;

  PDNChecker() = default;
  PDNChecker(const PDNChecker& other) = delete;
  PDNChecker(PDNChecker&& other) = delete;
  ~PDNChecker() = default;
  PDNChecker& operator=(const PDNChecker& other) = delete;
  PDNChecker& operator=(PDNChecker&& other) = delete;

  PCModel initPCModel();
  void buildSupplyPoint(PCModel& pc_model);
  std::vector<SupplyPoint> getSupplyPointList(const PhysicalGraph& physical_graph);
  bool isPowerGround(ConnectType connect_type);
  int32_t getMidpoint(int32_t first_coordinate, int32_t second_coordinate);
  SupplyPoint makeSupplyPoint(const SupplyTrack& supply_track);
  void checkSupplyConnectivity(PCModel& pc_model, const std::map<std::string, std::string>& instance_pin_net_map,
                               ConnectType connect_type);
};

}  // namespace ilvs
