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

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "graph/TimingStateQuery.hpp"
#include "path/PathEnd.hpp"

namespace ista {
class DelayCalculator;

// Evaluate capture choices and their exceptions for one propagated path.
// PathSearcher owns path enumeration; this checker returns its worst valid check.
class PathChecker
{
 public:
  PathChecker(Database& database, DelayCalculator& delay_calculator) : _database(database), _delay_calculator(delay_calculator), _query(database) {}
  // Return every legal capture/check candidate for this propagated path. The
  // winner-only checkPath() API remains the reduction used by setup analysis;
  // min/hold exporters use this API to preserve launch/capture context.
  std::vector<PathEnd> checkPathCandidates(const TimingPathState& path, const std::string& end_point, AnalysisType analysis);
  std::optional<PathEnd> checkPath(const TimingPathState& path, const std::string& end_point, AnalysisType analysis);
  double calcPathSlack(const TimingPathState& path, double required, AnalysisType analysis);

 private:
  struct CheckTimeResult
  {
    double value = 0.0;
    int32_t selected_arc_idx = -1;
  };

  CheckTimeResult checkTime(const TimingPathState& path, TimingCheckArc& arc, AnalysisType analysis, std::string_view clock, TransType source);
  Database& _database;
  DelayCalculator& _delay_calculator;
  TimingStateQuery _query;

  PathCheck evaluateRegister(const TimingPathState& path, TimingCheckArc& arc, AnalysisType analysis, std::string_view capture_clock,
                             std::size_t capture_index);
  std::vector<PathCheck> evaluateOutput(const TimingPathState& path, const std::string& end_point, AnalysisType analysis, TransType data_transition);
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
  double calcPathRequiredTime(const std::string& end_point, const TimingPathState& path, AnalysisType analysis, const PathCheck& check);
  double calcPathDelayRequiredTime(const std::string& end_point, const TimingPathState& path, AnalysisType analysis, const TimingException& exception,
                                   const PathCheck& check);
  double calcMulticycleRequiredTime(const TimingPathState& path, AnalysisType analysis, const ResolvedTimingExceptions& exceptions, const PathCheck& check);
  double exceptionClockShift(const TimingPathState& path, const PathCheck& check, bool use_end_clock, int steps, bool setup);
};
}  // namespace ista
