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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "AnalysisType.hpp"
#include "ParasiticGraph.hpp"
#include "DmpSolverModel.hpp"
#include "ParasiticDelayResult.hpp"
#include "ParasiticDmpLoadModel.hpp"
#include "ParasiticDmpModel.hpp"
#include "TransType.hpp"

namespace ipw {

class Arc;
class ParasiticNet;
class TimingArc;

// DMP（Driver Model with Pi / Zero-C2 负载）延时求解：
// 从 RC 树模型出发，用牛顿迭代求驱动波形，再逐负载节点求线延时与斜率。
class ParasiticDmpSolver
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

 private:
  // 数值常量
  static constexpr int32_t kStartTimeIndex = 0;
  static constexpr int32_t kTransitionTimeIndex = 1;
  static constexpr int32_t kEffectiveCapacitanceIndex = 2;
  static constexpr int32_t kLowerVoltageIndex = 0;
  static constexpr int32_t kThresholdVoltageIndex = 1;
  static constexpr int32_t kCurrentIndex = 2;
  static constexpr int32_t kMaxNewtonOrder = 3;
  static constexpr int32_t kMaxNewtonIteration = 100;
  static constexpr int32_t kMaxRootIteration = 20;
  static constexpr double kDriverParameterTolerance = 0.01;
  static constexpr double kThresholdTimeTolerance = 0.01;
  static constexpr double kTinyNumber = 1E-20;
  static constexpr double kGateResistanceCapacitanceStep = 1E-3;
  static constexpr double kCapacitiveDriverResistance = 1E-5;

  // 函数
  ParasiticDelayResult calcParasiticDmpTimingResult(
      std::string& output_pin,
      TimingArc& timing_arc,
      AnalysisType analysis_type,
      TransType output_trans_type,
      double input_slew,
      double output_load
  );

  bool calcParasiticDmpCeff(
      DmpSolverModel& dmp_solver_model,
      TimingArc& timing_arc,
      TransType trans_type,
      double input_slew,
      ParasiticDmpModel& dmp_model,
      double& gate_delay,
      double& driver_slew,
      double& effective_capacitance
  );

  bool initParasiticDmpCeff(
      DmpSolverModel& dmp_solver_model,
      TimingArc& timing_arc,
      TransType trans_type,
      double input_slew,
      ParasiticDmpModel& dmp_model
  );

  double calcGateResistance(DmpSolverModel& dmp_solver_model);

  bool getGateDelaySlew(DmpSolverModel& dmp_solver_model, double capacitance, double& gate_delay, double& gate_slew);

  bool calcCap(
      DmpSolverModel& dmp_solver_model,
      double& gate_delay,
      double& driver_slew,
      double& effective_capacitance
  );

  bool calcPi(DmpSolverModel& dmp_solver_model, double& gate_delay, double& driver_slew, double& effective_capacitance);

  bool initPi(DmpSolverModel& dmp_solver_model);

  bool calcZeroC2(
      DmpSolverModel& dmp_solver_model,
      double& gate_delay,
      double& driver_slew,
      double& effective_capacitance
  );

  bool initZeroC2(DmpSolverModel& dmp_solver_model);

  bool findDriverParams(DmpSolverModel& dmp_solver_model, double effective_capacitance);

  bool getGateDelays(
      DmpSolverModel& dmp_solver_model,
      double effective_capacitance,
      double& threshold_delay,
      double& lower_delay,
      double& measured_slew
  );

  bool newtonRaphson(DmpSolverModel& dmp_solver_model);

  bool evalDmpEqns(DmpSolverModel& dmp_solver_model);

  bool evalPiEqns(DmpSolverModel& dmp_solver_model);

  double calcPiCurrentDifference(
      DmpSolverModel& dmp_solver_model,
      double transition_time,
      double effective_capacitance_time,
      double effective_capacitance
  );

  bool evalOnePoleEqns(DmpSolverModel& dmp_solver_model);

  void calcCapacitiveWaveform(
      DmpSolverModel& dmp_solver_model,
      double time,
      double start_time,
      double transition_time,
      double capacitance,
      double& voltage
  );

  double calcCapacitiveUnitRamp(DmpSolverModel& dmp_solver_model, double time, double capacitance);

  void calcCapacitiveWaveformDerivative(
      DmpSolverModel& dmp_solver_model,
      double time,
      double start_time,
      double transition_time,
      double capacitance,
      double& start_derivative,
      double& transition_derivative,
      double& capacitance_derivative
  );

  double calcCapacitiveUnitRampTimeDerivative(DmpSolverModel& dmp_solver_model, double time, double capacitance);

  double calcCapacitiveUnitRampCapDerivative(DmpSolverModel& dmp_solver_model, double time, double capacitance);

  bool decomposeJacobian(DmpSolverModel& dmp_solver_model);

  void solveJacobian(DmpSolverModel& dmp_solver_model);

  bool findDriverDelaySlew(DmpSolverModel& dmp_solver_model, double& driver_delay, double& driver_slew);

  bool findOutputCrossing(
      DmpSolverModel& dmp_solver_model,
      double threshold,
      double lower_time,
      double upper_time,
      double& crossing_time
  );

  bool findRoot(
      std::function<void(double, double&, double&)>& function,
      double lower_time,
      double upper_time,
      double& root
  );

  void calcOutputWaveform(DmpSolverModel& dmp_solver_model, double time, double& voltage, double& derivative);

  void calcPiUnitRamp(DmpSolverModel& dmp_solver_model, double time, double& voltage, double& derivative);

  void calcZeroC2UnitRamp(DmpSolverModel& dmp_solver_model, double time, double& voltage, double& derivative);

  double getOutputCrossingUpperBound(DmpSolverModel& dmp_solver_model);

  double calcDmpExp(double value);

  void cacheParasiticDmpDriverResult(
      std::string& output_pin,
      AnalysisType analysis_type,
      TransType output_trans_type,
      double driver_slew,
      ParasiticDelayResult& timing_result
  );

  ParasiticDmpModel& getParasiticDmpModel(
      ParasiticNet& parasitic_net,
      uint32_t source_node_id,
      AnalysisType analysis_type,
      TransType trans_type
  );

  ParasiticDmpModel buildParasiticDmpModel(
      ParasiticNet& parasitic_net,
      uint32_t source_node_id,
      AnalysisType analysis_type,
      TransType trans_type
  );

  void buildParasiticDmpLoadModelList(
      ParasiticDmpModel& dmp_model,
      const ParasiticGraph& graph,
      std::vector<uint32_t>& node_id_list,
      std::vector<int32_t>& parent_idx_list,
      std::vector<double>& resistance_list,
      std::vector<double>& capacitance_list,
      uint32_t source_node_id
  );

  ParasiticDmpLoadModel buildParasiticDmpLoadModel(double moment1, double moment2, double moment3);

  bool calcParasiticDmpLoadDelay(
      ParasiticDmpLoadModel& load_model,
      TimingArc& timing_arc,
      TransType trans_type,
      double driver_slew,
      double& wire_delay,
      double& load_slew
  );

  double calcParasiticDmpLoadTime(
      double threshold,
      double pole1,
      double pole2,
      double residue1,
      double residue2,
      double constant,
      double residue_pole1,
      double residue_pole2,
      double transition_time,
      double transition_voltage
  );
};

}  // namespace ipw
