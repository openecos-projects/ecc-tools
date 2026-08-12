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
#include "DisjointSet.hpp"
#include "LVSHeader.hpp"
#include "Monitor.hpp"
#include "Net.hpp"
#include "PhysicalGraph.hpp"
#include "RCModel.hpp"

namespace ilvs {

#define LVSRC (ilvs::RoutingChecker::getInst())

class RoutingChecker
{
 public:
  static void initInst();
  static RoutingChecker& getInst();
  static void destroyInst();
  // function
  void check();

 private:
  // self
  static RoutingChecker* _rc_instance;

  RoutingChecker() = default;
  RoutingChecker(const RoutingChecker& other) = delete;
  RoutingChecker(RoutingChecker&& other) = delete;
  ~RoutingChecker() = default;
  RoutingChecker& operator=(const RoutingChecker& other) = delete;
  RoutingChecker& operator=(RoutingChecker&& other) = delete;

  RCModel initRCModel();
  void checkRouting(RCModel& rc_model);
  RoutingCheck checkNetRoutingConnectivity(const std::string& net_name, const Net& net, const NetRoutingGraph* routing_graph);
  bool isIntersected(const Shape& first_shape, const Shape& second_shape);
  int32_t getTerminalRoot(const NetRoutingGraph& routing_graph, const std::string& terminal_name, DisjointSet& graph);
  void checkShort(RCModel& rc_model);
  void updateSummary(RCModel& rc_model);
};

}  // namespace ilvs
