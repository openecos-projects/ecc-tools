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

#include "STAHeader.hpp"

namespace ista {

class TimingClock
{
 public:
  TimingClock() = default;
  ~TimingClock() = default;
  // getter
  std::string& get_clock_name() { return _clock_name; }
  std::vector<std::string>& get_source_list() { return _source_list; }
  double get_period() const { return _period; }
  double get_rise_edge() const { return _rise_edge; }
  double get_fall_edge() const { return _fall_edge; }
  bool get_is_propagated() const { return _is_propagated; }
  // setter
  void set_clock_name(const std::string& clock_name) { _clock_name = clock_name; }
  void set_source_list(const std::vector<std::string>& source_list) { _source_list = source_list; }
  void set_period(const double period) { _period = period; }
  void set_rise_edge(const double rise_edge) { _rise_edge = rise_edge; }
  void set_fall_edge(const double fall_edge) { _fall_edge = fall_edge; }
  void set_is_propagated(const bool is_propagated) { _is_propagated = is_propagated; }
  // function

 private:
  std::string _clock_name;
  std::vector<std::string> _source_list;
  double _period = 0.0;
  double _rise_edge = 0.0;
  double _fall_edge = 0.0;
  bool _is_propagated = false;
};

}  // namespace ista
