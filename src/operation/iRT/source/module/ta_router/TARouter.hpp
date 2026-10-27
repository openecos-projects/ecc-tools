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
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "Database.hpp"
#include "LayerCoord.hpp"
#include "Net.hpp"
#include "PlanarRect.hpp"
#include "RTHeader.hpp"
#include "Segment.hpp"

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
    LayerCoord real_coord;
    int32_t layer_idx = -1;
    bool is_pin = false;
    std::vector<PlanarCoord> pin_ap_list;
    std::vector<int32_t> edge_idx_list;
  };

  struct TAEdge
  {
    int32_t first_node_idx = -1;
    int32_t second_node_idx = -1;
    int32_t layer_idx = -1;
    PlanarRect guide_rect;
    std::vector<int32_t> candidate_track_list;
    int32_t begin_scale = 0;
    int32_t end_scale = 0;
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

  struct TrackInterval
  {
    int32_t begin = 0;
    int32_t end = 0;
    int32_t net_idx = -1;
  };

  struct TrackOverlap
  {
    int32_t interval_num = 0;
    double length = 0.0;
  };

  using NodeMap = std::map<std::tuple<int32_t, int32_t, int32_t>, int32_t>;

  // self
  static TARouter* _ta_router_instance;

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
  std::set<int32_t> updateOverflowNetSet();
  double getTrackHistory(int32_t layer_idx, int32_t track_coord) const;
  void ripupNet(const TANet& ta_net);
  Direction getEdgeDirection(const TANet& ta_net, const TAEdge& edge) const;
  bool isHorizontalEdge(const TANet& ta_net, const TAEdge& edge) const;
  bool isVerticalEdge(const TANet& ta_net, const TAEdge& edge) const;
  std::pair<int32_t, int32_t> getEdgeScaleRange(const TANet& ta_net, const TAEdge& edge, bool is_horizontal) const;
  TrackOverlap getTrackOverlap(int32_t layer_idx, int32_t track_coord, int32_t begin, int32_t end, int32_t net_idx) const;
  void addTrackUsage(int32_t layer_idx, int32_t track_coord, int32_t begin, int32_t end, int32_t net_idx);
  // output
  void outputNet(TANet& ta_net);
  void updateSummary();
  void printSummary();
  void addWireSegment(std::vector<Segment<LayerCoord>>& segment_list, const LayerCoord& first_coord, const LayerCoord& second_coord);

  // resource
  // key = (layer_idx << 32) | track_coord, 用扁平 key 避免双层 std::map 的指针追逐
  std::unordered_map<int64_t, std::vector<TrackInterval>> _track_usage;
  std::unordered_map<int64_t, double> _track_history;
  // net_idx -> 该 net 占用过的 track key, 用于 ripup 时只访问自己的 track
  std::unordered_map<int32_t, std::vector<int64_t>> _net_track_index;
  std::vector<TANet> _ta_net_list;
};

}  // namespace irt
