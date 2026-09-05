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

#include "AccessPoint.hpp"
#include "PAPin.hpp"
#include "Segment.hpp"
#include "Violation.hpp"

namespace irt {

class PABoxResult
{
 public:
  PABoxResult() = default;
  PABoxResult(const PABoxResult&) = default;
  PABoxResult(PABoxResult&&) = default;
  PABoxResult& operator=(const PABoxResult&) = default;
  PABoxResult& operator=(PABoxResult&&) = default;
  ~PABoxResult() = default;
  // getter
  bool get_valid() const { return _valid; }
  std::map<int32_t, std::map<int32_t, std::vector<Segment<LayerCoord>>>>& get_net_task_result_map() { return _net_task_result_map; }
  std::map<int32_t, std::map<int32_t, std::vector<EXTLayerRect>>>& get_net_task_patch_map() { return _net_task_patch_map; }
  std::map<PAPin*, AccessPoint>& get_pin_access_point_map() { return _pin_access_point_map; }
  std::vector<Violation>& get_route_violation_list() { return _route_violation_list; }
  // setter
  void set_valid(bool valid) { _valid = valid; }

 private:
  bool _valid = false;
  std::map<int32_t, std::map<int32_t, std::vector<Segment<LayerCoord>>>> _net_task_result_map;
  std::map<int32_t, std::map<int32_t, std::vector<EXTLayerRect>>> _net_task_patch_map;
  std::map<PAPin*, AccessPoint> _pin_access_point_map;
  std::vector<Violation> _route_violation_list;
};

}  // namespace irt
