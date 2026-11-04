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

#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "Database.hpp"

namespace ista {
const TimingPoint* clockPoint(const Database& database, const std::string& pin, std::string_view clock);
const ClockWaveform* clockWaveform(const Database& database, const std::string& pin, std::string_view clock, std::optional<TransType> source = std::nullopt);
std::vector<TransType> clockSourceTransitions(const Database& database, const std::string& pin, std::string_view clock, AnalysisType mode,
                                              TransType local_edge);
bool isIdealClockPin(const Database& database, const std::string& pin);

struct ClockPinValues
{
  bool propagated = false;
  double source_latency = 0.0;
  double network_latency = 0.0;
  double uncertainty = 0.0;
  std::string propagated_pin;
};
ClockPinValues clockPinValues(const Database& database, const std::string& pin, std::string_view clock, AnalysisType analysis, TransType transition,
                              AnalysisType check = AnalysisType::kNone, std::optional<TransType> source = std::nullopt, const TimingPoint* point = nullptr);
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
  std::map<std::string, TimingPathState>& getPathStateMap(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type,
                                                          TransType trans_type);
  TimingPathState& getPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                                const std::string& path_state_tag);
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
