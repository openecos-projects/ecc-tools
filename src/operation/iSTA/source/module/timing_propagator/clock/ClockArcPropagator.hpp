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

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "ClockSource.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"

namespace ista {
class ClockArcPropagator
{
 public:
  explicit ClockArcPropagator(Database& database, DelayCalculator& delay) : _database(database), _delay(delay) {}
  bool isDisableArc(Arc& arc);
  void propagateArrival(ClockSource& clock, std::size_t arc_idx, AnalysisType analysis_type, bool respect_boundaries = true);
  void propagateArrival(ClockSource& clock, std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type, bool respect_boundaries = true);
  void propagateSlew(ClockSource& clock, std::size_t arc_idx, AnalysisType analysis_type, bool respect_boundaries = true);
  void propagateSlew(ClockSource& clock, std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type, bool respect_boundaries = true);

 private:
  void updateClockPathState(std::string_view clock_name, TransType source_edge, Arc& arc, const ClockWaveform& source, ClockWaveform& sink,
                            AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type);
  void updateGraphArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double arc_delay);
  bool isBetterDelay(double candidate_delay, double current_delay, AnalysisType analysis_type);
  bool isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type);
  double roundTime(double time);
  std::vector<TransType> getOutputTransTypeList(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  bool isBetterArrival(double candidate_arrival, double current_arrival, AnalysisType analysis_type);
  bool isFinite(double value);
  void updateClockSlewDelay(std::string_view clock_name, TransType source_edge, Arc& arc, const ClockWaveform& source, ClockWaveform& sink,
                            AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type);
  // Evaluated delay belongs to one named clock. Graph delays are summaries only.
  using ArcDelayKey = std::tuple<std::string, const Arc*, AnalysisType, TransType, TransType, TransType>;
  std::map<ArcDelayKey, double> _clock_arc_delays;
  Database& _database;
  DelayCalculator& _delay;
};
}  // namespace ista
