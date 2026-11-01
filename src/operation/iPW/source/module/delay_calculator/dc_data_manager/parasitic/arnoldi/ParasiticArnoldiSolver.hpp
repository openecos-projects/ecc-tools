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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "AnalysisType.hpp"
#include "ParasiticGraph.hpp"
#include "ParasiticArnoldiModel.hpp"
#include "ParasiticArnoldiPoleResidue.hpp"
#include "ParasiticDelayResult.hpp"
#include "TransType.hpp"

namespace ipw {

class Arc;
class ParasiticNet;
class TimingArc;

// Arnoldi 降阶延时求解：
// 把 RC 树投影到低阶子空间，求极点/留数，再解各节点电压穿越时间。
class ParasiticArnoldiSolver
{
 public:
  ParasiticDelayResult& getTimingResult(
      std::string& output_pin,
      TimingArc& timing_arc,
      AnalysisType analysis_type,
      TransType output_trans_type,
      double input_slew,
      double output_load
  );

  std::optional<double> getCachedLoadSlew(
      Arc& arc,
      AnalysisType analysis_type,
      TransType trans_type,
      double input_slew
  );

  std::optional<double> getInputPortSlew(
      ParasiticNet& parasitic_net,
      const std::string& source_node_name,
      const std::string& sink_node_name,
      AnalysisType analysis_type,
      TransType trans_type,
      double input_slew
  );

 private:
  static constexpr double kMinSlewResistanceThreshold = 0.45;

  ParasiticDelayResult calcParasiticArnoldiTimingResult(
      std::string& output_pin,
      TimingArc& timing_arc,
      AnalysisType analysis_type,
      TransType output_trans_type,
      double input_slew,
      double output_load
  );

  void cacheParasiticArnoldiDriverResult(
      std::string& output_pin,
      AnalysisType analysis_type,
      TransType output_trans_type,
      double driver_slew,
      ParasiticDelayResult& timing_result
  );

  // 按 (net, 源节点 id, corner) 取/建降阶模型：不再用字符串做 key。
  ParasiticArnoldiModel& getParasiticArnoldiModel(
      ParasiticNet& parasitic_net,
      uint32_t source_node_id,
      AnalysisType analysis_type,
      TransType trans_type
  );

  ParasiticArnoldiModel buildParasiticArnoldiModel(
      ParasiticNet& parasitic_net,
      uint32_t source_node_id,
      AnalysisType analysis_type,
      TransType trans_type
  );

  void initParasiticArnoldiTerm(
      ParasiticArnoldiModel& arnoldi_model,
      const ParasiticGraph& graph,
      std::vector<uint32_t>& node_id_list,
      uint32_t source_node_id
  );

  void updateParasiticArnoldiModel(
      ParasiticArnoldiModel& arnoldi_model,
      ParasiticNet& parasitic_net,
      const ParasiticGraph& graph,
      std::vector<uint32_t>& node_id_list,
      std::vector<int32_t>& parent_idx_list,
      std::vector<double>& resistance_list,
      std::vector<double>& capacitance_list,
      std::vector<std::size_t>& term_point_idx_list
  );

  void updateParasiticArnoldiProjection(
      ParasiticArnoldiModel& arnoldi_model,
      std::vector<double>& basis_list,
      std::vector<std::size_t>& term_point_idx_list,
      std::size_t order_idx
  );

  ParasiticDelayResult& getParasiticInputPortResult(
      ParasiticNet& parasitic_net,
      const std::string& source_node_name,
      AnalysisType analysis_type,
      TransType trans_type,
      double input_slew
  );

  ParasiticDelayResult calcParasiticInputPortResult(
      ParasiticNet& parasitic_net,
      const std::string& source_node_name,
      AnalysisType analysis_type,
      TransType trans_type,
      double input_slew
  );

  double calcParasiticInputPortCrossing(
      std::vector<double>& time_constants,
      std::vector<double>& weights,
      double ramp,
      double voltage
  );

  ParasiticArnoldiPoleResidue calcParasiticArnoldiPoleResidue(
      ParasiticArnoldiModel& arnoldi_model,
      double drive_resistance
  );

  bool solveParasiticArnoldiTridiagonalEigen(
      std::vector<double>& diagonal_list,
      std::vector<double>& off_diagonal_list,
      std::vector<double>& eigenvalue_list,
      std::vector<std::vector<double>>& eigenvector_list
  );

  void calcParasiticArnoldiThreshold(
      TimingArc& timing_arc,
      TransType trans_type,
      double& slew_derate,
      double& lower_threshold,
      double& upper_threshold,
      double& voltage_log,
      double& min_slew_factor,
      double& x1,
      double& y1
  );

  void calcParasiticArnoldiThreshold(
      TransType trans_type,
      double& slew_derate,
      double& lower_threshold,
      double& upper_threshold,
      double& voltage_log,
      double& min_slew_factor,
      double& x1,
      double& y1
  );

  void calcParasiticArnoldiThresholdFactor(
      double lower_threshold,
      double upper_threshold,
      double& min_slew_factor,
      double& x1,
      double& y1
  );

  double calcParasiticArnoldiHInverse(double value);

  double calcParasiticArnoldiTableResistance(
      TimingArc& timing_arc,
      TransType output_trans_type,
      double input_slew,
      double total_capacitance,
      double voltage_log,
      double slew_derate,
      double delay_resistance
  );

  double calcParasiticArnoldiTableRamp(
      TimingArc& timing_arc,
      TransType output_trans_type,
      double input_slew,
      double drive_resistance,
      double capacitance,
      double lower_threshold,
      double upper_threshold,
      double voltage_log,
      double slew_derate,
      double min_slew_factor
  );

  void solveParasiticArnoldiRamp(
      double pole,
      double transition_time,
      double lower_threshold,
      double upper_threshold,
      double& ramp
  );

  double solveParasiticArnoldiRampTime(double pole, double ramp, double voltage);

  void solveParasiticArnoldiRampPoint(double pole_ramp, double voltage, double& pole_time, double& derivative);

  double calcParasiticArnoldiEffectiveCapacitance(
      double driver_ramp,
      double drive_resistance,
      ParasiticArnoldiPoleResidue& pole_residue,
      double effective_capacitance_time
  );

  void solveParasiticArnoldiWaveformTime(
      double driver_ramp,
      ParasiticArnoldiPoleResidue& pole_residue,
      std::size_t term_idx,
      double voltage,
      double& waveform_time
  );

  void solveParasiticArnoldiWaveformTime(
      double driver_ramp,
      ParasiticArnoldiPoleResidue& pole_residue,
      std::size_t term_idx,
      double upper_threshold,
      double& upper_time,
      double mid_threshold,
      double& mid_time,
      double lower_threshold,
      double& lower_time
  );

  double calcParasiticArnoldiWaveformVoltage(
      double time,
      double driver_ramp,
      std::vector<double>& pole_list,
      std::vector<double>& residue_list
  );

  void calcParasiticArnoldiWaveformVoltageAndDerivative(
      double time,
      double driver_ramp,
      std::vector<double>& pole_list,
      std::vector<double>& residue_list,
      double& voltage,
      double& derivative
  );

  double solveParasiticArnoldiBracketedTime(
      double driver_ramp,
      std::vector<double>& pole_list,
      std::vector<double>& residue_list,
      double voltage,
      double lower_time,
      double upper_time,
      double lower_voltage,
      double upper_voltage
  );

  double calcTimingArcDelayByRawLoad(
      TimingArc& timing_arc,
      TransType output_trans_type,
      double input_slew,
      double output_load
  );

  double calcTimingArcSlewByRawLoad(
      TimingArc& timing_arc,
      TransType output_trans_type,
      double input_slew,
      double output_load
  );
};

}  // namespace ipw
