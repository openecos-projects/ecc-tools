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
#include "TransType.hpp"

namespace ista {

class TimingPathState
{
 public:
  TimingPathState() = default;
  ~TimingPathState() = default;
  // getter
  double get_arrival() const { return _arrival; }
  double get_slew() const { return _slew; }
  double get_launch_time() const { return _launch_time; }
  std::string& get_start_point() { return _start_point; }
  std::string& get_predecessor() { return _predecessor; }
  std::string& get_clock_name() { return _clock_name; }
  std::string& get_crpr_clock_pin() { return _crpr_clock_pin; }
  std::size_t get_predecessor_arc_idx() const { return _predecessor_arc_idx; }
  double get_predecessor_arc_delay() const { return _predecessor_arc_delay; }
  TransType get_trans_type() const { return _trans_type; }
  TransType get_predecessor_trans_type() const { return _predecessor_trans_type; }
  TransType get_crpr_clock_trans_type() const { return _crpr_clock_trans_type; }
  // setter
  void set_arrival(const double arrival) { _arrival = arrival; }
  void set_slew(const double slew) { _slew = slew; }
  void set_launch_time(const double launch_time) { _launch_time = launch_time; }
  void set_start_point(const std::string& start_point) { _start_point = start_point; }
  void set_predecessor(const std::string& predecessor) { _predecessor = predecessor; }
  void set_clock_name(const std::string& clock_name) { _clock_name = clock_name; }
  void set_crpr_clock_pin(const std::string& crpr_clock_pin) { _crpr_clock_pin = crpr_clock_pin; }
  void set_predecessor_arc_idx(const std::size_t predecessor_arc_idx) { _predecessor_arc_idx = predecessor_arc_idx; }
  void set_predecessor_arc_delay(const double predecessor_arc_delay) { _predecessor_arc_delay = predecessor_arc_delay; }
  void set_trans_type(const TransType& trans_type) { _trans_type = trans_type; }
  void set_predecessor_trans_type(const TransType& predecessor_trans_type) { _predecessor_trans_type = predecessor_trans_type; }
  void set_crpr_clock_trans_type(const TransType& crpr_clock_trans_type) { _crpr_clock_trans_type = crpr_clock_trans_type; }
  // function

 private:
  double _arrival = -std::numeric_limits<double>::infinity();
  double _slew = 0.0;
  double _launch_time = 0.0;
  std::string _start_point;
  std::string _predecessor;
  std::string _clock_name;
  std::string _crpr_clock_pin;
  std::size_t _predecessor_arc_idx = std::numeric_limits<std::size_t>::max();
  double _predecessor_arc_delay = 0.0;
  TransType _trans_type = TransType::kNone;
  TransType _predecessor_trans_type = TransType::kNone;
  TransType _crpr_clock_trans_type = TransType::kNone;
};

}  // namespace ista
