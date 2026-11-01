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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "EXTLayerRect.hpp"
#include "GRGridGraph.hpp"
#include "GRMaze.hpp"
#include "GRNet.hpp"
#include "GRParams.hpp"
#include "GRTreeBuilder.hpp"
#include "LayerRect.hpp"

namespace irt {

#define RTGR (irt::GlobalRouter::getInst())

class GlobalRouter
{
 public:
  static void initInst();
  static GlobalRouter& getInst();
  static void destroyInst();
  void route();

 private:
  static GlobalRouter* gr_instance_;

  GlobalRouter() = default;
  GlobalRouter(const GlobalRouter& other) = delete;
  GlobalRouter(GlobalRouter&& other) = delete;
  ~GlobalRouter() = default;
  GlobalRouter& operator=(const GlobalRouter& other) = delete;
  GlobalRouter& operator=(GlobalRouter&& other) = delete;

  // model
  void initGridGraph();
  void analyzeSupply();
  EXTLayerRect getSearchRect(const LayerCoord& first_coord, const LayerCoord& second_coord);
  std::vector<LayerRect> getCrossingWireList(const EXTLayerRect& search_rect);
  void initNetList();

  // route
  void patternRoute();
  void detourRoute();
  std::vector<size_t> getOverflowNetIndexList() const;
  void mazeRouteFailedNets();
  void mazeRerouteOverflowNets();
  double getTotalRoutingCost() const;
  bool routeNetByMaze(GRNet& net);
  void fallbackRoute();

  // output
  void uploadGRResult();
  void printSummary() const;
  bool isLayerInRange(int32_t layer_idx) const;
  void debugPlot(const std::string& flag) const;

  GRParams params_;
  GRGridGraph grid_graph_;
  std::vector<GRNet> net_list_;
  GRMazeWorkspace maze_workspace_;
  std::vector<size_t> pattern_failed_net_idx_list_;
  std::vector<size_t> unrouted_net_idx_list_;
};

}  // namespace irt
