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

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "common/AnalysisType.hpp"
#include "common/TransType.hpp"

namespace ista {

enum class TimingInputDriveType
{
  kNone,
  kInputTransition,
  kDrivingCell
};

class TimingIoDelay
{
 public:
  [[nodiscard]] const std::string& get_clock_name() const { return _clock_name; }
  [[nodiscard]] const std::string& get_reference_pin() const { return _reference_pin; }
  [[nodiscard]] const std::string& get_group_path() const { return _group_path; }
  [[nodiscard]] AnalysisType get_analysis_type() const { return _analysis_type; }
  [[nodiscard]] TransType get_trans_type() const { return _trans_type; }
  [[nodiscard]] TransType get_clock_trans_type() const { return _clock_trans_type; }
  [[nodiscard]] double get_delay() const { return _delay; }
  [[nodiscard]] bool get_level_sensitive() const { return _level_sensitive; }
  [[nodiscard]] bool get_network_latency_included() const { return _network_latency_included; }
  [[nodiscard]] bool get_source_latency_included() const { return _source_latency_included; }

  void set_clock_name(std::string value) { _clock_name = std::move(value); }
  void set_reference_pin(std::string value) { _reference_pin = std::move(value); }
  void set_group_path(std::string value) { _group_path = std::move(value); }
  void set_analysis_type(AnalysisType value) { _analysis_type = value; }
  void set_trans_type(TransType value) { _trans_type = value; }
  void set_clock_trans_type(TransType value) { _clock_trans_type = value; }
  void set_delay(double value) { _delay = value; }
  void set_level_sensitive(bool value) { _level_sensitive = value; }
  void set_network_latency_included(bool value) { _network_latency_included = value; }
  void set_source_latency_included(bool value) { _source_latency_included = value; }

  [[nodiscard]] bool hasSameSource(const TimingIoDelay& other) const
  {
    return _analysis_type == other._analysis_type && _trans_type == other._trans_type && _clock_name == other._clock_name
           && _clock_trans_type == other._clock_trans_type && _reference_pin == other._reference_pin;
  }

 private:
  std::string _clock_name;
  std::string _reference_pin;
  std::string _group_path;
  AnalysisType _analysis_type = AnalysisType::kMax;
  TransType _trans_type = TransType::kRise;
  TransType _clock_trans_type = TransType::kRise;
  double _delay = 0.0;
  bool _level_sensitive = false;
  bool _network_latency_included = false;
  bool _source_latency_included = false;
};

class TimingDrivingCell
{
 public:
  TimingDrivingCell() = default;
  ~TimingDrivingCell() = default;
  // getter
  const std::string& get_library_name() const { return _library_name; }
  const std::string& get_cell_name() const { return _cell_name; }
  const std::string& get_from_pin() const { return _from_pin; }
  const std::string& get_to_pin() const { return _to_pin; }
  double get_input_transition_rise() const { return _input_transition_rise; }
  double get_input_transition_fall() const { return _input_transition_fall; }
  bool get_no_design_rule() const { return _no_design_rule; }
  void set_no_design_rule(bool value) { _no_design_rule = value; }
  // setter
  void set_library_name(const std::string& library_name) { _library_name = library_name; }
  void set_cell_name(const std::string& cell_name) { _cell_name = cell_name; }
  void set_from_pin(const std::string& from_pin) { _from_pin = from_pin; }
  void set_to_pin(const std::string& to_pin) { _to_pin = to_pin; }
  void set_input_transition_rise(const double input_transition_rise) { _input_transition_rise = input_transition_rise; }
  void set_input_transition_fall(const double input_transition_fall) { _input_transition_fall = input_transition_fall; }
  // function

 private:
  bool _no_design_rule = false;
  std::string _library_name;
  std::string _cell_name;
  std::string _from_pin;
  std::string _to_pin;
  double _input_transition_rise = 0.0;
  double _input_transition_fall = 0.0;
};

// Latest assignments in write order. There is at most one record per
// (analysis, transition, kind). Order preserves the existing unspecified-edge
// fallback without a second mutable scalar copy.
struct TimingPortLoad
{
  AnalysisType analysis;
  TransType transition;
  double value;
  bool wire;
};

class TimingPortConstraint
{
 public:
  TimingPortConstraint() = default;
  ~TimingPortConstraint() = default;
  // getter
  std::string& get_port_name() { return _port_name; }
  const std::string& inputClock() const { return firstClock(_input_delay_list); }
  const std::string& outputClock() const { return firstClock(_output_delay_list); }
  double get_input_delay_max() const { return delaySummary(_input_delay_list, AnalysisType::kMax).value_or(0.0); }
  double get_input_delay_min() const { return delaySummary(_input_delay_list, AnalysisType::kMin).value_or(0.0); }
  double get_output_delay_max() const { return delaySummary(_output_delay_list, AnalysisType::kMax).value_or(0.0); }
  double get_output_delay_min() const { return delaySummary(_output_delay_list, AnalysisType::kMin).value_or(0.0); }
  double get_input_transition() const { return _input_transition; }
  double get_input_transition(AnalysisType analysis_type, TransType trans_type) const
  {
    const auto analysis_iter = _input_transition_map.find(analysis_type);
    if (analysis_iter != _input_transition_map.end()) {
      const auto trans_iter = analysis_iter->second.find(trans_type);
      if (trans_iter != analysis_iter->second.end()) {
        return trans_iter->second;
      }
    }
    return 0.0;
  }
  bool get_has_input_delay_max() const { return delaySummary(_input_delay_list, AnalysisType::kMax).has_value(); }
  bool get_has_input_delay_min() const { return delaySummary(_input_delay_list, AnalysisType::kMin).has_value(); }
  bool get_has_output_delay_max() const { return delaySummary(_output_delay_list, AnalysisType::kMax).has_value(); }
  bool get_has_output_delay_min() const { return delaySummary(_output_delay_list, AnalysisType::kMin).has_value(); }
  bool get_has_input_transition() const { return _has_input_transition; }
  bool get_has_input_transition(AnalysisType analysis_type, TransType trans_type) const
  {
    return get_input_drive_type(analysis_type, trans_type) == TimingInputDriveType::kInputTransition;
  }
  bool get_has_driving_cell(AnalysisType analysis_type, TransType trans_type) const
  {
    return get_input_drive_type(analysis_type, trans_type) == TimingInputDriveType::kDrivingCell;
  }
  TimingDrivingCell* get_driving_cell(AnalysisType analysis_type, TransType trans_type)
  {
    if (get_input_drive_type(analysis_type, trans_type) != TimingInputDriveType::kDrivingCell || !_driving_cell_map.contains(analysis_type)
        || !_driving_cell_map.at(analysis_type).contains(trans_type)) {
      return nullptr;
    }
    return &_driving_cell_map.at(analysis_type).at(trans_type);
  }
  const std::map<AnalysisType, std::map<TransType, TimingDrivingCell>>& drivingCells() const { return _driving_cell_map; }
  bool get_has_load() const { return !_loads.empty(); }
  void removeClock(const std::string& name)
  {
    const auto references = [&](const auto& delay) { return delay.get_clock_name() == name; };
    std::erase_if(_input_delay_list, references);
    std::erase_if(_output_delay_list, references);
  }
  const std::vector<TimingIoDelay>& get_input_delay_list() const { return _input_delay_list; }
  const std::vector<TimingIoDelay>& get_output_delay_list() const { return _output_delay_list; }
  std::vector<const TimingIoDelay*> get_input_delays(AnalysisType analysis_type, TransType trans_type) const
  {
    return getIoDelays(_input_delay_list, analysis_type, trans_type);
  }
  std::vector<const TimingIoDelay*> get_output_delays(AnalysisType analysis_type, TransType trans_type) const
  {
    return getIoDelays(_output_delay_list, analysis_type, trans_type);
  }
  double get_load(AnalysisType analysis_type, TransType trans_type) const
  {
    std::optional<double> value;
    for (const auto& load : _loads) {
      if (load.analysis == analysis_type && load.transition == trans_type)
        value = value.value_or(0.0) + load.value;
    }
    return value.value_or(_loads.empty() ? 0.0 : _loads.back().value);
  }

  // setter
  void set_port_name(const std::string& port_name) { _port_name = port_name; }
  const std::vector<TimingPortLoad>& get_loads() const { return _loads; }
  void set_input_transition(const double input_transition)
  {
    _input_transition = input_transition;
    for (AnalysisType analysis_type : {AnalysisType::kMin, AnalysisType::kMax}) {
      for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
        set_input_transition(analysis_type, trans_type, input_transition);
      }
    }
  }
  void set_input_transition(AnalysisType analysis_type, TransType trans_type, const double input_transition)
  {
    // Setting transition removes the driving-cell model for the entire port,
    // while preserving other explicitly assigned transition values.
    _driving_cell_map.clear();
    for (auto& [mode, edges] : _input_drive_type_map)
      std::erase_if(edges, [](const auto& value) { return value.second == TimingInputDriveType::kDrivingCell; });
    _input_transition = input_transition;
    _input_transition_map[analysis_type][trans_type] = input_transition;
    _input_drive_type_map[analysis_type][trans_type] = TimingInputDriveType::kInputTransition;
    _has_input_transition = true;
  }
  void set_driving_cell(AnalysisType analysis_type, TransType trans_type, const TimingDrivingCell& driving_cell)
  {
    _driving_cell_map[analysis_type][trans_type] = driving_cell;
    // no_design_rule is port-wide even when delay/slew selection is scoped.
    for (auto& [mode, edges] : _driving_cell_map)
      for (auto& [edge, driver] : edges)
        driver.set_no_design_rule(driving_cell.get_no_design_rule());
    _input_drive_type_map[analysis_type][trans_type] = TimingInputDriveType::kDrivingCell;
    _input_transition_map[analysis_type].erase(trans_type);
    _has_input_transition = false;
    for (const auto& [mode, edges] : _input_transition_map)
      _has_input_transition |= !edges.empty();
  }
  void set_has_input_transition(const bool has_input_transition) { _has_input_transition = has_input_transition; }
  void set_input_delays(const std::vector<TimingIoDelay>& delays, bool add_delay) { updateIoDelays(_input_delay_list, delays, add_delay); }
  void set_output_delays(const std::vector<TimingIoDelay>& delays, bool add_delay) { updateIoDelays(_output_delay_list, delays, add_delay); }
  void set_load(AnalysisType analysis_type, TransType trans_type, double load, bool wire_load)
  {
    std::erase_if(_loads, [&](const auto& entry) { return entry.analysis == analysis_type && entry.transition == trans_type && entry.wire == wire_load; });
    _loads.push_back({analysis_type, trans_type, load, wire_load});
  }

  // function

 private:
  static std::vector<const TimingIoDelay*> getIoDelays(const std::vector<TimingIoDelay>& delays, AnalysisType analysis_type, TransType trans_type)
  {
    std::vector<const TimingIoDelay*> result;
    for (const TimingIoDelay& delay : delays) {
      if (delay.get_analysis_type() == analysis_type && delay.get_trans_type() == trans_type) {
        result.push_back(&delay);
      }
    }
    return result;
  }

  static void updateIoDelays(std::vector<TimingIoDelay>& current, const std::vector<TimingIoDelay>& incoming, bool add_delay)
  {
    if (!add_delay) {
      std::set<std::pair<AnalysisType, TransType>> replaced;
      for (const TimingIoDelay& delay : incoming) {
        replaced.emplace(delay.get_analysis_type(), delay.get_trans_type());
      }
      current.erase(std::remove_if(current.begin(), current.end(),
                                   [&](const TimingIoDelay& delay) { return replaced.contains({delay.get_analysis_type(), delay.get_trans_type()}); }),
                    current.end());
    }

    for (const TimingIoDelay& delay : incoming) {
      auto existing = std::find_if(current.begin(), current.end(), [&](const TimingIoDelay& item) { return item.hasSameSource(delay); });
      if (existing == current.end()) {
        current.push_back(delay);
        continue;
      }
      if (!add_delay || (delay.get_analysis_type() == AnalysisType::kMax && delay.get_delay() > existing->get_delay())
          || (delay.get_analysis_type() == AnalysisType::kMin && delay.get_delay() < existing->get_delay())) {
        *existing = delay;
      }
    }
  }

  static std::optional<double> delaySummary(const std::vector<TimingIoDelay>& delays, AnalysisType analysis)
  {
    std::optional<double> value;
    for (const auto& delay : delays) {
      if (delay.get_analysis_type() == analysis && (!value || (analysis == AnalysisType::kMin ? delay.get_delay() < *value : delay.get_delay() > *value)))
        value = delay.get_delay();
    }
    return value;
  }

  static const std::string& firstClock(const std::vector<TimingIoDelay>& delays)
  {
    for (const auto& delay : delays)
      if (!delay.get_clock_name().empty())
        return delay.get_clock_name();
    static const std::string empty;
    return empty;
  }

  TimingInputDriveType get_input_drive_type(AnalysisType analysis_type, TransType trans_type) const
  {
    const auto analysis_iter = _input_drive_type_map.find(analysis_type);
    if (analysis_iter != _input_drive_type_map.end()) {
      const auto trans_iter = analysis_iter->second.find(trans_type);
      if (trans_iter != analysis_iter->second.end()) {
        return trans_iter->second;
      }
    }
    return TimingInputDriveType::kNone;
  }

  std::string _port_name;
  double _input_transition = 0.0;
  std::map<AnalysisType, std::map<TransType, double>> _input_transition_map;
  std::map<AnalysisType, std::map<TransType, TimingInputDriveType>> _input_drive_type_map;
  std::map<AnalysisType, std::map<TransType, TimingDrivingCell>> _driving_cell_map;
  std::vector<TimingIoDelay> _input_delay_list;
  std::vector<TimingIoDelay> _output_delay_list;
  std::vector<TimingPortLoad> _loads;
  bool _has_input_transition = false;
};

}  // namespace ista
