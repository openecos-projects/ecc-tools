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

namespace ipw {

#define PWSP (ipw::SignalPropagator::getInst())

class SignalPropagator
{
 public:
  static void initInst();
  static SignalPropagator& getInst();
  static void destroyInst();
  // function
  void propagate();

 private:
  // self
  static SignalPropagator* _sp_instance;

  SignalPropagator() = default;
  SignalPropagator(const SignalPropagator& other) = delete;
  SignalPropagator(SignalPropagator&& other) = delete;
  ~SignalPropagator() = default;
  SignalPropagator& operator=(const SignalPropagator& other) = delete;
  SignalPropagator& operator=(SignalPropagator&& other) = delete;
  // function
  bool isDisableArc(Arc& arc);
  bool shouldStopSignalSlewPropagation(Arc& arc);
  bool isSequentialClockPin(std::string& pin_name);
  bool hasIncomingPhysicalSlewArc(std::string& pin_name);
  void seedSignalSlewList();
  void seedSignalSlew(std::string& source_pin, AnalysisType analysis_type);
  void seedSignalSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type);
  double getSourcePinSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type);
  std::optional<double> getDrivingCellSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type);
  TransType getClockTransType(TimingCellArc& timing_cell_arc);
  double getClockSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  void propagateSignalSlew();
  void propagateSignalSlewArc(std::size_t arc_idx);
  void propagateSignalSlewArc(std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type);
  void updateSignalSlew(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type, TransType input_trans_type,
                        TransType output_trans_type);
  void updateSignalSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, double signal_slew);
  bool hasSignalSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  double getSignalSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  bool isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type);
  std::vector<TransType> getOutputTransTypeList(Arc& arc, TransType input_trans_type);
};

}  // namespace ipw
