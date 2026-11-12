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

#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

#include "LayerCoord.hpp"
#include "Net.hpp"
#include "PlanarRect.hpp"
#include "RTHeader.hpp"
#include "Segment.hpp"
#include "TATrackTable.hpp"

namespace irt {

#define RTTR (irt::TARouter::getInst())

class TARouter
{
 public:
  static void initInst();
  static TARouter& getInst();
  static void destroyInst();
  // function
  void route();

 private:
  struct TANode
  {
    PlanarCoord grid_coord;
    LayerCoord real_coord;                 // gcell center or via position or boundary coord
    std::vector<PlanarCoord> pin_ap_list;  // non-empty when this node hosts pin access points
    std::vector<int32_t> edge_idx_list;
  };

  struct TAEdge
  {
    int32_t first_node_idx = -1;
    int32_t second_node_idx = -1;
    int32_t layer_idx = -1;
    PlanarRect guide_rect;
    int32_t track_coord = 0;
    bool is_via = false;
  };

  struct TANet
  {
    Net* origin_net = nullptr;
    int32_t net_idx = -1;
    std::vector<TANode> node_list;
    std::vector<TAEdge> edge_list;
  };

  // (gcell_x, gcell_y, layer) -> node_idx
  using NodeMap = std::map<std::tuple<int32_t, int32_t, int32_t>, int32_t>;

  // self
  static TARouter* ta_router_instance_;

  TARouter() = default;
  TARouter(const TARouter& other) = delete;
  TARouter(TARouter&& other) = delete;
  ~TARouter() = default;
  TARouter& operator=(const TARouter& other) = delete;
  TARouter& operator=(TARouter&& other) = delete;

  // model
  void initTANetList();
  TANet buildTANet(Net& net);
  void buildTANetFromSegments(TANet& ta_net, const std::vector<Segment<LayerCoord>>& segment_list, NodeMap& node_map);
  int32_t getOrCreateNode(TANet& ta_net, NodeMap& node_map, int32_t x, int32_t y, int32_t layer_idx);
  void addEdge(TANet& ta_net, int32_t first_node_idx, int32_t second_node_idx, int32_t layer_idx, const PlanarRect& guide_rect, bool is_via);
  void attachPins(TANet& ta_net, NodeMap& node_map);
  void updateNodeAndViaCoords(TANet& ta_net);
  int32_t getPinTerminalTarget(const TANet& ta_net, int32_t node_idx, int32_t other_node_idx, bool is_horizontal) const;
  int32_t getPinTrackTarget(const TANet& ta_net, const TAEdge& edge, bool is_horizontal) const;
  // assign
  void assignTracks(TANet& ta_net);
  void assignWireTrack(TANet& ta_net, TAEdge& edge);
  void commitNetUsage(TANet& ta_net);
  void addViaUsage(TANet& ta_net, const TAEdge& edge);
  void rerouteOverflowNets();
  Direction getEdgeDirection(const TANet& ta_net, const TAEdge& edge) const;
  std::pair<int32_t, int32_t> getEdgeScaleRange(const TANet& ta_net, const TAEdge& edge, bool is_horizontal, bool use_pin_terminal) const;
  // output
  void outputNet(TANet& ta_net);
  void updateSummary();
  void printSummary();
  void addWireSegment(std::vector<Segment<LayerCoord>>& segment_list, const LayerCoord& first_coord, const LayerCoord& second_coord);

  // resource
  TATrackTable track_table_;
  std::vector<TANet> ta_net_list_;
};

}  // namespace irt
