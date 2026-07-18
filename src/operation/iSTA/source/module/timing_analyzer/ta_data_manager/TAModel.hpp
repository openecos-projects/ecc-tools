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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "STAHeader.hpp"
#include "TimingPathGroup.hpp"

namespace ista {

class TAModel
{
 public:
  TAModel() = default;
  ~TAModel() = default;
  // getter
  std::map<std::string, TimingPathGroup>& get_timing_path_group_map() { return _timing_path_group_map; }
  std::size_t get_checked_end_point_num() { return _checked_end_point_num; }
  std::size_t get_unconstrained_end_point_num() { return _unconstrained_end_point_num; }
  std::size_t get_violating_end_point_num() { return _violating_end_point_num; }
  double get_worst_slack() { return _worst_slack; }
  double get_total_negative_slack() { return _total_negative_slack; }
  std::string& get_worst_end_point() { return _worst_end_point; }
  // setter
  void set_timing_path_group_map(const std::map<std::string, TimingPathGroup>& timing_path_group_map)
  {
    _timing_path_group_map = timing_path_group_map;
  }
  void set_checked_end_point_num(const std::size_t checked_end_point_num) { _checked_end_point_num = checked_end_point_num; }
  void set_unconstrained_end_point_num(const std::size_t unconstrained_end_point_num)
  {
    _unconstrained_end_point_num = unconstrained_end_point_num;
  }
  void set_violating_end_point_num(const std::size_t violating_end_point_num) { _violating_end_point_num = violating_end_point_num; }
  void set_worst_slack(const double worst_slack) { _worst_slack = worst_slack; }
  void set_total_negative_slack(const double total_negative_slack) { _total_negative_slack = total_negative_slack; }
  void set_worst_end_point(const std::string& worst_end_point) { _worst_end_point = worst_end_point; }
  // function

 private:
  std::map<std::string, TimingPathGroup> _timing_path_group_map;
  std::size_t _checked_end_point_num = 0;
  std::size_t _unconstrained_end_point_num = 0;
  std::size_t _violating_end_point_num = 0;
  double _worst_slack = 0.0;
  double _total_negative_slack = 0.0;
  std::string _worst_end_point;
};

}  // namespace ista
