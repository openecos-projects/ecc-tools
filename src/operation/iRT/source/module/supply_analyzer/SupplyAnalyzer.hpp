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
#include "DataManager.hpp"
#include "Database.hpp"
#include "Net.hpp"

namespace irt {

#define RTSA (irt::SupplyAnalyzer::getInst())

class SupplyAnalyzer
{
 public:
  static void initInst();
  static SupplyAnalyzer& getInst();
  static void destroyInst();
  // function
  void analyze();

 private:
  using ShapeRTree = bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>;

  // self
  static SupplyAnalyzer* _sa_instance;

  SupplyAnalyzer() = default;
  SupplyAnalyzer(const SupplyAnalyzer& other) = delete;
  SupplyAnalyzer(SupplyAnalyzer&& other) = delete;
  ~SupplyAnalyzer() = default;
  SupplyAnalyzer& operator=(const SupplyAnalyzer& other) = delete;
  SupplyAnalyzer& operator=(SupplyAnalyzer&& other) = delete;
  // function
  void initRoutingEdgeMap();
  std::vector<ShapeRTree> buildShapeRTreeList();
  void analyzeSupply();
  EXTLayerRect getSearchRect(const LayerCoord& first_coord, const LayerCoord& second_coord);
  std::vector<LayerRect> getCrossingWireList(const EXTLayerRect& search_rect);
  bool isAccess(const LayerRect& wire, const std::vector<PlanarRect>& enlarged_obs_rect_list);

#if 1  // exhibit
  void updateSummary();
  void printSummary();
  void outputPlanarSupplyCSV();
  void outputLayerSupplyCSV();
#endif

#if 1  // debug
  void debugPlotSAModel();
#endif
};

}  // namespace irt
