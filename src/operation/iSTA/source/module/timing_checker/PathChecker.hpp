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

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ClockReconvergenceCalculator.hpp"
#include "graph/TimingStateQuery.hpp"
#include "path/PathEnd.hpp"

namespace ista {
class DelayCalculator;

// Evaluate capture choices and their exceptions for one propagated path.
// PathSearcher owns path enumeration; this checker returns its worst valid check.
class PathChecker
{
 public:
  PathChecker(Database& database, DelayCalculator& delay_calculator,
              ClockReconvergenceCalculator::CacheMode clock_cache = ClockReconvergenceCalculator::CacheMode::kNone)
      : _database(database), _delay_calculator(delay_calculator), _query(database), _clock_pin_values(database, clock_cache), _reconvergence(database, clock_cache)
  {
  }
  std::optional<PathEnd> checkPath(const TimingPathState& path, const std::string& end_point, AnalysisType analysis, bool include_reconvergence = true);
  double calcPathSlack(const TimingPathState& path, double required, AnalysisType analysis);

 private:
  double checkTime(const TimingPathState& path, TimingCheckArc& arc, AnalysisType analysis, std::string_view clock, TransType source);
  Database& _database;
  DelayCalculator& _delay_calculator;
  TimingStateQuery _query;
  ClockPinValuesQuery _clock_pin_values;
  ClockReconvergenceCalculator _reconvergence;

  PathCheck evaluateRegister(const TimingPathState& path, TimingCheckArc& arc, AnalysisType analysis, std::string_view capture_clock,
                             std::size_t capture_index, bool include_reconvergence);
  std::vector<PathCheck> evaluateOutput(const TimingPathState& path, const std::string& end_point, AnalysisType analysis, TransType data_transition,
                                        bool include_reconvergence);
  std::vector<TimingCheckArc*> getCheckArcs(const std::string& end_point, AnalysisType analysis);
  std::vector<std::string> getCaptureClockNames(const std::string& clock_pin);
  std::vector<TimingIoDelay> getOutputDelayList(const std::string& end_point, AnalysisType analysis, TransType transition);
  bool isMatchCheckTransType(TimingCheckArc& arc, TransType transition);
  bool isMatchCheckType(TimingCheckArc& arc, AnalysisType analysis);
  std::string_view getClockName(const std::string& pin_name);
  std::string_view getOutputClockName(const std::string& end_point);
  AnalysisType getCaptureAnalysisType(AnalysisType analysis);
  double getClockEdge(std::string_view clock_name, TransType transition);
  double getEndPointCaptureTime(const TimingPathState& path, std::string_view capture_clock, TransType capture_transition, AnalysisType analysis,
                                std::size_t capture_index, bool level_sensitive = false);
  double getClockUncertainty(std::string_view launch_clock, TransType launch_transition, std::string_view capture_clock, TransType capture_transition,
                             AnalysisType analysis, const std::string& capture_pin, std::optional<TransType> local = std::nullopt);
  double getClockReconvergencePessimism(const TimingPathState& path, PathCheck& check, AnalysisType analysis);
  double calcPathRequiredTime(const std::string& end_point, const TimingPathState& path, AnalysisType analysis, PathCheck& check);
  double calcPathDelayRequiredTime(const std::string& end_point, const TimingPathState& path, AnalysisType analysis, const TimingException& exception,
                                   const PathCheck& check);
  double multicycleAdjustment(const TimingPathState& path, AnalysisType analysis, const ResolvedTimingExceptions& exceptions, const PathCheck& check);
  double exceptionClockShift(const TimingPathState& path, const PathCheck& check, bool use_end_clock, int steps, bool setup);
};
}  // namespace ista
