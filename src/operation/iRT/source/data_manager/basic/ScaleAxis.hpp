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

#include "ScaleGrid.hpp"

namespace irt {

class ScaleAxis
{
 public:
  ScaleAxis() = default;
  ~ScaleAxis() = default;
  // getter
  std::vector<ScaleGrid>& get_x_grid_list() { return _x_grid_list; }
  std::vector<ScaleGrid>& get_y_grid_list() { return _y_grid_list; }
  const std::vector<int32_t>& get_x_scale_list() const { return _x_scale_list; }
  const std::vector<int32_t>& get_y_scale_list() const { return _y_scale_list; }
  // setter
  void set_x_grid_list(const std::vector<ScaleGrid>& x_grid_list)
  {
    _x_grid_list = x_grid_list;
    _x_scale_list = makeScaleList(x_grid_list);
  }
  void set_y_grid_list(const std::vector<ScaleGrid>& y_grid_list)
  {
    _y_grid_list = y_grid_list;
    _y_scale_list = makeScaleList(y_grid_list);
  }
  // function
 private:
  static std::vector<int32_t> makeScaleList(const std::vector<ScaleGrid>& grid_list)
  {
    std::vector<int32_t> scale_list;
    size_t scale_num = 0;
    for (const ScaleGrid& grid : grid_list) {
      scale_num += std::max(grid.get_step_num(), 0) + 1;
    }
    scale_list.reserve(scale_num);
    for (const ScaleGrid& grid : grid_list) {
      for (int32_t i = 0; i <= grid.get_step_num(); i++) {
        int32_t scale = grid.get_start_line() + i * grid.get_step_length();
        if (scale_list.empty() || scale_list.back() != scale) {
          scale_list.push_back(scale);
        }
      }
    }
    return scale_list;
  }

  std::vector<ScaleGrid> _x_grid_list;
  std::vector<ScaleGrid> _y_grid_list;
  std::vector<int32_t> _x_scale_list;
  std::vector<int32_t> _y_scale_list;
};
}  // namespace irt
