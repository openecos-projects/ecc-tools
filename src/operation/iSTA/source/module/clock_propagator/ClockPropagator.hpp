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

#include "CPModel.hpp"
#include "Database.hpp"

namespace ista {

#define STACP (ista::ClockPropagator::getInst())

class ClockPropagator
{
 public:
  static void initInst();
  static ClockPropagator& getInst();
  static void destroyInst();
  // function
  void propagate();

 private:
  // self
  static ClockPropagator* _cp_instance;

  ClockPropagator() = default;
  ClockPropagator(const ClockPropagator& other) = delete;
  ClockPropagator(ClockPropagator&& other) = delete;
  ~ClockPropagator() = default;
  ClockPropagator& operator=(const ClockPropagator& other) = delete;
  ClockPropagator& operator=(ClockPropagator&& other) = delete;
  // function
  bool isDisableArc(Arc& arc);
  CPModel initCPModel();
  void buildClockSourceList(CPModel& cp_model);
  void initTimingPointList();
  void markClockPointList(CPModel& cp_model);
  void markClockPoint(CPClock& clock);
  void propagateClockArrival(CPModel& cp_model);
  void seedPhysicalClockState(CPClock& clock);
  void updateEffectiveClockState(CPClock& clock);
  void propagateClockSlewDelay(CPClock& clock);
  void propagateClockSlewDelayArc(CPClock& clock, std::size_t arc_idx, AnalysisType analysis_type);
  void propagateClockSlewDelayArc(CPClock& clock, std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type);
  void updateClockSlewDelay(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type, TransType input_trans_type,
                            TransType output_trans_type);
  void propagateClockArrivalArc(CPClock& clock, std::size_t arc_idx, AnalysisType analysis_type);
  void propagateClockArrivalArc(CPClock& clock, std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type);
  void updateClockPathState(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type, TransType input_trans_type,
                            TransType output_trans_type);
  bool hasClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  double getClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  void updateClockArrival(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, double clock_arrival);
  void updateClockPredecessor(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, TransType predecessor_trans_type, Arc& arc,
                              double arc_delay);
  bool shouldStopClockPropagation(std::string& pin_name);
  void updateGraphArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double arc_delay);
  bool isBetterDelay(double candidate_delay, double current_delay, AnalysisType analysis_type);
  bool isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type);
  double roundTime(double time);
  std::vector<TransType> getOutputTransTypeList(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  double getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  double getArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type);
  bool isBetterArrival(double candidate_arrival, double current_arrival, AnalysisType analysis_type);
  bool isFinite(double value);
  static bool is_clock_tree_overlap(const TimingPoint& timing_point, const CPClock& clock)
  {
    if (timing_point.get_clock_name().empty())
      return false;
    if (timing_point.get_clock_name() == clock.get_clock_name())
      return false;
    return true;
  }
};

}  // namespace ista
