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
#include "TimingClock.hpp"
#include "TimingException.hpp"
#include "TimingPortConstraint.hpp"

namespace ipw {

enum class TimingCaseValue
{
  kZero,
  kOne,
  kStatic,
  kRise,
  kFall
};

class TimingClockUncertainty
{
 public:
  const std::set<std::string>& get_from_clocks() const { return _from_clocks; }
  const std::set<std::string>& get_to_clocks() const { return _to_clocks; }
  TransType get_from_trans_type() const { return _from_trans_type; }
  TransType get_to_trans_type() const { return _to_trans_type; }
  bool get_setup() const { return _setup; }
  bool get_hold() const { return _hold; }
  double get_value() const { return _value; }

  void set_from_clocks(std::set<std::string> value) { _from_clocks = std::move(value); }
  void set_to_clocks(std::set<std::string> value) { _to_clocks = std::move(value); }
  void set_from_trans_type(TransType value) { _from_trans_type = value; }
  void set_to_trans_type(TransType value) { _to_trans_type = value; }
  void set_setup(bool value) { _setup = value; }
  void set_hold(bool value) { _hold = value; }
  void set_value(double value) { _value = value; }

  bool matches(std::string_view from_clock, TransType from_trans_type, std::string_view to_clock, TransType to_trans_type,
               AnalysisType analysis_type) const
  {
    return (analysis_type == AnalysisType::kMax ? _setup : _hold) && (_from_clocks.empty() || _from_clocks.contains(std::string(from_clock)))
           && (_to_clocks.empty() || _to_clocks.contains(std::string(to_clock)))
           && (_from_trans_type == TransType::kNone || _from_trans_type == from_trans_type)
           && (_to_trans_type == TransType::kNone || _to_trans_type == to_trans_type);
  }

  int specificity() const
  {
    return (!_from_clocks.empty() ? 4 : 0) + (!_to_clocks.empty() ? 4 : 0) + (_from_trans_type != TransType::kNone ? 1 : 0)
           + (_to_trans_type != TransType::kNone ? 1 : 0);
  }

 private:
  std::set<std::string> _from_clocks;
  std::set<std::string> _to_clocks;
  TransType _from_trans_type = TransType::kNone;
  TransType _to_trans_type = TransType::kNone;
  bool _setup = true;
  bool _hold = true;
  double _value = 0.0;
};

class TimingLoadConstraint
{
 public:
  double get(AnalysisType analysis_type, TransType trans_type) const
  {
    const auto analysis = _load_map.find(analysis_type);
    if (analysis == _load_map.end()) {
      return 0.0;
    }
    const auto transition = analysis->second.find(trans_type);
    return transition == analysis->second.end() ? 0.0 : transition->second;
  }
  bool has(AnalysisType analysis_type, TransType trans_type) const
  {
    const auto analysis = _load_map.find(analysis_type);
    return analysis != _load_map.end() && analysis->second.contains(trans_type);
  }
  void set(AnalysisType analysis_type, TransType trans_type, double value) { _load_map[analysis_type][trans_type] = value; }

 private:
  std::map<AnalysisType, std::map<TransType, double>> _load_map;
};

class TimingConstraint
{
 public:
  TimingConstraint() = default;
  ~TimingConstraint() = default;
  // getter
  std::string& get_sdc_file_path() { return _sdc_file_path; }
  std::map<std::string, TimingClock>& get_clock_map() { return _clock_map; }
  std::map<std::string, TimingPortConstraint>& get_port_constraint_map() { return _port_constraint_map; }
  std::map<std::string, TimingCaseValue>& get_case_analysis_map() { return _case_analysis_map; }
  std::map<std::string, TimingCaseValue>& get_effective_case_analysis_map() { return _effective_case_analysis_map; }
  std::vector<TimingException>& get_path_exception_list() { return _path_exception_list; }
  // Compatibility alias for callers written before path exceptions shared one model.
  std::vector<TimingException>& get_false_path_list() { return _path_exception_list; }
  std::vector<TimingClockGroup>& get_clock_group_list() { return _clock_group_list; }
  std::optional<double>& get_max_fanout() { return _max_fanout; }
  std::map<std::string, double>& get_port_max_fanout_map() { return _port_max_fanout_map; }
  std::map<std::string, TimingLoadConstraint>& get_net_load_map() { return _net_load_map; }
  std::vector<TimingClockUncertainty>& get_clock_uncertainty_list() { return _clock_uncertainty_list; }
  std::set<std::string>& get_path_break_start_points() { return _path_break_start_points; }
  std::set<std::string>& get_path_break_end_points() { return _path_break_end_points; }
  // setter
  void set_sdc_file_path(const std::string& sdc_file_path) { _sdc_file_path = sdc_file_path; }
  void set_clock_map(const std::map<std::string, TimingClock>& clock_map) { _clock_map = clock_map; }
  void set_port_constraint_map(const std::map<std::string, TimingPortConstraint>& port_constraint_map) { _port_constraint_map = port_constraint_map; }
  void set_case_analysis_map(const std::map<std::string, TimingCaseValue>& case_analysis_map) { _case_analysis_map = case_analysis_map; }
  std::optional<double> resolve_clock_uncertainty_value(std::string_view from_clock, TransType from_trans_type, std::string_view to_clock,
                                                        TransType to_trans_type, AnalysisType analysis_type) const
  {
    const TimingClockUncertainty* best = nullptr;
    int best_specificity = -1;
    for (const TimingClockUncertainty& uncertainty : _clock_uncertainty_list) {
      const int specificity = uncertainty.specificity();
      if (uncertainty.matches(from_clock, from_trans_type, to_clock, to_trans_type, analysis_type) && specificity >= best_specificity) {
        best = &uncertainty;
        best_specificity = specificity;
      }
    }
    return best == nullptr ? std::nullopt : std::optional<double>(best->get_value());
  }
  double resolve_clock_uncertainty(std::string_view from_clock, TransType from_trans_type, std::string_view to_clock, TransType to_trans_type,
                                   AnalysisType analysis_type) const
  {
    return resolve_clock_uncertainty_value(from_clock, from_trans_type, to_clock, to_trans_type, analysis_type).value_or(0.0);
  }
  // function

 private:
  std::vector<TimingException> _path_exception_list;
  std::vector<TimingClockGroup> _clock_group_list;
  std::string _sdc_file_path;
  std::map<std::string, TimingClock> _clock_map;
  std::map<std::string, TimingPortConstraint> _port_constraint_map;
  std::map<std::string, TimingCaseValue> _case_analysis_map;
  std::map<std::string, TimingCaseValue> _effective_case_analysis_map;
  std::optional<double> _max_fanout;
  std::map<std::string, double> _port_max_fanout_map;
  std::map<std::string, TimingLoadConstraint> _net_load_map;
  std::vector<TimingClockUncertainty> _clock_uncertainty_list;
  std::set<std::string> _path_break_start_points;
  std::set<std::string> _path_break_end_points;
};

}  // namespace ipw
