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
#include <vector>

#include "GRGridGraph.hpp"
#include "GRNet.hpp"
#include "GRParams.hpp"
#include "GridMap.hpp"
#include "PlanarCoord.hpp"

namespace irt {

struct GRDetourGrid
{
  static GRDetourGrid build(const GRGridGraph& grid);
  int32_t getSegmentCongestionNum(const PlanarCoord& first_coord, const PlanarCoord& second_coord) const;
  int32_t getPathCongestionNum(const std::vector<PlanarCoord>& path) const;

  int32_t x_size = 0;
  int32_t y_size = 0;
  GridMap<int32_t> horizontal_prefix;
  GridMap<int32_t> vertical_prefix;
};

bool buildPatternTree(const GRNet& net, const GRGridGraph& grid, const GRParams& params, GRTree& routing_tree);
bool buildDetourTree(const GRNet& net, const GRGridGraph& grid, const GRParams& params, const GRDetourGrid& detour_grid, GRTree& routing_tree,
                     bool& detour_shifted);
bool buildTreeFromPaths(const GRNet& net, const GRGridGraph& grid, const GRParams& params, const std::vector<std::vector<PlanarCoord>>& path_list,
                        GRTree& routing_tree);
void buildDirectTree(const GRNet& net, const GRGridGraph& grid, GRTree& routing_tree);

}  // namespace irt
