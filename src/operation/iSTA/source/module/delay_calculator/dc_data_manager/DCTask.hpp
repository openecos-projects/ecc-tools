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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "AnalysisType.hpp"
#include "DCProcType.hpp"
#include "DCTimingResult.hpp"
#include "STAHeader.hpp"
#include "TransType.hpp"

namespace ista {

class Arc;
class TimingCellArc;
class TimingCheckArc;

class DCTask
{
 public:
  DCTask() = default;
  ~DCTask() = default;
  // getter
  DCProcType& get_proc_type() { return _proc_type; }
  Arc* get_arc() { return _arc; }
  TimingCellArc* get_timing_cell_arc() { return _timing_cell_arc; }
  TimingCheckArc* get_timing_check_arc() { return _timing_check_arc; }
  std::string& get_output_pin() { return _output_pin; }
  AnalysisType& get_analysis_type() { return _analysis_type; }
  TransType& get_input_trans_type() { return _input_trans_type; }
  TransType& get_output_trans_type() { return _output_trans_type; }
  double get_input_slew() { return _input_slew; }
  double get_output_slew_input_slew() { return _output_slew_input_slew; }
  bool get_has_output_slew_input_slew() { return _has_output_slew_input_slew; }
  TransType& get_clock_trans_type() { return _clock_trans_type; }
  TransType& get_data_trans_type() { return _data_trans_type; }
  double get_clock_slew() { return _clock_slew; }
  double get_data_slew() { return _data_slew; }
  DCTimingResult& get_timing_result() { return _timing_result; }
  bool get_is_valid() { return _is_valid; }
  // setter
  void set_proc_type(const DCProcType& proc_type) { _proc_type = proc_type; }
  void set_arc(Arc* arc) { _arc = arc; }
  void set_timing_cell_arc(TimingCellArc* timing_cell_arc) { _timing_cell_arc = timing_cell_arc; }
  void set_timing_check_arc(TimingCheckArc* timing_check_arc) { _timing_check_arc = timing_check_arc; }
  void set_output_pin(const std::string& output_pin) { _output_pin = output_pin; }
  void set_analysis_type(const AnalysisType& analysis_type) { _analysis_type = analysis_type; }
  void set_input_trans_type(const TransType& input_trans_type) { _input_trans_type = input_trans_type; }
  void set_output_trans_type(const TransType& output_trans_type) { _output_trans_type = output_trans_type; }
  void set_input_slew(const double input_slew) { _input_slew = input_slew; }
  void set_output_slew_input_slew(const double input_slew)
  {
    _output_slew_input_slew = input_slew;
    _has_output_slew_input_slew = true;
  }
  void set_clock_trans_type(const TransType& clock_trans_type) { _clock_trans_type = clock_trans_type; }
  void set_data_trans_type(const TransType& data_trans_type) { _data_trans_type = data_trans_type; }
  void set_clock_slew(const double clock_slew) { _clock_slew = clock_slew; }
  void set_data_slew(const double data_slew) { _data_slew = data_slew; }
  void set_timing_result(const DCTimingResult& timing_result) { _timing_result = timing_result; }
  void set_is_valid(const bool is_valid) { _is_valid = is_valid; }
  // function

 private:
  DCProcType _proc_type = DCProcType::kNone;
  Arc* _arc = nullptr;
  TimingCellArc* _timing_cell_arc = nullptr;
  TimingCheckArc* _timing_check_arc = nullptr;
  std::string _output_pin;
  AnalysisType _analysis_type = AnalysisType::kNone;
  TransType _input_trans_type = TransType::kNone;
  TransType _output_trans_type = TransType::kNone;
  double _input_slew = 0.0;
  // A sequential C2Q arc can use a different slew for delay and transition lookup.
  double _output_slew_input_slew = 0.0;
  bool _has_output_slew_input_slew = false;
  TransType _clock_trans_type = TransType::kNone;
  TransType _data_trans_type = TransType::kNone;
  double _clock_slew = 0.0;
  double _data_slew = 0.0;
  DCTimingResult _timing_result;
  bool _is_valid = false;
};

}  // namespace ista
