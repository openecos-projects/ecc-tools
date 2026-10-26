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
#include <string>
#include <vector>

#include "EXTLayerRect.hpp"
#include "GRGridGraph.hpp"
#include "GRNet.hpp"
#include "GRParams.hpp"
#include "LayerRect.hpp"

namespace irt {

#define RTGR (irt::GlobalRouter::getInst())

class GlobalRouter
{
 public:
  static void initInst();
  static GlobalRouter& getInst();
  static void destroyInst();
  // function
  void route();

 private:
  // self
  static GlobalRouter* _gr_instance;

  // parameter
  GRParams _gr_params;

  // runtime state
  bool _last_detour_shifted = false;
  std::vector<GRNet*> _pattern_failed_net_list;
  std::vector<GRNet*> _unrouted_net_list;
  GridMap<int32_t> _horizontal_congestion_prefix;
  GridMap<int32_t> _vertical_congestion_prefix;

  struct GRDpState
  {
    const GRTopoTree& topo_tree;
    int32_t layer_num;
    int32_t bottom_layer_idx;
    int32_t top_layer_idx;
    std::vector<std::vector<double>> d;
    std::vector<std::vector<int32_t>> best_b;
    std::vector<std::vector<int32_t>> best_t;
    std::vector<std::vector<std::vector<int32_t>>> best_child_layer;
    std::vector<std::vector<std::vector<int32_t>>> best_child_bend;
    std::vector<bool> computed;

    GRDpState(const GRTopoTree& tree, int32_t node_num_, int32_t layer_num_, int32_t bottom_, int32_t top_)
        : topo_tree(tree),
          layer_num(layer_num_),
          bottom_layer_idx(bottom_),
          top_layer_idx(top_),
          d(node_num_, std::vector<double>(layer_num_, GRParams::kInf)),
          best_b(node_num_, std::vector<int32_t>(layer_num_, -1)),
          best_t(node_num_, std::vector<int32_t>(layer_num_, -1)),
          best_child_layer(node_num_, std::vector<std::vector<int32_t>>(layer_num_)),
          best_child_bend(node_num_, std::vector<std::vector<int32_t>>(layer_num_)),
          computed(node_num_, false)
    {
    }
  };

  GlobalRouter() = default;
  GlobalRouter(const GlobalRouter& other) = delete;
  GlobalRouter(GlobalRouter&& other) = delete;
  ~GlobalRouter() = default;
  GlobalRouter& operator=(const GlobalRouter& other) = delete;
  GlobalRouter& operator=(GlobalRouter&& other) = delete;

  // init
  void initGRGridGraph();
  void initGRNetList();
  void buildExemptEdgeSet(GRNet& net);
  void addPinEscapeCorridor(GRNet& net, const GRPin& gr_pin, bool is_horizontal, int32_t direction);
  GRWireEdgeId getEscapeEdgeId(int32_t x, int32_t y, bool is_horizontal, int32_t direction, int32_t layer_idx) const;

  // supply
  void analyzeSupply();
  EXTLayerRect getSearchRect(const LayerCoord& first_coord, const LayerCoord& second_coord);
  std::vector<LayerRect> getCrossingWireList(const EXTLayerRect& search_rect);

  // route
  void patternRouteGRNetList();
  void detourRouteGRNetList();
  void mazeRouteGRNetList();
  void routeUnroutedNetList();
  void updateCongestionCost();
  bool routeGRNetByMaze(GRNet& net, bool allow_blocked_edge);
  bool buildMazeRoutingTree(GRNet& net, GRTree& routing_tree, bool allow_blocked_edge);
  bool buildMazePathList(const GRNet& net, std::vector<std::vector<PlanarCoord>>& path_list, bool allow_blocked_edge);
  bool buildMazeTopoTree(GRNet& net, const std::vector<std::vector<PlanarCoord>>& path_list);
  double getMazeWireCost(const GRNet& net, int32_t x, int32_t y, bool is_horizontal, bool allow_blocked_edge) const;
  double getDirectionCost() const;
  std::vector<GRNet*> getOverflowNetList();
  bool isNetOverflowing(const GRNet& net) const;
  double getTotalOverflow() const;
  double getTotalRoutingCost() const;
  double getRoutingTreeCost(const GRNet& net, const GRTree& tree) const;
  double getRoutingTreeOverflow(const GRNet& net, const GRTree& tree) const;
  bool buildRoutingTree(GRNet& net, GRTree& routing_tree);
  bool buildDetourRoutingTree(GRNet& net, GRTree& routing_tree);
  bool buildSkeletonTree(GRNet& net, GRSkeletonTree& skeleton);
  bool buildTopoTree(GRNet& net, const GRSkeletonTree& skeleton, bool with_detour);
  bool buildDetourSkeleton(const GRSkeletonTree& skeleton, GRSkeletonTree& detour_skeleton) const;
  void buildPatternPathList(const PlanarCoord& parent_coord, const PlanarCoord& child_coord, bool with_detour,
                            std::vector<std::vector<PlanarCoord>>& path_list);
  void addPatternPathCandidate(std::vector<std::vector<PlanarCoord>>& path_list, const std::vector<PlanarCoord>& path) const;
  void addUDetourPathList(const PlanarCoord& parent_coord, const PlanarCoord& child_coord, std::vector<std::vector<PlanarCoord>>& path_list);
  void initDetourCongestionView();
  int32_t getCongestionPrefixSum(const GridMap<int32_t>& prefix_map, int32_t begin_x, int32_t begin_y, int32_t end_x, int32_t end_y) const;
  int32_t getSegmentCongestionNum(const PlanarCoord& first_coord, const PlanarCoord& second_coord) const;
  int32_t getPathCongestionNum(const std::vector<PlanarCoord>& path) const;
  double getPathLength(const std::vector<PlanarCoord>& path) const;
  bool isStraightSkeletonChain(const GRSkeletonTree& skeleton, const std::vector<int32_t>& chain) const;
  bool runDP(const GRNet& net, GRTree& routing_tree, bool allow_blocked_edge);
  void computeNodeDP(const GRNet& net, GRDpState& state, int32_t node_idx, bool allow_blocked_edge);
  void getPostorder(int32_t node_idx, const GRTopoTree& topo_tree, std::vector<int32_t>& postorder);
  int32_t getOrCreateTopoNode(GRTopoTree& topo_tree, std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC>& coord_node_map, const PlanarCoord& coord);
  int32_t addBendNode(GRTopoTree& topo_tree, int32_t parent_idx, int32_t child_idx, const PlanarCoord& coord);
  int32_t addPatternPath(GRTopoTree& topo_tree, int32_t parent_idx, int32_t child_idx, const std::vector<PlanarCoord>& path);
  void addTopoEdge(GRTopoTree& topo_tree, int32_t parent_idx, int32_t child_idx, std::vector<int32_t> bend_idx_list = {});
  void reconstructTree(const GRNet& net, const std::vector<std::vector<int32_t>>& best_b, const std::vector<std::vector<int32_t>>& best_t,
                       const std::vector<std::vector<std::vector<int32_t>>>& best_child_layer,
                       const std::vector<std::vector<std::vector<int32_t>>>& best_child_bend, int32_t node_idx, int32_t layer_idx, GRTree& routing_tree);

  // cost
  double getWireSegmentCost(const GRNet& net, int32_t layer_idx, const PlanarCoord& first_coord, const PlanarCoord& second_coord,
                            bool allow_blocked_edge) const;
  double getWireEdgeCost(const GRNet& net, const GRWireEdgeId& edge_id, double amount, bool allow_blocked_edge) const;
  double getViaCost(const GRNet& net, const PlanarCoord& coord, int32_t below_layer_idx, bool allow_blocked_edge) const;
  double getCongestionCost(double supply, double demand, double amount) const;
  double addCost(double first, double second) const;
  bool isExemptWireEdge(const GRNet& net, const GRWireEdgeId& edge_id) const;
  bool isAPViaSide(const GRNet& net, const PlanarCoord& coord, int32_t layer_idx) const;

  // commit
  void ripupRoute(GRNet& net, const GRTree& routing_tree);
  void commitRoute(GRNet& net, const GRTree& routing_tree);
  void buildWireEdgeSet(const GRTree& routing_tree, std::set<GRWireEdgeId>& wire_edge_set) const;
  bool hasPlanarEdgeOrAP(const GRNet& net, const std::set<GRWireEdgeId>& wire_edge_set, const PlanarCoord& coord,
                         int32_t layer_idx) const;
  void addWireDemand(const GRNet& net, const GRTree& routing_tree, std::set<GRWireEdgeId>& used_edge_set, double delta);
  void addViaDemand(const GRNet& net, const GRViaStack& via_stack, const std::set<GRWireEdgeId>& wire_edge_set,
                    std::set<GRWireEdgeId>& used_edge_set, double delta);
  void outputGlobalResult();
  void printSummary();
  bool isLayerInRange(int32_t layer_idx) const;
  std::string getNetIdxList(const std::set<int32_t>& net_idx_set) const;
  void debugPlotGRModel(const std::string& flag);

  // environment
  GRGridGraph _grid_graph;
  std::vector<GRNet> _gr_net_list;
};

}  // namespace irt
