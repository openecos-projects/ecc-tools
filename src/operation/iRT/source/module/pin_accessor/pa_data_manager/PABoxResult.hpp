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
#include "Segment.hpp"
#include "Violation.hpp"

namespace irt {

class PATaskResult
{
 public:
  std::vector<Segment<LayerCoord>>& get_segment_list() { return _segment_list; }
  std::vector<EXTLayerRect>& get_patch_list() { return _patch_list; }
  AccessPoint& get_access_point() { return _access_point; }
  void set_access_point(const AccessPoint& access_point) { _access_point = access_point; }

 private:
  std::vector<Segment<LayerCoord>> _segment_list;
  std::vector<EXTLayerRect> _patch_list;
  AccessPoint _access_point;
};

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
  std::vector<PATaskResult>& get_task_result_list() { return _task_result_list; }
  std::vector<Violation>& get_route_violation_list() { return _route_violation_list; }
  // setter
  void set_valid(bool valid) { _valid = valid; }

 private:
  bool _valid = false;
  std::vector<PATaskResult> _task_result_list;
  std::vector<Violation> _route_violation_list;
};

}  // namespace irt
