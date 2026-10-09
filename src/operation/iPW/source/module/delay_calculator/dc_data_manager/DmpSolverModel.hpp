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

#include "PWHeader.hpp"
#include "TransType.hpp"

namespace ipw {

class TimingArc;

class DmpSolverModel
{
 public:
  DmpSolverModel() = default;
  ~DmpSolverModel() = default;
  // getter
  TimingArc*& get_timing_arc() { return _timing_arc; }
  TransType& get_trans_type() { return _trans_type; }
  double& get_input_slew() { return _input_slew; }
  double& get_driver_capacitance() { return _driver_capacitance; }
  double& get_pi_resistance() { return _pi_resistance; }
  double& get_load_capacitance() { return _load_capacitance; }
  double& get_driver_resistance() { return _driver_resistance; }
  double& get_threshold() { return _threshold; }
  double& get_lower_threshold() { return _lower_threshold; }
  double& get_upper_threshold() { return _upper_threshold; }
  double& get_slew_derate() { return _slew_derate; }
  bool& get_is_pi() { return _is_pi; }
  bool& get_is_zero_c2() { return _is_zero_c2; }
  int32_t& get_newton_order() { return _newton_order; }
  double& get_start_time() { return _start_time; }
  double& get_transition_time() { return _transition_time; }
  double& get_pi_pole1() { return _pi_pole1; }
  double& get_pi_pole2() { return _pi_pole2; }
  double& get_pi_zero() { return _pi_zero; }
  double& get_pi_scale() { return _pi_scale; }
  double& get_pi_constant1() { return _pi_constant1; }
  double& get_pi_constant2() { return _pi_constant2; }
  double& get_pi_residue1() { return _pi_residue1; }
  double& get_pi_residue2() { return _pi_residue2; }
  double& get_pi_current_constant() { return _pi_current_constant; }
  double& get_pi_current_residue1() { return _pi_current_residue1; }
  double& get_pi_current_residue2() { return _pi_current_residue2; }
  double& get_zero_pole() { return _zero_pole; }
  double& get_zero_zero() { return _zero_zero; }
  double& get_zero_scale() { return _zero_scale; }
  double& get_zero_constant1() { return _zero_constant1; }
  double& get_zero_constant2() { return _zero_constant2; }
  double& get_zero_residue() { return _zero_residue; }
  std::array<double, 3>& get_parameter_list() { return _parameter_list; }
  std::array<double, 3>& get_function_list() { return _function_list; }
  std::array<std::array<double, 3>, 3>& get_jacobian() { return _jacobian; }
  std::array<double, 3>& get_scale_list() { return _scale_list; }
  std::array<double, 3>& get_delta_list() { return _delta_list; }
  std::array<int32_t, 3>& get_index_list() { return _index_list; }
 private:
  TimingArc* _timing_arc = nullptr;
  TransType _trans_type = TransType::kNone;
  double _input_slew = 0.0;
  double _driver_capacitance = 0.0;
  double _pi_resistance = 0.0;
  double _load_capacitance = 0.0;
  double _driver_resistance = 0.0;
  double _threshold = 0.5;
  double _lower_threshold = 0.3;
  double _upper_threshold = 0.7;
  double _slew_derate = 1.0;
  bool _is_pi = false;
  bool _is_zero_c2 = false;
  int32_t _newton_order = 0;
  double _start_time = 0.0;
  double _transition_time = 0.0;
  double _pi_pole1 = 0.0;
  double _pi_pole2 = 0.0;
  double _pi_zero = 0.0;
  double _pi_scale = 0.0;
  double _pi_constant1 = 0.0;
  double _pi_constant2 = 0.0;
  double _pi_residue1 = 0.0;
  double _pi_residue2 = 0.0;
  double _pi_current_constant = 0.0;
  double _pi_current_residue1 = 0.0;
  double _pi_current_residue2 = 0.0;
  double _zero_pole = 0.0;
  double _zero_zero = 0.0;
  double _zero_scale = 0.0;
  double _zero_constant1 = 0.0;
  double _zero_constant2 = 0.0;
  double _zero_residue = 0.0;
  std::array<double, 3> _parameter_list = {0.0, 0.0, 0.0};
  std::array<double, 3> _function_list = {0.0, 0.0, 0.0};
  std::array<std::array<double, 3>, 3> _jacobian = {{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}};
  std::array<double, 3> _scale_list = {0.0, 0.0, 0.0};
  std::array<double, 3> _delta_list = {0.0, 0.0, 0.0};
  std::array<int32_t, 3> _index_list = {0, 0, 0};
};

}  // namespace ipw
