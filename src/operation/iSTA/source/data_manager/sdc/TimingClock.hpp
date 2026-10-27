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

#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "common/AnalysisType.hpp"
#include "common/TransType.hpp"

namespace ista {

// Identity retained for clients which read constraints before the design.
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

struct ClockEdge
{
  std::size_t index;
  TransType transition;
  double time;
};

class TimingClock
{
 public:
  TimingClock() = default;
  ~TimingClock() = default;
  const std::vector<std::string>& get_source_objects() const { return _has_source_objects ? _source_objects : _source_list; }
  void set_source_objects(std::vector<std::string> objects)
  {
    _source_objects = std::move(objects);
    _has_source_objects = true;
  }
  const std::vector<ClockSourceKind>& get_source_kinds() const { return _source_kinds; }
  void set_source_kinds(std::vector<ClockSourceKind> kinds) { _source_kinds = std::move(kinds); }
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
  std::size_t edgeCount() const { return _waveform.empty() ? 2 : _waveform.size(); }
  ClockEdge edge(std::size_t index) const
  {
    if (index >= edgeCount())
      throw std::out_of_range("clock edge index");
    return {index, index % 2 == 0 ? TransType::kRise : TransType::kFall, _waveform.empty() ? (index == 0 ? _rise_edge : _fall_edge) : _waveform[index]};
  }
  const std::string& get_comment() const { return _comment; }
  bool get_is_generated() const { return !_master_clock_name.empty(); }
  const std::optional<GeneratedClockDefinition>& get_generated_clock_definition() const { return _generated_clock_definition; }
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
  double get_network_latency(AnalysisType analysis_type, TransType trans_type) const
  {
    const auto analysis = _network_latency_map.find(analysis_type);
    if (analysis == _network_latency_map.end())
      return 0.0;
    const auto transition = analysis->second.find(trans_type);
    return transition == analysis->second.end() ? 0.0 : transition->second;
  }
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
  void set_waveform(std::vector<double> waveform)
  {
    if (!waveform.empty()) {
      if (waveform.size() < 2 || waveform.size() % 2 != 0)
        throw std::invalid_argument("clock waveform must contain rise/fall pairs");
      _rise_edge = waveform[0];
      _fall_edge = waveform[1];
    }
    _waveform = std::move(waveform);
  }
  void set_comment(std::string comment) { _comment = std::move(comment); }
  void set_source_latency(AnalysisType analysis_type, ClockVariation variation, TransType trans_type, double latency)
  {
    _source_latency_map[analysis_type][variation][trans_type] = latency;
  }
  void set_network_latency(AnalysisType analysis_type, TransType trans_type, double latency) { _network_latency_map[analysis_type][trans_type] = latency; }
  const ClockSourceLatency& get_source_latency_map() const { return _source_latency_map; }
  const std::map<AnalysisType, std::map<TransType, double>>& get_network_latency_map() const { return _network_latency_map; }
  void clear_source_latency() { _source_latency_map.clear(); }
  void clear_network_latency() { _network_latency_map.clear(); }
  // Preserve identity-bound annotations when a clock definition changes.
  void copyAnnotations(const TimingClock& other)
  {
    _setup_uncertainty = other._setup_uncertainty;
    _hold_uncertainty = other._hold_uncertainty;
    _is_propagated = other._is_propagated;
    _transition_map = other._transition_map;
    _source_latency_map = other._source_latency_map;
    _network_latency_map = other._network_latency_map;
  }

 private:
  std::vector<ClockSourceKind> _source_kinds;
  bool _has_source_objects = false;
  std::vector<std::string> _source_objects;
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
  ClockSourceLatency _source_latency_map;
  std::map<AnalysisType, std::map<TransType, double>> _network_latency_map;
  std::vector<double> _waveform;
  std::string _comment;
};

}  // namespace ista
