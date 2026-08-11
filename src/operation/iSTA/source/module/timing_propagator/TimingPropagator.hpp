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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Database.hpp"
#include "TPModel.hpp"

namespace ista {

#define STATP (ista::TimingPropagator::getInst())

class TimingPropagator
{
 public:
  static void initInst();
  static TimingPropagator& getInst();
  static void destroyInst();
  // function
  void propagate();

 private:
  // self
  static TimingPropagator* _tp_instance;

  TimingPropagator() = default;
  TimingPropagator(const TimingPropagator& other) = delete;
  TimingPropagator(TimingPropagator&& other) = delete;
  ~TimingPropagator() = default;
  TimingPropagator& operator=(const TimingPropagator& other) = delete;
  TimingPropagator& operator=(TimingPropagator&& other) = delete;
  // function
  bool isDisableArc(Arc& arc);
  bool shouldStopDataPropagation(Arc& arc);
  bool shouldStopDataSlewPropagation(Arc& arc);
  bool isSequentialClockPin(std::string& pin_name);
  bool hasIncomingPhysicalSlewArc(std::string& pin_name);
  TPModel initTPModel();
  void buildStartPointList(TPModel& tp_model);
  double getClockArrival(std::string& pin_name, AnalysisType analysis_type);
  double getClockArrival(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  double getClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  double getClockSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  void seedStartPointList(TPModel& tp_model);
  void propagateDataSlewDelay(TPModel& tp_model);
  void seedDataSlewList(TPModel& tp_model);
  void seedDataSlew(std::string& start_point, AnalysisType analysis_type);
  void seedDataSlew(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  void propagateDataSlewDelayArc(std::size_t arc_idx);
  void propagateDataSlewDelayArc(std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type);
  void updateDataSlewDelay(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type,
                           TransType input_trans_type, TransType output_trans_type);
  void updateGraphArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double arc_delay);
  void updateDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, double data_slew);
  bool hasDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  double getDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  bool isBetterDelay(double candidate_delay, double current_delay, AnalysisType analysis_type);
  bool isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type);
  double roundTime(double time);
  double getStartPointArrival(std::string& start_point, AnalysisType analysis_type);
  double getStartPointArrival(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  bool isClockSourceStartPoint(std::string& start_point);
  TimingClock* getStartPointClock(std::string& start_point);
  double getStartPointClockEdge(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  double getStartPointSlew(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  double getStartPointLaunchTime(std::string& start_point, AnalysisType analysis_type);
  double getStartPointLaunchTime(std::string& start_point, AnalysisType analysis_type, TransType trans_type);
  std::string getStartPointCrprClockPin(std::string& start_point);
  TransType getStartPointCrprClockTransType(std::string& start_point);
  TransType getClockTransType(TimingCellArc& timing_cell_arc);
  std::string_view getClockName(std::string& pin_name);
  std::string getPathStateStartPoint(std::string& start_point);
  void seedPathState(std::string& start_point, AnalysisType analysis_type);
  PathSourceType getStartPointSourceType(std::string& start_point, AnalysisType analysis_type);
  bool hasInputDelay(std::string& start_point, AnalysisType analysis_type);
  bool isInputStartPoint(std::string& start_point);
  bool isRegisterStartPoint(std::string& start_point);
  bool hasClockPoint(std::string& pin_name);
  void propagateArrivalArc(std::size_t arc_idx);
  void propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type);
  void propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type, TransType input_trans_type);
  void propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type, TransType input_trans_type,
                             TransType output_trans_type);
  std::vector<TransType> getOutputTransTypeList(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  double getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  double getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type);
  bool hasPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type);
  bool hasPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type);
  std::map<std::string, TimingPathState>& getPathStateMap(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type,
                                                          TransType trans_type);
  TimingPathState& getPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                                std::string& path_state_tag);
  TimingPathState* getWorstPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type);
  TimingPathState* getWorstPathState(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type);
  TransType getEndPointTransType(TimingPoint& timing_point, AnalysisType analysis_type, PathSourceType source_type);
  bool isBetterArrival(double candidate_arrival, double current_arrival, AnalysisType analysis_type);
  bool isFinite(double value);
};

}  // namespace ista
