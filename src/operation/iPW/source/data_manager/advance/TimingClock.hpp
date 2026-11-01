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

enum class ClockSourceKind
{
  kPort,
  kPin,
  kNet
};

struct GeneratedClockDefinition
{
  std::optional<std::string> master_clock;
  std::string master_source;
  ClockSourceKind master_source_kind = ClockSourceKind::kPort;
  std::vector<ClockSourceKind> target_kinds;
  std::vector<std::string> targets;
  std::optional<double> divide_by;
  std::optional<double> multiply_by;
  std::optional<double> duty_cycle;
  std::optional<std::vector<double>> edges;
  std::optional<std::vector<double>> edge_shifts;
  bool invert = false;
  bool preinvert = false;
  bool combinational = false;
  bool add = false;
};

enum class ClockVariation
{
  kEarly,
  kLate
};
constexpr ClockVariation clockVariation(AnalysisType analysis)
{
  return analysis == AnalysisType::kMin ? ClockVariation::kEarly : ClockVariation::kLate;
}
using ClockSourceLatency = std::map<AnalysisType, std::map<ClockVariation, std::map<TransType, double>>>;

// Explicit pin/port annotations; an empty clock selector in Sdc means all clocks.
struct ClockPinConstraint
{
  std::optional<bool> propagated;
  ClockSourceLatency source_latency;
  std::map<AnalysisType, std::map<TransType, double>> network_latency;
  std::optional<double> setup_uncertainty, hold_uncertainty;
};

class TimingClock
{
 public:
  TimingClock() = default;
  ~TimingClock() = default;
  // getter
  const auto& get_generated_clock_definition() const { return _generated_clock_definition; }
  const auto& get_waveform() const { return _waveform; }
  const auto& get_comment() const { return _comment; }
  const auto& get_source_latency_map() const { return _source_latency_map; }
  const auto& get_network_latency_map() const { return _network_latency_map; }
  void set_generated_clock_definition(GeneratedClockDefinition value)
  {
    _generated_clock_definition = std::move(value);
  }
  void set_waveform(std::vector<double> value) { _waveform = std::move(value); }
  void set_comment(std::string value) { _comment = std::move(value); }

  std::vector<std::string>& get_source_list() { return _source_list; }
  const std::vector<std::string>& get_source_list() const { return _source_list; }
  double get_period() const { return _period; }
  double get_rise_edge() const { return _rise_edge; }
  double get_fall_edge() const { return _fall_edge; }
  bool get_is_propagated() const { return _is_propagated; }
  const std::string& get_master_clock_name() const { return _master_clock_name; }
  const std::string& get_master_source() const { return _master_source; }
  bool get_is_generated() const { return !_master_clock_name.empty(); }
  bool get_is_generated_combinational() const { return _is_generated_combinational; }
  std::map<AnalysisType, std::map<TransType, double>>& get_transition_map() { return _transition_map; }
  double get_source_latency(AnalysisType analysis_type, ClockVariation variation, TransType trans_type) const
  {
    const auto analysis = _source_latency_map.find(analysis_type);
    if (analysis == _source_latency_map.end())
      return 0.0;
    const auto selected = analysis->second.find(variation);
    if (selected == analysis->second.end())
      return 0.0;
    const auto edge = selected->second.find(trans_type);
    return edge == selected->second.end() ? 0.0 : edge->second;
  }
  double get_source_latency(AnalysisType analysis, TransType edge) const
  {
    return get_source_latency(analysis, clockVariation(analysis), edge);
  }
  double get_network_latency(AnalysisType analysis_type, TransType trans_type) const
  {
    const auto analysis = _network_latency_map.find(analysis_type);
    if (analysis == _network_latency_map.end())
      return 0.0;
    const auto transition = analysis->second.find(trans_type);
    return transition == analysis->second.end() ? 0.0 : transition->second;
  }
  double get_setup_uncertainty() const { return _setup_uncertainty; }
  double get_hold_uncertainty() const { return _hold_uncertainty; }
  // setter
  void set_source_list(const std::vector<std::string>& source_list) { _source_list = source_list; }
  void set_period(const double period) { _period = period; }
  void set_rise_edge(const double rise_edge) { _rise_edge = rise_edge; }
  void set_fall_edge(const double fall_edge) { _fall_edge = fall_edge; }
  void set_is_propagated(const bool is_propagated) { _is_propagated = is_propagated; }
  void set_master_clock_name(const std::string& name) { _master_clock_name = name; }
  void set_master_source(const std::string& source) { _master_source = source; }
  void set_is_generated_combinational(const bool is_generated_combinational)
  {
    _is_generated_combinational = is_generated_combinational;
  }
  void set_source_latency(AnalysisType analysis_type, ClockVariation variation, TransType trans_type, double latency)
  {
    _source_latency_map[analysis_type][variation][trans_type] = latency;
  }
  void set_network_latency(AnalysisType analysis_type, TransType trans_type, double latency)
  {
    _network_latency_map[analysis_type][trans_type] = latency;
  }
  void clear_source_latency() { _source_latency_map.clear(); }
  void clear_network_latency() { _network_latency_map.clear(); }
  void set_setup_uncertainty(double value) { _setup_uncertainty = value; }
  void set_hold_uncertainty(double value) { _hold_uncertainty = value; }
  // function

 private:
  std::optional<GeneratedClockDefinition> _generated_clock_definition;
  std::vector<double> _waveform;
  std::string _comment;
  double _setup_uncertainty = 0.0;
  double _hold_uncertainty = 0.0;
  ClockSourceLatency _source_latency_map;
  std::map<AnalysisType, std::map<TransType, double>> _network_latency_map;
  std::string _master_clock_name;
  std::string _master_source;
  std::vector<std::string> _source_list;
  double _period = 0.0;
  double _rise_edge = 0.0;
  double _fall_edge = 0.0;
  bool _is_propagated = false;
  bool _is_generated_combinational = false;
  std::map<AnalysisType, std::map<TransType, double>> _transition_map;
};

}  // namespace ipw
