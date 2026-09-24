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

class TimingPortConstraint
{
 public:
  TimingPortConstraint() = default;
  ~TimingPortConstraint() = default;
  // getter
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
    if (!hasLoadValue(_pin_load_map, analysis_type, trans_type) && !hasLoadValue(_wire_load_map, analysis_type, trans_type)) {
      return _load;
    }
    return getLoadValue(_pin_load_map, analysis_type, trans_type) + getLoadValue(_wire_load_map, analysis_type, trans_type);
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

  double _input_transition = 0.0;
  std::map<AnalysisType, std::map<TransType, double>> _input_transition_map;
  std::map<AnalysisType, std::map<TransType, TimingDrivingCell>> _driving_cell_map;
  std::map<AnalysisType, std::map<TransType, double>> _pin_load_map;
  std::map<AnalysisType, std::map<TransType, double>> _wire_load_map;
  double _load = 0.0;
  bool _has_input_transition = false;
};

}  // namespace ipw
