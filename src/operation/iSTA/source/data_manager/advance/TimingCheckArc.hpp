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
#include "TimingArc.hpp"
#include "TimingCheckType.hpp"

namespace ista {

class TimingCheckArc
{
 public:
  TimingCheckArc() = default;
  ~TimingCheckArc() = default;
  // getter
  std::string& get_clock_port() { return _clock_port; }
  std::string& get_data_port() { return _data_port; }
  TimingCheckType get_check_type() const { return _check_type; }
  double get_check_time() const { return _check_time; }
  std::vector<TimingArc>& get_timing_arc_list() { return _timing_arc_list; }
  TransType get_clock_trans_type() const { return _clock_trans_type; }
  // setter
  void set_clock_port(const std::string& clock_port) { _clock_port = clock_port; }
  void set_data_port(const std::string& data_port) { _data_port = data_port; }
  void set_check_type(const TimingCheckType& check_type) { _check_type = check_type; }
  void set_check_time(const double check_time) { _check_time = check_time; }
  void set_timing_arc_list(const std::vector<TimingArc>& timing_arc_list) { _timing_arc_list = timing_arc_list; }
  void set_clock_trans_type(const TransType& clock_trans_type) { _clock_trans_type = clock_trans_type; }
  // function

 private:
  std::string _clock_port;
  std::string _data_port;
  TimingCheckType _check_type = TimingCheckType::kNone;
  double _check_time = 0.0;
  std::vector<TimingArc> _timing_arc_list;
  TransType _clock_trans_type = TransType::kRise;
};

}  // namespace ista
