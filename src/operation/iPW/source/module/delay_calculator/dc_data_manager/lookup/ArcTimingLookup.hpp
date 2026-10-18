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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <string>
#include <vector>

#include "AnalysisType.hpp"
#include "TransType.hpp"

namespace ipw {

class Arc;
class DCTask;
class ParasiticArnoldiSolver;
class ParasiticDmpSolver;
class TimingArc;
class TimingCellArc;

// 弧级延时/斜率查询：
// 负责弧与单元弧的选择、Liberty 查表，并按 Arnoldi → DMP → 查表 的顺序取结果。
class ArcTimingLookup
{
 public:
  ArcTimingLookup(ParasiticArnoldiSolver& arnoldi_solver, ParasiticDmpSolver& dmp_solver);

  // 填 DCTask 的入口：分派到 net 弧或单元弧的斜率计算
  void calculateArc(DCTask& dc_task);
  void calculateTimingCellArc(DCTask& dc_task);

  bool calcDrivingCellSlew(
      std::string& output_pin,
      TimingCellArc& timing_cell_arc,
      AnalysisType analysis_type,
      TransType output_trans_type,
      double input_transition_rise,
      double input_transition_fall,
      double& output_slew
  );

  std::vector<TransType> getOutputTransTypeList(Arc& arc, TransType input_trans_type);

 private:
  bool isClockArcTriggerTrans(TimingCellArc& timing_cell_arc, TransType input_trans_type);

  TimingCellArc* getTimingCellArc(Arc& arc);

  double calcTimingCellArcSlew(
      Arc& arc,
      TimingCellArc& timing_cell_arc,
      AnalysisType analysis_type,
      TransType input_trans_type,
      TransType output_trans_type,
      double input_slew
  );

  double calcTimingCellArcSlew(
      TimingCellArc& timing_cell_arc,
      AnalysisType analysis_type,
      TransType input_trans_type,
      TransType output_trans_type,
      double input_slew,
      double output_load
  );

  TransType getOutputTransType(TimingCellArc& timing_cell_arc, TransType input_trans_type);

  std::vector<TransType> getOutputTransTypeList(TimingCellArc& timing_cell_arc, TransType input_trans_type);

  std::vector<TimingArc*> getCandidateTimingArcList(
      TimingCellArc& timing_cell_arc,
      TransType input_trans_type,
      TransType output_trans_type,
      const std::string& output_pin = ""
  );

  bool isMatchTimingType(TimingArc& timing_arc, TransType trans_type);

  bool isMatchTimingType(TimingCellArc& timing_cell_arc, TransType trans_type);

  bool isPositiveArc(TimingArc& timing_arc);

  bool isNegativeArc(TimingArc& timing_arc);

  bool isNegativeArc(TimingCellArc& timing_cell_arc);

  bool isUnateArc(TimingCellArc& timing_cell_arc);

  bool isTwoTypeSenseArcSet(TimingCellArc& timing_cell_arc);

  double convertOutputLoad(TimingArc& timing_arc, double output_load);

  double calcTimingArcDelay(
      std::string& output_pin,
      TimingArc& timing_arc,
      AnalysisType analysis_type,
      TransType output_trans_type,
      double input_slew,
      double output_load
  );

  double calcTimingArcSlew(
      std::string& output_pin,
      TimingArc& timing_arc,
      AnalysisType analysis_type,
      TransType output_trans_type,
      double input_slew,
      double output_load
  );

  double calcTimingArcDelayByLoad(
      TimingArc& timing_arc,
      TransType output_trans_type,
      double input_slew,
      double output_load
  );

  double calcTimingArcSlewByLoad(
      TimingArc& timing_arc,
      TransType output_trans_type,
      double input_slew,
      double output_load
  );

  double calcNetArcSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);

  double calcParasiticSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  double calcArcSlew(
      Arc& arc,
      AnalysisType analysis_type,
      TransType input_trans_type,
      TransType output_trans_type,
      double input_slew
  );

  double calcTimingCellArcSlew(
      std::string& output_pin,
      TimingCellArc& timing_cell_arc,
      AnalysisType analysis_type,
      TransType input_trans_type,
      TransType output_trans_type,
      double input_slew
  );

  ParasiticArnoldiSolver& _arnoldi_solver;
  ParasiticDmpSolver& _dmp_solver;
};

}  // namespace ipw
