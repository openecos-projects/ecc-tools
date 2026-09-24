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
#include "PWHeader.hpp"
#include "TransType.hpp"

namespace ipw {

class Arc;
class TimingCellArc;

class DCTask
{
 public:
  DCTask() = default;
  ~DCTask() = default;
  // getter
  Arc* get_arc() { return _arc; }
  TimingCellArc* get_timing_cell_arc() { return _timing_cell_arc; }
  std::string& get_output_pin() { return _output_pin; }
  AnalysisType& get_analysis_type() { return _analysis_type; }
  TransType& get_input_trans_type() { return _input_trans_type; }
  TransType& get_output_trans_type() { return _output_trans_type; }
  double get_input_slew() { return _input_slew; }
  double get_output_slew_input_slew() { return _output_slew_input_slew; }
  bool get_has_output_slew_input_slew() { return _has_output_slew_input_slew; }
  double get_output_slew() { return _output_slew; }
  bool get_is_valid() { return _is_valid; }
  // setter
  void set_arc(Arc* arc) { _arc = arc; }
  void set_timing_cell_arc(TimingCellArc* timing_cell_arc) { _timing_cell_arc = timing_cell_arc; }
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
  void set_output_slew(const double output_slew) { _output_slew = output_slew; }
  void set_is_valid(const bool is_valid) { _is_valid = is_valid; }
  // function

 private:
  Arc* _arc = nullptr;
  TimingCellArc* _timing_cell_arc = nullptr;
  std::string _output_pin;
  AnalysisType _analysis_type = AnalysisType::kNone;
  TransType _input_trans_type = TransType::kNone;
  TransType _output_trans_type = TransType::kNone;
  double _input_slew = 0.0;
  // A sequential C2Q arc can use a different slew for delay and transition lookup.
  double _output_slew_input_slew = 0.0;
  bool _has_output_slew_input_slew = false;
  double _output_slew = 0.0;
  bool _is_valid = false;
};

}  // namespace ipw
