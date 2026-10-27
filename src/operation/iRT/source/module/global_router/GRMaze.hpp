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

namespace irt {

class GRMaze
{
 public:
  GRMaze() = default;

  bool buildPathList(const GRNet& net, const GRGridGraph& grid, const GRParams& params, std::vector<std::vector<PlanarCoord>>& path_list);

 private:
  struct Field
  {
    int32_t x_size = 0;
    int32_t y_size = 0;

    std::vector<double> g;
    std::vector<int32_t> parent;
    std::vector<char> closed;
    std::vector<int32_t> search_touched;

    std::vector<double> edge_cost;
    std::vector<int32_t> cost_touched;

    void init(int32_t x_size_, int32_t y_size_);
    void beginNet();
    void clearSearch();
    void setCost(int32_t state, double cost, int32_t parent_state);
    void setClosed(int32_t state);
  };

  double getWireCost(const GRNet& net, const GRGridGraph& grid, const GRParams& params, int32_t x, int32_t y, bool is_horizontal);
  double computeWireCost(const GRNet& net, const GRGridGraph& grid, const GRParams& params, int32_t x, int32_t y, bool is_horizontal) const;

  static constexpr double kNoEdgeCost = -1.0;

  Field field_;
};

}  // namespace irt
