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

namespace irt {

struct GRParams
{
  static constexpr double kInf = 1e18;
  static constexpr double kEpsilon = 1e-6;

  double unit_wire_cost = 1.0;
  double initial_cost_weight = 20.0;
  double cost_weight = 20.0;
  double congestion_saturation_ratio = 0.8;
  double congestion_hotspot_ratio = 0.9;
  double via_base_cost = 2.0;
  double via_demand = 0.5;
  int32_t congestion_radius = 1;
  double congestion_decay = 0.5;
  double congestion_threshold = 0.8;
  double congestion_cost_scale = 0.1;
  int32_t max_detour_offset = 8;
  double max_detour_length_ratio = 0.25;
};

}  // namespace irt
