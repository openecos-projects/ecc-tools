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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "Database.hpp"

namespace ista {
// Resolve a clock name to its SDC table id; the invalid id stands in for a
// name-probe miss, which every callee maps to the same empty result.
inline ClockId clockIdOf(const Database& database, std::string_view clock)
{
  const auto id = database.sdc().get_clock_map().id(clock);
  return id ? *id : ClockId{};
}
const TimingPoint* clockPoint(const Database& database, const std::string& pin, ClockId clock);
inline const TimingPoint* clockPoint(const Database& database, const std::string& pin, std::string_view clock)
{
  return clockPoint(database, pin, clockIdOf(database, clock));
}
const ClockWaveform* clockWaveform(const Database& database, const std::string& pin, ClockId clock, std::optional<TransType> source = std::nullopt);
inline const ClockWaveform* clockWaveform(const Database& database, const std::string& pin, std::string_view clock, std::optional<TransType> source = std::nullopt)
{
  return clockWaveform(database, pin, clockIdOf(database, clock), source);
}
std::vector<TransType> clockSourceTransitions(const Database& database, const std::string& pin, ClockId clock, AnalysisType mode,
                                              TransType local_edge);
inline std::vector<TransType> clockSourceTransitions(const Database& database, const std::string& pin, std::string_view clock, AnalysisType mode,
                                                     TransType local_edge)
{
  return clockSourceTransitions(database, pin, clockIdOf(database, clock), mode, local_edge);
}
bool isIdealClockPin(const Database& database, const std::string& pin);

// Name-ordered view of a point's clock states, reproducing the iteration order
// the string-keyed map gave. Use only where downstream order is observable.
inline std::vector<std::pair<std::string_view, const ClockState*>> clockStatesInNameOrder(const TimingPoint& point, const Database& database)
{
  std::vector<std::pair<std::string_view, const ClockState*>> states;
  for (const auto& [clock, state] : point.get_clock_state_map())
    states.emplace_back(database.sdc().get_clock_map().nameOf(clock), &state);
  std::sort(states.begin(), states.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
  return states;
}

struct ClockPinValues
{
  bool propagated = false;
  double source_latency = 0.0;
  double network_latency = 0.0;
  double uncertainty = 0.0;
  std::string propagated_pin;
};
ClockPinValues clockPinValues(const Database& database, const std::string& pin, ClockId clock, AnalysisType analysis, TransType transition,
                              AnalysisType check = AnalysisType::kNone, std::optional<TransType> source = std::nullopt, const TimingPoint* point = nullptr);
inline ClockPinValues clockPinValues(const Database& database, const std::string& pin, std::string_view clock, AnalysisType analysis, TransType transition,
                                     AnalysisType check = AnalysisType::kNone, std::optional<TransType> source = std::nullopt,
                                     const TimingPoint* point = nullptr)
{
  return clockPinValues(database, pin, clockIdOf(database, clock), analysis, transition, check, source, point);
}
// Snapshot queries are scoped to a phase with fixed topology, waveforms and
// constraints. Live references remain valid until the next values() call.
class ClockPinValuesQuery
{
 public:
  enum class CacheMode { kNone, kSnapshot };
  explicit ClockPinValuesQuery(const Database& database, CacheMode mode = CacheMode::kNone) : _database(database), _mode(mode) {}
  const ClockPinValues& values(const std::string& pin, std::string_view clock, AnalysisType analysis, TransType transition,
                               AnalysisType check = AnalysisType::kNone, std::optional<TransType> source = std::nullopt);

 private:
  using Key = std::tuple<std::string, std::string, AnalysisType, TransType, AnalysisType, std::optional<TransType>>;
  const Database& _database;
  CacheMode _mode;
  ClockPinValues _live;
  std::map<Key, ClockPinValues, std::less<>> _values;
};
// Resolve reference-pin clocks from the propagated graph on each analysis;
// keep the user's original clock-less I/O definition unchanged for write_sdc.
std::vector<TimingIoDelay> resolveIoDelays(const Database& database, const std::vector<const TimingIoDelay*>& definitions);
// Graph, clock and propagated-state lookups. Preserves existing mutable query semantics.
class TimingStateQuery
{
 public:
  explicit TimingStateQuery(Database& database) : _database(database) {}
  bool isSequentialClockPin(const std::string& pin_name);
  bool isDisableArc(Arc& arc);
  bool shouldStopDataPropagation(Arc& arc);
  double getClockArrival(const std::string& pin_name, AnalysisType analysis_type);
  double getClockArrival(const std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  double getClockArrival(const std::string& pin_name, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                         std::optional<TransType> source = std::nullopt);
  double getClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  double getClockSlew(const std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  double getClockSlew(const std::string& pin_name, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                      std::optional<TransType> source = std::nullopt);
  bool clockPathHasVariation(const std::string& pin_name, std::string_view clock_name, TransType transition,
                             std::optional<TransType> source = std::nullopt) const;
  double clockPathBound(const std::string& pin_name, std::string_view clock_name, TransType transition, std::optional<TransType> source = std::nullopt) const;
  bool canPrunePathStates() const;
  using PathStateGroup = std::tuple<TimingPathState::Group, std::vector<int32_t>, std::size_t, std::string>;
  PathStateGroup pathStateGroup(const TimingPathState& state, const std::vector<int32_t>* progress = nullptr) const;
  bool prunePathStates(TimingPoint& point, std::size_t remaining_arcs, const std::vector<std::string_view>* allowed_clocks = nullptr) const;
  double roundTime(double time);
  std::vector<TransType> getOutputTransTypeList(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  double getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  double getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type);
  double getDataArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type);
  double maxDataArcDelay(Arc& arc);
  double deratedDelay(double nominal, AnalysisType analysis, bool clock_path, TransType edge, DerateDelay kind, const std::string& pin_name) const;
  bool hasPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type);
  bool hasPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type);
  std::map<uint32_t, TimingPathState>& getPathStateMap(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type,
                                                       TransType trans_type);
  TimingPathState& getPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                                uint32_t path_state_tag);
  TimingPathState* getWorstPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type);
  TimingPathState* getWorstPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type);
  bool isBetterArrival(double candidate_arrival, double current_arrival, AnalysisType analysis_type);
  bool isFinite(double value);
  double getDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  bool isConstrainedEndPoint(const std::string& end_point);
  bool isRegisterEndPoint(const std::string& end_point);
  bool isTimingCheckEndPoint(const std::string& end_point);
  TimingClock* getStartPointClock(const std::string& start_point);
  bool hasClockPoint(const std::string& pin_name);
  TimingPathState* getWorstEndPointPathState(const std::string& end_point, AnalysisType analysis_type);

 private:
  DerateContext derateContext(const std::string& pin_name) const;
  Database& _database;

  TransType getEndPointTransType(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type);
  bool isOutputEndPoint(const std::string& end_point);
  bool hasOutputDelay(const std::string& end_point);
};
}  // namespace ista
