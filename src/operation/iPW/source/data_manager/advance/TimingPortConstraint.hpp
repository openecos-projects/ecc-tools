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
#include "TimingDrivingCell.hpp"
#include "TransType.hpp"

namespace ipw {

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
    return _analysis_type == other._analysis_type && _trans_type == other._trans_type
           && _clock_name == other._clock_name && _clock_trans_type == other._clock_trans_type
           && _reference_pin == other._reference_pin;
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

class TimingPortConstraint
{
 public:
  TimingPortConstraint() = default;
  ~TimingPortConstraint() = default;
  // getter
  struct Load
  {
    AnalysisType analysis;
    TransType transition;
    double value;
    bool wire;
  };
  std::vector<Load> get_loads() const
  {
    std::vector<Load> result;
    for (bool wire : {false, true})
      for (const auto& [analysis, transitions] : (wire ? _wire_load_map : _pin_load_map))
        for (const auto& [transition, value] : transitions)
          result.push_back({analysis, transition, value, wire});
    return result;
  }

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
  bool get_has_input_transition(AnalysisType analysis_type, TransType trans_type) const
  {
    return !get_has_driving_cell(analysis_type, trans_type) && _has_input_transition;
  }
  bool get_has_driving_cell(AnalysisType analysis_type, TransType trans_type) const
  {
    return _driving_cell_map.contains(analysis_type) && _driving_cell_map.at(analysis_type).contains(trans_type);
  }
  TimingDrivingCell* get_driving_cell(AnalysisType analysis_type, TransType trans_type)
  {
    if (!get_has_driving_cell(analysis_type, trans_type)) {
      return nullptr;
    }
    return &_driving_cell_map.at(analysis_type).at(trans_type);
  }
  bool get_has_load() const { return !_pin_load_map.empty() || !_wire_load_map.empty(); }
  double get_load(AnalysisType analysis_type, TransType trans_type) const
  {
    if (!hasLoadValue(_pin_load_map, analysis_type, trans_type)
        && !hasLoadValue(_wire_load_map, analysis_type, trans_type)) {
      return _load;
    }
    return getLoadValue(_pin_load_map, analysis_type, trans_type)
           + getLoadValue(_wire_load_map, analysis_type, trans_type);
  }
  const std::vector<TimingIoDelay>& get_input_delay_list() const { return _input_delay_list; }
  const std::vector<TimingIoDelay>& get_output_delay_list() const { return _output_delay_list; }
  void set_input_delays(const std::vector<TimingIoDelay>& delays, bool add_delay)
  {
    updateIoDelays(_input_delay_list, delays, add_delay);
  }
  void set_output_delays(const std::vector<TimingIoDelay>& delays, bool add_delay)
  {
    updateIoDelays(_output_delay_list, delays, add_delay);
  }
  // setter
  void set_input_transition(AnalysisType analysis_type, TransType trans_type, const double input_transition)
  {
    _input_transition = input_transition;
    _input_transition_map[analysis_type][trans_type] = input_transition;
    if (_driving_cell_map.contains(analysis_type)) {
      _driving_cell_map.at(analysis_type).erase(trans_type);
    }
    _has_input_transition = true;
  }
  void set_driving_cell(AnalysisType analysis_type, TransType trans_type, const TimingDrivingCell& driving_cell)
  {
    if (_input_transition_map.contains(analysis_type)) {
      _input_transition_map.at(analysis_type).erase(trans_type);
    }
    _driving_cell_map[analysis_type][trans_type] = driving_cell;
  }
  void set_load(AnalysisType analysis_type, TransType trans_type, double load, bool wire_load)
  {
    std::map<AnalysisType, std::map<TransType, double>>& load_map = wire_load ? _wire_load_map : _pin_load_map;
    load_map[analysis_type][trans_type] = load;
    _load = load;
  }
  // function

 private:
  static void updateIoDelays(
      std::vector<TimingIoDelay>& current,
      const std::vector<TimingIoDelay>& incoming,
      bool add_delay
  )
  {
    if (!add_delay) {
      std::set<std::pair<AnalysisType, TransType>> replaced;
      for (const TimingIoDelay& delay : incoming) {
        replaced.emplace(delay.get_analysis_type(), delay.get_trans_type());
      }
      current.erase(
          std::remove_if(
              current.begin(),
              current.end(),
              [&](const TimingIoDelay& delay) {
                return replaced.contains({delay.get_analysis_type(), delay.get_trans_type()});
              }
          ),
          current.end()
      );
    }

    for (const TimingIoDelay& delay : incoming) {
      auto existing = std::find_if(current.begin(), current.end(), [&](const TimingIoDelay& item) {
        return item.hasSameSource(delay);
      });
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

  std::vector<TimingIoDelay> _input_delay_list;
  std::vector<TimingIoDelay> _output_delay_list;

  static double getLoadValue(
      const std::map<AnalysisType, std::map<TransType, double>>& load_map,
      AnalysisType analysis_type,
      TransType trans_type
  )
  {
    const auto analysis = load_map.find(analysis_type);
    if (analysis == load_map.end()) {
      return 0.0;
    }
    const auto transition = analysis->second.find(trans_type);
    return transition == analysis->second.end() ? 0.0 : transition->second;
  }

  static bool hasLoadValue(
      const std::map<AnalysisType, std::map<TransType, double>>& load_map,
      AnalysisType analysis_type,
      TransType trans_type
  )
  {
    const auto analysis = load_map.find(analysis_type);
    return analysis != load_map.end() && analysis->second.contains(trans_type);
  }

  double _input_transition = 0.0;
  std::map<AnalysisType, std::map<TransType, double>> _input_transition_map;
  std::map<AnalysisType, std::map<TransType, TimingDrivingCell>> _driving_cell_map;
  std::map<AnalysisType, std::map<TransType, double>> _pin_load_map;
  std::map<AnalysisType, std::map<TransType, double>> _wire_load_map;
  double _load = 0.0;
  bool _has_input_transition = false;
};

}  // namespace ipw
