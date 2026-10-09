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

namespace ipw {

#define PWCP (ipw::ClockPropagator::getInst())

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
  void initSignalPointList(CPModel& cp_model);
  void buildClockNameList(std::vector<std::string>& clock_name_list);
  void markClockPointList(CPModel& cp_model, std::vector<std::string>& clock_name_list);
  void markClockPoint(CPModel& cp_model, std::string& clock_name);
  void propagateClockSlew(CPModel& cp_model, std::vector<std::string>& clock_name_list);
  void seedPhysicalClockSlew(CPModel& cp_model, std::string& clock_name);
  bool seedGeneratedClockSlew(CPModel& cp_model, std::string& clock_name, const TimingClock& timing_clock);
  bool hasPhysicalClockSlew(const CPModel& cp_model, std::string_view pin_name, std::string_view clock_name);
  void updateEffectiveClockSlew(CPModel& cp_model, std::string& clock_name);
  void propagateClockSlew(CPModel& cp_model, std::string& clock_name);
  void propagateClockSlewArc(CPModel& cp_model, std::string& clock_name, std::size_t arc_idx, AnalysisType analysis_type);
  void propagateClockSlewArc(CPModel& cp_model, std::string& clock_name, std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type);
  void updateClockSlew(CPModel& cp_model, std::string_view clock_name, Arc& arc, AnalysisType analysis_type, TransType input_trans_type,
                       TransType output_trans_type);
  bool shouldStopClockPropagation(std::string& pin_name);
  bool isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type);
  std::vector<TransType> getOutputTransTypeList(Arc& arc, TransType input_trans_type);
};

}  // namespace ipw
