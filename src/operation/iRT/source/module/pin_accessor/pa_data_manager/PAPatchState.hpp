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

#include "PATask.hpp"
#include "Violation.hpp"

namespace irt {

class PAPatchState
{
 public:
  PAPatchState() = default;
  PAPatchState(const PAPatchState&) = default;
  PAPatchState(PAPatchState&&) = default;
  PAPatchState& operator=(const PAPatchState&) = default;
  PAPatchState& operator=(PAPatchState&&) = default;
  ~PAPatchState() = default;
  // single task
  PATask* get_curr_patch_task() { return _curr_patch_task; }
  std::vector<EXTLayerRect>& get_routing_patch_list() { return _routing_patch_list; }
  std::vector<Violation>& get_patch_violation_list() { return _patch_violation_list; }
  std::set<Violation, CmpViolation>& get_tried_fix_violation_set() { return _tried_fix_violation_set; }
  void set_curr_patch_task(PATask* curr_patch_task) { _curr_patch_task = curr_patch_task; }
  void set_routing_patch_list(const std::vector<EXTLayerRect>& routing_patch_list) { _routing_patch_list = routing_patch_list; }
  void set_patch_violation_list(const std::vector<Violation>& patch_violation_list) { _patch_violation_list = patch_violation_list; }
  void set_tried_fix_violation_set(const std::set<Violation, CmpViolation>& tried_fix_violation_set) { _tried_fix_violation_set = tried_fix_violation_set; }
  // single violation
  Violation& get_curr_patch_violation() { return _curr_patch_violation; }
  void set_curr_patch_violation(const Violation& curr_patch_violation) { _curr_patch_violation = curr_patch_violation; }

 private:
  // single task
  PATask* _curr_patch_task = nullptr;
  std::vector<EXTLayerRect> _routing_patch_list;
  std::vector<Violation> _patch_violation_list;
  std::set<Violation, CmpViolation> _tried_fix_violation_set;
  // single violation
  Violation _curr_patch_violation;
};

}  // namespace irt
