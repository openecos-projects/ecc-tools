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

#include "PWHeader.hpp"
#include "TimingCellArc.hpp"
#include "TimingCellPort.hpp"
#include "TimingLeakagePower.hpp"
#include "TimingPowerArc.hpp"
#include "TimingSequential.hpp"

namespace ipw {

class TimingCell
{
 public:
  TimingCell() = default;
  ~TimingCell() = default;
  // getter
  std::vector<TimingSequential>& get_sequentials() { return _sequentials; }
  std::string& get_library_name() { return _library_name; }
  double get_nom_voltage() const { return _nom_voltage; }
  double get_cell_leakage_power() const { return _cell_leakage_power; }
  std::map<std::string, TimingCellPort>& get_port_map() { return _port_map; }
  std::vector<TimingCellArc>& get_cell_arc_list() { return _cell_arc_list; }
  std::vector<TimingPowerArc>& get_power_arc_list() { return _power_arc_list; }
  std::vector<TimingLeakagePower>& get_leakage_power_list() { return _leakage_power_list; }
  std::string& get_clock_port_name() { return _clock_port_name; }
  std::string& get_data_port_name() { return _data_port_name; }
  bool get_is_sequential() const { return _is_sequential; }
  // Power can simulate an explicit state function even if the Liberty cell is
  // not marked sequential.
  bool get_is_sequential_for_power() const { return _is_sequential || !_sequentials.empty(); }
  bool get_is_clock_gating() const { return _is_clock_gating; }
  bool get_is_macro() const { return _is_macro; }
  double get_slew_lower_threshold_pct_rise() const { return _slew_lower_threshold_pct_rise; }
  double get_slew_upper_threshold_pct_rise() const { return _slew_upper_threshold_pct_rise; }
  double get_slew_lower_threshold_pct_fall() const { return _slew_lower_threshold_pct_fall; }
  double get_slew_upper_threshold_pct_fall() const { return _slew_upper_threshold_pct_fall; }
  double get_input_threshold_pct_rise() const { return _input_threshold_pct_rise; }
  double get_output_threshold_pct_rise() const { return _output_threshold_pct_rise; }
  double get_input_threshold_pct_fall() const { return _input_threshold_pct_fall; }
  double get_output_threshold_pct_fall() const { return _output_threshold_pct_fall; }
  double get_slew_derate_from_library() const { return _slew_derate_from_library; }
  // setter
  void set_library_name(const std::string& library_name) { _library_name = library_name; }
  void set_nom_voltage(const double nom_voltage) { _nom_voltage = nom_voltage; }
  void set_cell_leakage_power(const double cell_leakage_power) { _cell_leakage_power = cell_leakage_power; }
  void set_clock_port_name(const std::string& clock_port_name) { _clock_port_name = clock_port_name; }
  void set_data_port_name(const std::string& data_port_name) { _data_port_name = data_port_name; }
  void set_is_sequential(const bool is_sequential) { _is_sequential = is_sequential; }
  void set_is_clock_gating(const bool is_clock_gating) { _is_clock_gating = is_clock_gating; }
  void set_is_macro(const bool is_macro) { _is_macro = is_macro; }
  void set_slew_lower_threshold_pct_rise(const double slew_lower_threshold_pct_rise) { _slew_lower_threshold_pct_rise = slew_lower_threshold_pct_rise; }
  void set_slew_upper_threshold_pct_rise(const double slew_upper_threshold_pct_rise) { _slew_upper_threshold_pct_rise = slew_upper_threshold_pct_rise; }
  void set_slew_lower_threshold_pct_fall(const double slew_lower_threshold_pct_fall) { _slew_lower_threshold_pct_fall = slew_lower_threshold_pct_fall; }
  void set_slew_upper_threshold_pct_fall(const double slew_upper_threshold_pct_fall) { _slew_upper_threshold_pct_fall = slew_upper_threshold_pct_fall; }
  void set_input_threshold_pct_rise(const double input_threshold_pct_rise) { _input_threshold_pct_rise = input_threshold_pct_rise; }
  void set_output_threshold_pct_rise(const double output_threshold_pct_rise) { _output_threshold_pct_rise = output_threshold_pct_rise; }
  void set_input_threshold_pct_fall(const double input_threshold_pct_fall) { _input_threshold_pct_fall = input_threshold_pct_fall; }
  void set_output_threshold_pct_fall(const double output_threshold_pct_fall) { _output_threshold_pct_fall = output_threshold_pct_fall; }
  void set_slew_derate_from_library(const double slew_derate_from_library) { _slew_derate_from_library = slew_derate_from_library; }
  // function
  void resolveDefaultPowerArcConditions()
  {
    using Group = std::tuple<std::string, std::string, std::string>;
    std::map<Group, LogicExpression> covered;
    for (auto& arc : _power_arc_list) {
      auto& condition = arc.get_when_expression();
      if (condition.get_is_empty()) continue;
      auto& combined = covered[{arc.get_source_port(), arc.get_sink_port(), arc.get_related_pg_port()}];
      bool append = !combined.get_is_empty();
      auto& terms = combined.get_term_list();
      terms.insert(terms.end(), condition.get_term_list().begin(), condition.get_term_list().end());
      if (append) {
        LogicExpressionTerm either;
        either.set_operation_type(LogicOperationType::kOr);
        terms.push_back(either);
      }
    }
    for (auto& arc : _power_arc_list) {
      if (!arc.get_when_expression().get_is_empty()) continue;
      auto found = covered.find({arc.get_source_port(), arc.get_sink_port(), arc.get_related_pg_port()});
      if (found == covered.end()) continue;
      // A default table applies only where the state-dependent tables for
      // this path and supply rail do not apply. Use a Boolean union so that
      // overlapping conditions cannot double-count the covered probability.
      LogicExpression fallback = found->second;
      LogicExpressionTerm invert;
      invert.set_operation_type(LogicOperationType::kNot);
      fallback.get_term_list().push_back(invert);
      arc.set_when_expression(fallback);
    }
  }

 private:
  std::vector<TimingSequential> _sequentials;
  std::string _library_name;
  double _nom_voltage = 0.0;
  double _cell_leakage_power = 0.0;
  std::map<std::string, TimingCellPort> _port_map;
  std::vector<TimingCellArc> _cell_arc_list;
  std::vector<TimingPowerArc> _power_arc_list;
  std::vector<TimingLeakagePower> _leakage_power_list;
  std::string _clock_port_name;
  std::string _data_port_name;
  bool _is_sequential = false;
  bool _is_clock_gating = false;
  bool _is_macro = false;
  double _slew_lower_threshold_pct_rise = 0.3;
  double _slew_upper_threshold_pct_rise = 0.7;
  double _slew_lower_threshold_pct_fall = 0.3;
  double _slew_upper_threshold_pct_fall = 0.7;
  double _input_threshold_pct_rise = 0.5;
  double _output_threshold_pct_rise = 0.5;
  double _input_threshold_pct_fall = 0.5;
  double _output_threshold_pct_fall = 0.5;
  double _slew_derate_from_library = 1.0;
};

}  // namespace ipw
