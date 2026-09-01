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
#include "NetRoutingGraph.hpp"
#include "PCModel.hpp"
#include "Shape.hpp"

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
  std::vector<SupplyPoint> getSupplyPointList();
  bool getSupplyRoutingLayerOrder(int32_t& top_layer_order, int32_t& second_top_layer_order);
  void addSupplyViaLayerOrder(std::set<int32_t>& layer_order_set, ConnectType connect_type);
  bool isPowerGround(ConnectType connect_type);
  void addCenterSupplyPoint(std::vector<SupplyPoint>& supply_point_list, int32_t center_x, int32_t center_y, int32_t top_layer_order,
                            int32_t second_top_layer_order);
  SupplyPoint getCenterSupplyPoint(ConnectType connect_type, int32_t center_x, int32_t center_y, int32_t top_layer_order, int32_t second_top_layer_order);
  int32_t getTopRoutingShapeIdx(const NetRoutingGraph& routing_graph, const std::pair<int32_t, int32_t>& via_shape_idx_pair, int32_t top_layer_order,
                                int32_t second_top_layer_order);
  bool isValidRoutingShapeIdx(int32_t routing_shape_idx, const std::vector<RoutingShape>& routing_shape_list);
  int64_t getShapeCenterDistance(const Shape& shape, int32_t point_x, int32_t point_y);
  void checkSupplyConnectivity(PCModel& pc_model, ConnectType connect_type);
  void updateSummary(PCModel& pc_model);
};

}  // namespace ilvs
