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
  // 一次待提交的斜率更新：提交阶段严格按 signal_order 原序回放，保证与旧逐 pin 实现逐位一致。
  struct SlewUpdate
  {
    uint32_t sink_vertex_idx = 0;
    AnalysisType analysis_type = AnalysisType::kNone;
    TransType trans_type = TransType::kNone;
    double slew = 0.0;
  };

  bool isDisableArc(Arc& arc);
  bool shouldStopSignalSlewPropagation(Arc& arc);
  bool isSequentialClockPin(uint32_t pin_index);
  bool isSequentialClockPin(std::string& pin_name);
  bool hasIncomingPhysicalSlewArc(uint32_t pin_index);
  bool hasIncomingPhysicalSlewArc(std::string& pin_name);
  TimingPoint* findTimingPoint(uint32_t pin_index);
  void seedSignalSlewList();
  void seedSignalSlew(std::string& source_pin, AnalysisType analysis_type);
  void seedSignalSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type);
  double getSourcePinSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type);
  std::optional<double> getDrivingCellSlew(std::string& source_pin, AnalysisType analysis_type, TransType trans_type);
  TransType getClockTransType(TimingCellArc& timing_cell_arc);
  double getClockSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  double getClockSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  void propagateSignalSlew();
  // 阶段①：只读源斜率 + thread_local 延时缓存，把结果追加到本 pin 的 updates（无共享写）
  void computeSignalSlewPin(uint32_t pin_index, std::vector<SlewUpdate>& updates);
  void computeSignalSlewArc(uint32_t arc_idx, std::vector<SlewUpdate>& updates);
  void computeSignalSlewArc(
      uint32_t arc_idx,
      AnalysisType analysis_type,
      TransType input_trans_type,
      std::vector<SlewUpdate>& updates
  );
  // 阶段②：串行提交（唯一写 TimingPoint 的地方）
  void applySignalSlewList(const std::vector<SlewUpdate>& updates);
  void updateSignalSlew(
      TimingPoint& timing_point,
      AnalysisType analysis_type,
      TransType trans_type,
      double signal_slew
  );
  bool hasSignalSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  double getSignalSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  bool isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type);
  std::vector<TransType> getOutputTransTypeList(Arc& arc, TransType input_trans_type);
};

}  // namespace ipw
