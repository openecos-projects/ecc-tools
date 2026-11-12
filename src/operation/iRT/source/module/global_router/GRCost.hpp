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

#include "GRGridGraph.hpp"
#include "GRNet.hpp"
#include "GRParams.hpp"

namespace irt {

double edgeCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, const GRWireEdgeId& edge_id, double amount = 1.0);
double viaCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, const PlanarCoord& coord, int32_t below_layer_idx);
double segmentCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, int32_t layer_idx, const PlanarCoord& first_coord,
                   const PlanarCoord& second_coord);
double wireMoveCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, int32_t x, int32_t y, bool is_horizontal);
double treeCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, const GRTree& tree);
double treeOverflow(const GRGridGraph& grid, const GRNet& net, const GRTree& tree);
double congestionCost(const GRParams& params, double supply, double demand, double amount);
double directionCost(const GRParams& params);
double addCost(double first, double second);
bool isBetterResult(double old_overflow, double old_cost, double new_overflow, double new_cost);

}  // namespace irt
