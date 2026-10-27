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

// 线网对单条网格边的代价. 退火权重是 cost 参数的一部分, 由 GRParams 统一携带.
double edgeCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, const GRWireEdgeId& edge_id, double amount = 1.0);
// 一个 via 的代价: 基础代价加上两侧各两条相邻边的线代价.
double viaCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, const PlanarCoord& coord, int32_t below_layer_idx);
// 同层一段水平/垂直线的代价.
double segmentCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, int32_t layer_idx, const PlanarCoord& first_coord,
                   const PlanarCoord& second_coord);
double treeCost(const GRGridGraph& grid, const GRNet& net, const GRParams& params, const GRTree& tree);
double treeOverflow(const GRGridGraph& grid, const GRNet& net, const GRTree& tree);
double congestionCost(const GRParams& params, double supply, double demand, double amount);
double directionCost(const GRParams& params);
double addCost(double first, double second);
bool isBetter(double old_overflow, double old_cost, double new_overflow, double new_cost);

}  // namespace irt
