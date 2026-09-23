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

struct GeneratedClockDefinition
{
  std::string master_clock;
  std::string master_source;
  std::vector<std::string> targets;
  std::optional<double> divide_by;
  std::optional<double> multiply_by;
  std::optional<double> duty_cycle;
  std::vector<double> edges;
  std::vector<double> edge_shifts;
  bool invert = false;
  bool preinvert = false;
  bool combinational = false;
  bool add = false;
};

class TimingClock
{
 public:
  TimingClock() = default;
  ~TimingClock() = default;
  // getter
  std::string& get_clock_name() { return _clock_name; }
  std::vector<std::string>& get_source_list() { return _source_list; }
  const std::vector<std::string>& get_source_list() const { return _source_list; }
  double get_period() const { return _period; }
  double get_rise_edge() const { return _rise_edge; }
  double get_fall_edge() const { return _fall_edge; }
  double get_setup_uncertainty() const { return _setup_uncertainty; }
  double get_hold_uncertainty() const { return _hold_uncertainty; }
  bool get_is_propagated() const { return _is_propagated; }
  const std::string& get_master_clock_name() const { return _master_clock_name; }
  const std::string& get_master_source() const { return _master_source; }
  const std::vector<double>& get_waveform() const { return _waveform; }
  const std::string& get_comment() const { return _comment; }
  bool get_is_generated() const { return !_master_clock_name.empty(); }
  const std::optional<GeneratedClockDefinition>& get_generated_clock_definition() const { return _generated_clock_definition; }
  std::map<AnalysisType, std::map<TransType, double>>& get_transition_map() { return _transition_map; }
  // setter
  void set_clock_name(const std::string& clock_name) { _clock_name = clock_name; }
  void set_source_list(const std::vector<std::string>& source_list) { _source_list = source_list; }
  void set_period(const double period) { _period = period; }
  void set_rise_edge(const double rise_edge) { _rise_edge = rise_edge; }
  void set_fall_edge(const double fall_edge) { _fall_edge = fall_edge; }
  void set_setup_uncertainty(const double uncertainty) { _setup_uncertainty = uncertainty; }
  void set_hold_uncertainty(const double uncertainty) { _hold_uncertainty = uncertainty; }
  void set_is_propagated(const bool is_propagated) { _is_propagated = is_propagated; }
  void set_master_clock_name(const std::string& name) { _master_clock_name = name; }
  void set_master_source(const std::string& source) { _master_source = source; }
  void set_generated_clock_definition(GeneratedClockDefinition definition) { _generated_clock_definition = std::move(definition); }
  void set_waveform(std::vector<double> waveform) { _waveform = std::move(waveform); }
  void set_comment(std::string comment) { _comment = std::move(comment); }
  // function

 private:
  std::string _master_clock_name;
  std::string _master_source;
  std::optional<GeneratedClockDefinition> _generated_clock_definition;
  std::string _clock_name;
  std::vector<std::string> _source_list;
  double _period = 0.0;
  double _rise_edge = 0.0;
  double _fall_edge = 0.0;
  double _setup_uncertainty = 0.0;
  double _hold_uncertainty = 0.0;
  bool _is_propagated = false;
  std::map<AnalysisType, std::map<TransType, double>> _transition_map;
  std::vector<double> _waveform;
  std::string _comment;
};

}  // namespace ipw
