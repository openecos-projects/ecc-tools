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

class GRParams
{
 public:
  GRParams() = default;
  ~GRParams() = default;

  static constexpr double kInf = 1e18;
  static constexpr double kEpsilon = 1e-6;
  // 兜底布线时穿过没有可用 supply 的边的代价, 仍为有限值
  static constexpr double kBlockedEdgeCost = 1e6;

  // getter
  double get_unit_wire_cost() const { return _unit_wire_cost; }
  double get_initial_cost_weight() const { return _initial_cost_weight; }
  double get_cost_weight() const { return _cost_weight; }
  double get_congestion_saturation_ratio() const { return _congestion_saturation_ratio; }
  double get_congestion_hotspot_ratio() const { return _congestion_hotspot_ratio; }
  double get_via_base_cost() const { return _via_base_cost; }
  double get_via_demand() const { return _via_demand; }
  int32_t get_congestion_radius() const { return _congestion_radius; }
  double get_congestion_decay() const { return _congestion_decay; }
  double get_congestion_threshold() const { return _congestion_threshold; }
  double get_congestion_cost_scale() const { return _congestion_cost_scale; }
  int32_t get_max_detour_offset() const { return _max_detour_offset; }
  double get_max_detour_length_ratio() const { return _max_detour_length_ratio; }

  // setter
  void set_cost_weight(const double cost_weight) { _cost_weight = cost_weight; }
  void reset_cost_weight() { _cost_weight = _initial_cost_weight; }

 private:
  double _unit_wire_cost = 1.0;
  double _initial_cost_weight = 20.0;
  double _cost_weight = _initial_cost_weight;
  double _congestion_saturation_ratio = 0.8;
  double _congestion_hotspot_ratio = 0.9;
  double _via_base_cost = 3.0;
  double _via_demand = 0.5;
  int32_t _congestion_radius = 1;
  double _congestion_decay = 0.5;
  double _congestion_threshold = 0.8;
  double _congestion_cost_scale = 0.1;
  int32_t _max_detour_offset = 8;
  double _max_detour_length_ratio = 0.25;
};

}  // namespace irt
