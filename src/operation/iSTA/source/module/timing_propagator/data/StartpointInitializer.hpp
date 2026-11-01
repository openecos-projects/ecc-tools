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
#include <vector>

#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "graph/TimingStateQuery.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {
class StartpointInitializer
{
 public:
  explicit StartpointInitializer(Database& database, DelayCalculator& delay) : _database(database), _delay(delay), _path_query(database) {}
  void seed(std::vector<std::string>& startpoints);
  // Both path state and physical slew are initialized before graph propagation.

 private:
  bool hasIncomingPhysicalSlewArc(std::string& pin_name);
  void seedDataSlewList(std::vector<std::string>& startpoints);
  void seedDataSlew(std::string& start_point, AnalysisType analysis_type);
  void seedDataSlew(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  double getStartPointSlew(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  double getStartPointSlew(std::string& start_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                           std::optional<TransType> source = std::nullopt);

  double getStartPointArrival(std::string& start_point, AnalysisType analysis_type);
  double getStartPointArrival(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  double getStartPointArrival(std::string& start_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                              std::optional<TransType> source = std::nullopt);
  std::vector<TimingIoDelay> getInputDelayList(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  double getClockEdge(std::string_view clock_name, TransType trans_type);
  double getInputDelayClockTime(const TimingIoDelay& delay, AnalysisType analysis_type, std::optional<TransType> source = std::nullopt);
  double getInputDelayArrival(const TimingIoDelay& delay, AnalysisType analysis_type, std::optional<TransType> source = std::nullopt);
  bool isClockSourceStartPoint(std::string& start_point);
  TimingClock* getStartPointClock(std::string& start_point);
  double getStartPointClockEdge(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  std::optional<DelayResult> getDrivingCellTiming(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  double getStartPointLaunchTime(std::string& start_point, AnalysisType analysis_type);
  double getStartPointLaunchTime(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  double getStartPointLaunchTime(std::string& start_point, std::string_view clock_name, AnalysisType analysis_type, TransType trans_type,
                                 std::optional<TransType> source = std::nullopt);
  std::string getStartPointCrprClockPin(std::string& start_point);
  TransType getStartPointCrprClockTransType(std::string& start_point);
  TransType getClockTransType(TimingCellArc& timing_cell_arc);
  std::string_view getClockName(std::string& pin_name);
  std::vector<std::string> getStartPointClockNames(std::string& start_point);
  std::string getPathStateStartPoint(std::string& start_point);
  void seedPathState(std::string& start_point, AnalysisType analysis_type);
  void seedInputPathState(std::string& start_point, AnalysisType analysis_type, TransType trans_type, const TimingIoDelay& delay);
  void seedPathState(std::string& start_point, AnalysisType analysis_type, TransType trans_type, std::string_view clock_name, double arrival, double slew,
                     double launch_time, TransType clock_trans_type, bool clock_source_path = false, std::optional<double> input_delay = std::nullopt,
                     double input_source_latency = 0.0, std::optional<std::size_t> edge_index = std::nullopt, std::string_view reference_pin = {});
  PathSourceType getStartPointSourceType(std::string& start_point, AnalysisType analysis_type);
  bool hasInputDelay(std::string& start_point, AnalysisType analysis_type);
  bool isInputStartPoint(std::string& start_point);
  bool isRegisterStartPoint(std::string& start_point);
  bool hasClockPoint(std::string& pin_name);
  Database& _database;
  DelayCalculator& _delay;
  TimingStateQuery _path_query;
  ExceptionThroughIndex _through_index;
};
}  // namespace ista
