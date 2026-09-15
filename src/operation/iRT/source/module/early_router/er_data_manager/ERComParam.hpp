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

#include "ERStage.hpp"
#include "RTHeader.hpp"

namespace irt {

class ERComParam
{
 public:
  ERComParam() = default;
  ERComParam(ERStage stage, std::string resolve_congestion, int32_t max_candidate_point_num, int32_t topo_spilt_length, int32_t expand_step_num,
             int32_t expand_step_length, double via_unit, double overflow_unit, int32_t schedule_interval)
  {
    _stage = stage;
    _resolve_congestion = resolve_congestion;
    _max_candidate_point_num = max_candidate_point_num;
    _topo_spilt_length = topo_spilt_length;
    _expand_step_num = expand_step_num;
    _expand_step_length = expand_step_length;
    _via_unit = via_unit;
    _overflow_unit = overflow_unit;
    _schedule_interval = schedule_interval;
  }
  ~ERComParam() = default;
  // getter
  ERStage get_stage() const { return _stage; }
  std::string& get_resolve_congestion() { return _resolve_congestion; }
  int32_t get_max_candidate_point_num() const { return _max_candidate_point_num; }
  int32_t get_topo_spilt_length() const { return _topo_spilt_length; }
  int32_t get_expand_step_num() const { return _expand_step_num; }
  int32_t get_expand_step_length() const { return _expand_step_length; }
  double get_via_unit() const { return _via_unit; }
  double get_overflow_unit() const { return _overflow_unit; }
  int32_t get_schedule_interval() const { return _schedule_interval; }
  // setter
  void set_stage(const ERStage stage) { _stage = stage; }
  void set_resolve_congestion(std::string& resolve_congestion) { _resolve_congestion = resolve_congestion; }
  void set_max_candidate_point_num(const int32_t max_candidate_point_num) { _max_candidate_point_num = max_candidate_point_num; }
  void set_topo_spilt_length(const int32_t topo_spilt_length) { _topo_spilt_length = topo_spilt_length; }
  void set_expand_step_num(const int32_t expand_step_num) { _expand_step_num = expand_step_num; }
  void set_expand_step_length(const int32_t expand_step_length) { _expand_step_length = expand_step_length; }
  void set_via_unit(const double via_unit) { _via_unit = via_unit; }
  void set_overflow_unit(const double overflow_unit) { _overflow_unit = overflow_unit; }
  void set_schedule_interval(const int32_t schedule_interval) { _schedule_interval = schedule_interval; }

 private:
  ERStage _stage = ERStage::kNone;
  std::string _resolve_congestion;
  int32_t _max_candidate_point_num = -1;
  int32_t _topo_spilt_length = 0;
  int32_t _expand_step_num = 0;
  int32_t _expand_step_length = 0;
  double _via_unit = 0;
  double _overflow_unit = 0;
  int32_t _schedule_interval = 0;
};

}  // namespace irt
