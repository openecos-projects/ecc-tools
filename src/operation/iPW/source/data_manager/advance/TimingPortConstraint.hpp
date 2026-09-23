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
#include "PWHeader.hpp"
#include "TransType.hpp"

namespace ipw {

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
  std::string& get_library_name() { return _library_name; }
  std::string& get_cell_name() { return _cell_name; }
  std::string& get_from_pin() { return _from_pin; }
  std::string& get_to_pin() { return _to_pin; }
  double get_input_transition_rise() const { return _input_transition_rise; }
  double get_input_transition_fall() const { return _input_transition_fall; }
  // setter
  void set_library_name(const std::string& library_name) { _library_name = library_name; }
  void set_cell_name(const std::string& cell_name) { _cell_name = cell_name; }
  void set_from_pin(const std::string& from_pin) { _from_pin = from_pin; }
  void set_to_pin(const std::string& to_pin) { _to_pin = to_pin; }
  void set_input_transition_rise(const double input_transition_rise) { _input_transition_rise = input_transition_rise; }
  void set_input_transition_fall(const double input_transition_fall) { _input_transition_fall = input_transition_fall; }
  // function

 private:
  std::string _library_name;
  std::string _cell_name;
  std::string _from_pin;
  std::string _to_pin;
  double _input_transition_rise = 0.0;
  double _input_transition_fall = 0.0;
};

class TimingPortConstraint
{
 public:
  TimingPortConstraint() = default;
  ~TimingPortConstraint() = default;
  // getter
  std::string& get_port_name() { return _port_name; }
  std::string& get_clock_name() { return _clock_name; }
  double get_input_delay_max() const { return _input_delay_max; }
  double get_input_delay_min() const { return _input_delay_min; }
  double get_output_delay_max() const { return _output_delay_max; }
  double get_output_delay_min() const { return _output_delay_min; }
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
    return _input_transition;
  }
  double get_load() const { return _load; }
  bool get_has_input_delay_max() const { return _has_input_delay_max; }
  bool get_has_input_delay_min() const { return _has_input_delay_min; }
  bool get_has_output_delay_max() const { return _has_output_delay_max; }
  bool get_has_output_delay_min() const { return _has_output_delay_min; }
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
  bool get_has_load() const { return _has_load; }
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
    if (!hasLoadValue(_pin_load_map, analysis_type, trans_type) && !hasLoadValue(_wire_load_map, analysis_type, trans_type)) {
      return _load;
    }
    return getLoadValue(_pin_load_map, analysis_type, trans_type) + getLoadValue(_wire_load_map, analysis_type, trans_type);
  }
  // setter
  void set_port_name(const std::string& port_name) { _port_name = port_name; }
  void set_clock_name(const std::string& clock_name) { _clock_name = clock_name; }
  void set_input_delay_max(const double input_delay_max) { _input_delay_max = input_delay_max; }
  void set_input_delay_min(const double input_delay_min) { _input_delay_min = input_delay_min; }
  void set_output_delay_max(const double output_delay_max) { _output_delay_max = output_delay_max; }
  void set_output_delay_min(const double output_delay_min) { _output_delay_min = output_delay_min; }
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
    _input_transition = input_transition;
    _input_transition_map[analysis_type][trans_type] = input_transition;
    _input_drive_type_map[analysis_type][trans_type] = TimingInputDriveType::kInputTransition;
    _has_input_transition = true;
  }
  void set_driving_cell(AnalysisType analysis_type, TransType trans_type, const TimingDrivingCell& driving_cell)
  {
    _driving_cell_map[analysis_type][trans_type] = driving_cell;
    _input_drive_type_map[analysis_type][trans_type] = TimingInputDriveType::kDrivingCell;
  }
  void set_load(const double load) { _load = load; }
  void set_has_input_delay_max(const bool has_input_delay_max) { _has_input_delay_max = has_input_delay_max; }
  void set_has_input_delay_min(const bool has_input_delay_min) { _has_input_delay_min = has_input_delay_min; }
  void set_has_output_delay_max(const bool has_output_delay_max) { _has_output_delay_max = has_output_delay_max; }
  void set_has_output_delay_min(const bool has_output_delay_min) { _has_output_delay_min = has_output_delay_min; }
  void set_has_input_transition(const bool has_input_transition) { _has_input_transition = has_input_transition; }
  void set_has_load(const bool has_load) { _has_load = has_load; }
  void set_input_delays(const std::vector<TimingIoDelay>& delays, bool add_delay)
  {
    updateIoDelays(_input_delay_list, delays, add_delay);
    rebuildLegacyIoDelay(true);
  }
  void set_output_delays(const std::vector<TimingIoDelay>& delays, bool add_delay)
  {
    updateIoDelays(_output_delay_list, delays, add_delay);
    rebuildLegacyIoDelay(false);
  }
  void set_load(AnalysisType analysis_type, TransType trans_type, double load, bool wire_load)
  {
    auto& load_map = wire_load ? _wire_load_map : _pin_load_map;
    load_map[analysis_type][trans_type] = load;
    _load = load;
    _has_load = true;
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
      current.erase(std::remove_if(current.begin(), current.end(), [&](const TimingIoDelay& delay) {
                      return replaced.contains({delay.get_analysis_type(), delay.get_trans_type()});
                    }),
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

  void rebuildLegacyIoDelay(bool input)
  {
    const std::vector<TimingIoDelay>& delays = input ? _input_delay_list : _output_delay_list;
    std::optional<double> minimum;
    std::optional<double> maximum;
    _clock_name.clear();
    for (const TimingIoDelay& delay : delays) {
      if (_clock_name.empty() && !delay.get_clock_name().empty()) {
        _clock_name = delay.get_clock_name();
      }
      std::optional<double>& value = delay.get_analysis_type() == AnalysisType::kMin ? minimum : maximum;
      if (!value || (delay.get_analysis_type() == AnalysisType::kMin ? delay.get_delay() < *value : delay.get_delay() > *value)) {
        value = delay.get_delay();
      }
    }
    if (input) {
      _has_input_delay_min = minimum.has_value();
      _has_input_delay_max = maximum.has_value();
      _input_delay_min = minimum.value_or(0.0);
      _input_delay_max = maximum.value_or(0.0);
    } else {
      _has_output_delay_min = minimum.has_value();
      _has_output_delay_max = maximum.has_value();
      _output_delay_min = minimum.value_or(0.0);
      _output_delay_max = maximum.value_or(0.0);
    }
  }

  static double getLoadValue(const std::map<AnalysisType, std::map<TransType, double>>& load_map, AnalysisType analysis_type, TransType trans_type)
  {
    const auto analysis = load_map.find(analysis_type);
    if (analysis == load_map.end()) {
      return 0.0;
    }
    const auto transition = analysis->second.find(trans_type);
    return transition == analysis->second.end() ? 0.0 : transition->second;
  }

  static bool hasLoadValue(const std::map<AnalysisType, std::map<TransType, double>>& load_map, AnalysisType analysis_type, TransType trans_type)
  {
    const auto analysis = load_map.find(analysis_type);
    return analysis != load_map.end() && analysis->second.contains(trans_type);
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
    return _has_input_transition ? TimingInputDriveType::kInputTransition : TimingInputDriveType::kNone;
  }

  std::string _port_name;
  std::string _clock_name;
  double _input_delay_max = 0.0;
  double _input_delay_min = 0.0;
  double _output_delay_max = 0.0;
  double _output_delay_min = 0.0;
  double _input_transition = 0.0;
  std::map<AnalysisType, std::map<TransType, double>> _input_transition_map;
  std::map<AnalysisType, std::map<TransType, TimingInputDriveType>> _input_drive_type_map;
  std::map<AnalysisType, std::map<TransType, TimingDrivingCell>> _driving_cell_map;
  std::vector<TimingIoDelay> _input_delay_list;
  std::vector<TimingIoDelay> _output_delay_list;
  std::map<AnalysisType, std::map<TransType, double>> _pin_load_map;
  std::map<AnalysisType, std::map<TransType, double>> _wire_load_map;
  double _load = 0.0;
  bool _has_input_delay_max = false;
  bool _has_input_delay_min = false;
  bool _has_output_delay_max = false;
  bool _has_output_delay_min = false;
  bool _has_input_transition = false;
  bool _has_load = false;
};

}  // namespace ipw
