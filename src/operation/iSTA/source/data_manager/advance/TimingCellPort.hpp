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

#include "AnalysisType.hpp"
#include "LogicExpression.hpp"
#include "STAHeader.hpp"
#include "TransType.hpp"

namespace ista {

class TimingCellPort
{
 public:
  TimingCellPort() = default;
  ~TimingCellPort() = default;
  // getter
  std::string& get_port_name() { return _port_name; }
  double get_capacitance() const { return _capacitance; }
  double get_drive_resistance() const { return _drive_resistance; }
  std::map<AnalysisType, std::map<TransType, double>>& get_trans_capacitance_map() { return _trans_capacitance_map; }
  LogicExpression& get_function_expression() { return _function_expression; }
  bool get_is_input() const { return _is_input; }
  bool get_is_output() const { return _is_output; }
  bool get_is_clock() const { return _is_clock; }
  double get_fanout_load() const { return _fanout_load; }
  std::optional<double>& get_max_fanout() { return _max_fanout; }
  // setter
  void set_port_name(const std::string& port_name) { _port_name = port_name; }
  void set_capacitance(const double capacitance) { _capacitance = capacitance; }
  void set_drive_resistance(const double drive_resistance) { _drive_resistance = drive_resistance; }
  void set_trans_capacitance_map(const std::map<AnalysisType, std::map<TransType, double>>& trans_capacitance_map)
  {
    _trans_capacitance_map = trans_capacitance_map;
  }
  void set_function_expression(const LogicExpression& function_expression) { _function_expression = function_expression; }
  void set_is_input(const bool is_input) { _is_input = is_input; }
  void set_is_output(const bool is_output) { _is_output = is_output; }
  void set_is_clock(const bool is_clock) { _is_clock = is_clock; }
  void set_fanout_load(double fanout_load) { _fanout_load = fanout_load; }
  void set_max_fanout(const std::optional<double>& max_fanout) { _max_fanout = max_fanout; }
  // function

 private:
  std::string _port_name;
  double _capacitance = 0.0;
  double _drive_resistance = 0.0;
  std::map<AnalysisType, std::map<TransType, double>> _trans_capacitance_map;
  LogicExpression _function_expression;
  bool _is_input = false;
  bool _is_output = false;
  bool _is_clock = false;
  double _fanout_load = 0.0;
  std::optional<double> _max_fanout;
};

}  // namespace ista
