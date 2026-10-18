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

#include "PlanarRect.hpp"

namespace idrc {

// routing 层与 cut 层的 layer_idx 是两套独立编号（都从 0 起），比较时必须连 is_routing 一起看
struct MacroObsLayer
{
  int32_t layer_idx = -1;
  bool is_routing = true;
  int32_t min_spacing = -1;
  PlanarRect bounding_box;
  std::vector<PlanarRect> obs_rect_list;
  std::vector<PlanarRect> pin_rect_list;
};

// 一次检查以一个 macro 为单位
struct MacroObs
{
  std::vector<MacroObsLayer> obs_layer_list;
};

}  // namespace idrc
