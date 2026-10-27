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
#include <vector>

#include "GRGridGraph.hpp"
#include "GRNet.hpp"
#include "GRParams.hpp"
#include "PlanarCoord.hpp"

namespace irt {

class GRTreeBuilder
{
 public:
  GRTreeBuilder() = default;

  void prepareDetour(const GRGridGraph& grid);

  bool buildPattern(const GRNet& net, const GRGridGraph& grid, const GRParams& params, GRTree& routing_tree);
  bool buildDetour(const GRNet& net, const GRGridGraph& grid, const GRParams& params, GRTree& routing_tree);
  bool buildFromPaths(const GRNet& net, const GRGridGraph& grid, const GRParams& params, const std::vector<std::vector<PlanarCoord>>& path_list,
                      GRTree& routing_tree);
  void buildDirect(const GRNet& net, const GRGridGraph& grid, GRTree& routing_tree);

  bool wasDetourShifted() const { return detour_shifted_; }

 private:
  struct SkeletonNode;
  struct SkeletonTree;
  struct TopoNode;
  struct TopoTree;
  struct DpState;
  struct DetourChain;
  struct FluteTreeGuard;

  bool buildSkeletonTree(const GRNet& net, SkeletonTree& skeleton);
  bool buildFluteSkeletonTree(const GRNet& net, const std::vector<PlanarCoord>& coord_list, SkeletonTree& skeleton);
  bool attachPinLayerRange(const GRNet& net, const std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC>& coord_node_map, SkeletonTree& skeleton);
  bool attachPinLayerRange(const GRNet& net, const std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC>& coord_node_map, TopoTree& topo_tree);

  bool buildTopoTree(const GRNet& net, const GRGridGraph& grid, const GRParams& params, const SkeletonTree& skeleton, bool with_detour, TopoTree& topo_tree);
  bool buildTopoTreeFromPaths(const GRNet& net, const std::vector<std::vector<PlanarCoord>>& path_list, TopoTree& topo_tree);
  void buildPatternPathList(const GRGridGraph& grid, const GRParams& params, const PlanarCoord& parent_coord, const PlanarCoord& child_coord, bool with_detour,
                            std::vector<std::vector<PlanarCoord>>& path_list);
  void addPatternPathCandidate(const GRGridGraph& grid, std::vector<std::vector<PlanarCoord>>& path_list, const std::vector<PlanarCoord>& path) const;
  void addUDetourPathList(const GRGridGraph& grid, const GRParams& params, const PlanarCoord& parent_coord, const PlanarCoord& child_coord,
                          std::vector<std::vector<PlanarCoord>>& path_list);

  // detour: 把压过拥堵的直线骨架段整体侧移
  bool buildDetourSkeleton(const GRGridGraph& grid, const GRParams& params, const SkeletonTree& skeleton, SkeletonTree& detour_skeleton) const;
  void collectDetourChains(const GRParams& params, const SkeletonTree& skeleton, std::vector<DetourChain>& chain_list) const;
  double evaluateShift(const GRGridGraph& grid, const std::set<PlanarCoord, CmpPlanarCoordByXASC>& coord_set, const DetourChain& chain, int32_t offset) const;
  void applyShift(const DetourChain& chain, int32_t offset, SkeletonTree& detour_skeleton) const;
  bool isStraightSkeletonChain(const SkeletonTree& skeleton, const std::vector<int32_t>& chain) const;
  int32_t getCongestionPrefixSum(const GridMap<int32_t>& prefix_map, int32_t begin_x, int32_t begin_y, int32_t end_x, int32_t end_y) const;
  int32_t getSegmentCongestionNum(const PlanarCoord& first_coord, const PlanarCoord& second_coord) const;
  int32_t getPathCongestionNum(const std::vector<PlanarCoord>& path) const;
  double getPathLength(const std::vector<PlanarCoord>& path) const;
  void addDirectConnectVia(GRTree& routing_tree, const PlanarCoord& coord, int32_t first_layer_idx, int32_t second_layer_idx) const;

  // 层分配 DP: 后序求每个节点在各层的子树最优代价
  bool runDP(const GRNet& net, const GRGridGraph& grid, const GRParams& params, const TopoTree& topo_tree, GRTree& routing_tree);
  void computeNodeDP(const GRNet& net, const GRGridGraph& grid, const GRParams& params, DpState& state, int32_t node_idx);
  void prepareViaCost(const GRNet& net, const GRGridGraph& grid, const GRParams& params, DpState& state, int32_t node_idx);
  void computeChildCost(const GRNet& net, const GRGridGraph& grid, const GRParams& params, DpState& state, int32_t node_idx);
  void buildChildRangeMin(DpState& state, int32_t child_num) const;
  void sumChildRangeMin(DpState& state, int32_t child_num) const;
  void selectLayerWindow(DpState& state, int32_t node_idx) const;
  void getPostorder(int32_t node_idx, const TopoTree& topo_tree, std::vector<int32_t>& postorder);
  int32_t getOrCreateTopoNode(TopoTree& topo_tree, std::map<PlanarCoord, int32_t, CmpPlanarCoordByXASC>& coord_node_map, const PlanarCoord& coord);
  int32_t addBendNode(TopoTree& topo_tree, int32_t parent_idx, int32_t child_idx, const PlanarCoord& coord);
  int32_t addPatternPath(TopoTree& topo_tree, int32_t parent_idx, int32_t child_idx, const std::vector<PlanarCoord>& path);
  void addTopoEdge(TopoTree& topo_tree, int32_t parent_idx, int32_t child_idx, std::vector<int32_t> bend_idx_list = {});
  void reconstructTree(const DpState& state, int32_t node_idx, int32_t layer_idx, GRTree& routing_tree);

  GridMap<int32_t> horizontal_congestion_prefix_;
  GridMap<int32_t> vertical_congestion_prefix_;
  bool detour_shifted_ = false;
};

}  // namespace irt
