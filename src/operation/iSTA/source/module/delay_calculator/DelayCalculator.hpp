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

#include "DCTask.hpp"
#include "Database.hpp"
#include "ParasiticArnoldiModel.hpp"
#include "ParasiticDmpModel.hpp"

namespace ista {

#define STADC (ista::DelayCalculator::getInst())

class DelayCalculator
{
 public:
  static void initInst();
  static DelayCalculator& getInst();
  static void destroyInst();
  // function
  void init();
  void calculate(DCTask& dc_task);
  void destroy();

 private:
  // self
  static DelayCalculator* _dc_instance;
  std::map<std::string, std::map<std::string, std::vector<std::pair<std::string, double>>>> _parasitic_resistor_map_cache;
  std::map<std::string, std::map<AnalysisType, std::map<TransType, std::map<std::string, double>>>> _parasitic_load_map_cache;
  std::map<std::string, std::map<AnalysisType, std::map<TransType, std::map<std::string, double>>>> _parasitic_delay_map_cache;
  std::map<std::string, std::map<AnalysisType, std::map<TransType, std::map<std::string, double>>>> _parasitic_impulse_map_cache;
  std::map<std::string, ParasiticDmpModel> _parasitic_dmp_model_cache;
  std::map<std::string, ParasiticDmpTimingResult> _parasitic_dmp_timing_result_cache;
  std::map<std::string, ParasiticDmpTimingResult> _parasitic_dmp_driver_result_cache;
  std::map<ParasiticArnoldiModelKey, ParasiticArnoldiModel> _parasitic_arnoldi_model_cache;
  std::map<ParasiticArnoldiTimingResultKey, ParasiticArnoldiTimingResult> _parasitic_arnoldi_timing_result_cache;
  std::map<ParasiticArnoldiDriverResultKey, ParasiticArnoldiTimingResult> _parasitic_arnoldi_driver_result_cache;
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
  TimingArc* _timing_arc = nullptr;
  TransType _trans_type = TransType::kNone;
  double _input_slew = 0.0;
  double _driver_capacitance = 0.0;
  double _pi_resistance = 0.0;
  double _load_capacitance = 0.0;
  double _driver_resistance = 0.0;
  double _threshold = 0.5;
  double _lower_threshold = 0.3;
  double _upper_threshold = 0.7;
  double _slew_derate = 1.0;
  bool _is_pi = false;
  bool _is_zero_c2 = false;
  int32_t _newton_order = 0;
  double _start_time = 0.0;
  double _transition_time = 0.0;
  double _pi_pole1 = 0.0;
  double _pi_pole2 = 0.0;
  double _pi_zero = 0.0;
  double _pi_scale = 0.0;
  double _pi_constant1 = 0.0;
  double _pi_constant2 = 0.0;
  double _pi_residue1 = 0.0;
  double _pi_residue2 = 0.0;
  double _pi_current_constant = 0.0;
  double _pi_current_residue1 = 0.0;
  double _pi_current_residue2 = 0.0;
  double _zero_pole = 0.0;
  double _zero_zero = 0.0;
  double _zero_scale = 0.0;
  double _zero_constant1 = 0.0;
  double _zero_constant2 = 0.0;
  double _zero_residue = 0.0;
  std::array<double, 3> _parameter_list = {0.0, 0.0, 0.0};
  std::array<double, 3> _function_list = {0.0, 0.0, 0.0};
  std::array<std::array<double, 3>, 3> _jacobian = {{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}};
  std::array<double, 3> _scale_list = {0.0, 0.0, 0.0};
  std::array<double, 3> _delta_list = {0.0, 0.0, 0.0};
  std::array<int32_t, 3> _index_list = {0, 0, 0};

  DelayCalculator() = default;
  DelayCalculator(const DelayCalculator& other) = delete;
  DelayCalculator(DelayCalculator&& other) = delete;
  ~DelayCalculator() = default;
  DelayCalculator& operator=(const DelayCalculator& other) = delete;
  DelayCalculator& operator=(DelayCalculator&& other) = delete;
  // function
  void clearParasiticCache();
  void initializeArcTiming(DCTask& dc_task);
  void initializeArcTiming(Arc& arc);
  void initializeAnalysisArcTiming(Arc& arc, AnalysisType analysis_type);
  void initializeTransArcTiming(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  void calculateArc(DCTask& dc_task);
  void calculateTimingCellArc(DCTask& dc_task);
  void calculateTimingCheckArc(DCTask& dc_task);
  bool isClockArcTriggerTrans(TimingCellArc& timing_cell_arc, TransType input_trans_type);
  double calcArcDelay(Arc& arc);
  double calcCellArcDelay(Arc& arc, AnalysisType analysis_type);
  double calcCellArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  TimingCellArc* getTimingCellArc(Arc& arc);
  double calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type);
  double calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type);
  double calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                TransType output_trans_type);
  double calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                TransType output_trans_type, double input_slew, bool is_initialization = false);
  void updateTimingArcDelay(Arc& arc, TimingArc& timing_arc, AnalysisType analysis_type, TransType input_trans_type,
                            TransType output_trans_type, double delay, bool is_initialization);
  double calcTimingCellArcSlew(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                               TransType output_trans_type, double input_slew);
  double calcTimingCellArcDelay(TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                TransType output_trans_type, double input_slew, double output_load);
  double calcTimingCellArcSlew(TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                               TransType output_trans_type, double input_slew, double output_load);
  TransType getOutputTransType(TimingCellArc& timing_cell_arc, TransType input_trans_type);
  std::vector<TransType> getOutputTransTypeList(TimingCellArc& timing_cell_arc, TransType input_trans_type);
  std::vector<TimingArc*> getCandidateTimingArcList(TimingCellArc& timing_cell_arc, TransType input_trans_type, TransType output_trans_type);
  std::vector<TimingArc*> getCandidateTimingCheckArcList(TimingCheckArc& timing_check_arc, TransType clock_trans_type,
                                                         TransType data_trans_type);
  bool isMatchTimingType(TimingArc& timing_arc, TransType trans_type);
  bool isPositiveArc(TimingArc& timing_arc);
  bool isNegativeArc(TimingArc& timing_arc);
  bool isUnateArc(TimingCellArc& timing_cell_arc);
  bool isNegativeArc(TimingCellArc& timing_cell_arc);
  bool isTwoTypeSenseArcSet(TimingCellArc& timing_cell_arc);
  bool isMatchTimingType(TimingCellArc& timing_cell_arc, TransType trans_type);
  double convertOutputLoad(TimingArc& timing_arc, double output_load);
  double calcTimingArcDelay(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                            double input_slew, double output_load);
  double calcTimingArcSlew(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                           double input_slew, double output_load);
  double calcTimingArcDelayByLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load);
  double calcTimingArcSlewByLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load);
  double calcTimingArcDelayByRawLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load);
  double calcTimingArcSlewByRawLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load);
  double getArcOutputLoad(Arc& arc, AnalysisType analysis_type, TransType output_trans_type);
  double getOutputPinLoad(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type);
  double getNetOutputLoad(Net& net, AnalysisType analysis_type, TransType output_trans_type);
  double getParasiticNetOutputLoad(Net& net, ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type);
  double getPinCapacitance(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
  double calcNetArcDelay(Arc& arc);
  double calcNetArcDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type);
  double calcNetArcDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  double calcParasiticDelay(Arc& arc);
  double calcParasiticDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type);
  double calcParasiticDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  double getParasiticNodeCapacitance(ParasiticNet& parasitic_net, std::string& pin_name);
  double getParasiticNodeLoad(ParasiticNet& parasitic_net, std::string& node_name, AnalysisType analysis_type, TransType trans_type);
  void buildParasiticDelayMap(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type);
  double updateParasiticLoadMap(ParasiticNet& parasitic_net, std::string& node_name, std::string& parent_node_name,
                                std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map,
                                std::set<std::string>& visited_node_set, AnalysisType analysis_type, TransType trans_type);
  void updateParasiticDelayMap(ParasiticNet& parasitic_net, std::string& node_name, std::string& parent_node_name,
                               std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map,
                               std::set<std::string>& visited_node_set, AnalysisType analysis_type, TransType trans_type);
  double updateParasiticLoadDelayMap(ParasiticNet& parasitic_net, std::string& node_name, std::string& parent_node_name,
                                     std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map,
                                     std::set<std::string>& visited_node_set, AnalysisType analysis_type, TransType trans_type,
                                     std::map<std::string, double>& load_delay_map);
  void updateParasiticImpulseMap(ParasiticNet& parasitic_net, std::string& node_name, std::string& parent_node_name,
                                 std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map,
                                 std::set<std::string>& visited_node_set, AnalysisType analysis_type, TransType trans_type,
                                 std::map<std::string, double>& load_delay_map, std::map<std::string, double>& beta_map);
  double getParasiticTotalLoad(ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type);
  void buildParasiticResistorMap(ParasiticNet& parasitic_net, std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map);
  std::string getParasiticNodeName(ParasiticNet& parasitic_net, std::string& pin_name);
  std::string getPinNameByParasiticNodeName(std::string& node_name);
  ParasiticDmpTimingResult& getParasiticDmpTimingResult(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type,
                                                        TransType output_trans_type, double input_slew, double output_load);
  std::string getParasiticDmpTimingResultKey(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type,
                                             TransType output_trans_type, double input_slew);
  ParasiticDmpTimingResult calcParasiticDmpTimingResult(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type,
                                                        TransType output_trans_type, double input_slew, double output_load);
  bool calcParasiticDmpCeff(TimingArc& timing_arc, TransType trans_type, double input_slew, ParasiticDmpModel& dmp_model, double& gate_delay,
                            double& driver_slew, double& effective_capacitance);
  bool initParasiticDmpCeff(TimingArc& timing_arc, TransType trans_type, double input_slew, ParasiticDmpModel& dmp_model);
  double getNormalizedThreshold(double threshold);
  double calcGateResistance();
  bool getGateDelaySlew(double capacitance, double& gate_delay, double& gate_slew);
  bool calcCap(double& gate_delay, double& driver_slew, double& effective_capacitance);
  bool calcPi(double& gate_delay, double& driver_slew, double& effective_capacitance);
  bool initPi();
  bool calcZeroC2(double& gate_delay, double& driver_slew, double& effective_capacitance);
  bool initZeroC2();
  bool findDriverParams(double effective_capacitance);
  bool getGateDelays(double effective_capacitance, double& threshold_delay, double& lower_delay, double& measured_slew);
  bool newtonRaphson();
  bool evalDmpEqns();
  bool evalPiEqns();
  double calcPiCurrentDifference(double transition_time, double effective_capacitance_time, double effective_capacitance);
  bool evalOnePoleEqns();
  void calcCapacitiveWaveform(double time, double start_time, double transition_time, double capacitance, double& voltage);
  double calcCapacitiveUnitRamp(double time, double capacitance);
  void calcCapacitiveWaveformDerivative(double time, double start_time, double transition_time, double capacitance, double& start_derivative,
                                        double& transition_derivative, double& capacitance_derivative);
  double calcCapacitiveUnitRampTimeDerivative(double time, double capacitance);
  double calcCapacitiveUnitRampCapDerivative(double time, double capacitance);
  bool decomposeJacobian();
  void solveJacobian();
  bool findDriverDelaySlew(double& driver_delay, double& driver_slew);
  bool findOutputCrossing(double threshold, double lower_time, double upper_time, double& crossing_time);
  bool findRoot(std::function<void(double, double&, double&)>& function, double lower_time, double upper_time, double& root);
  void calcOutputWaveform(double time, double& voltage, double& derivative);
  void calcPiUnitRamp(double time, double& voltage, double& derivative);
  void calcZeroC2UnitRamp(double time, double& voltage, double& derivative);
  double getOutputCrossingUpperBound();
  double calcDmpExp(double value);
  void cacheParasiticDmpDriverResult(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type, double driver_slew,
                                     ParasiticDmpTimingResult& timing_result);
  std::string getParasiticDmpDriverResultKey(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type, double driver_slew);
  std::optional<double> getParasiticDmpCachedWireDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  std::optional<double> getParasiticDmpCachedLoadSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  ParasiticDmpModel& getParasiticDmpModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                          TransType trans_type);
  std::string getParasiticDmpModelKey(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                      TransType trans_type);
  ParasiticDmpModel buildParasiticDmpModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                           TransType trans_type);
  void buildParasiticDmpPiModel(ParasiticDmpModel& dmp_model, std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                std::vector<double>& capacitance_list);
  void buildParasiticDmpLoadModelList(ParasiticDmpModel& dmp_model, std::vector<std::string>& node_name_list,
                                      std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                      std::vector<double>& capacitance_list, std::string& source_node_name);
  ParasiticDmpLoadModel buildParasiticDmpLoadModel(double moment1, double moment2, double moment3);
  bool calcParasiticDmpLoadDelay(ParasiticDmpLoadModel& load_model, TimingArc& timing_arc, TransType trans_type, double driver_slew,
                                 double& wire_delay, double& load_slew);
  double calcParasiticDmpLoadTime(double threshold, double pole1, double pole2, double residue1, double residue2, double constant,
                                  double residue_pole1, double residue_pole2, double transition_time, double transition_voltage);
  ParasiticArnoldiTimingResult& getParasiticArnoldiTimingResult(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type,
                                                                TransType output_trans_type, double input_slew, double output_load);
  ParasiticArnoldiTimingResultKey getParasiticArnoldiTimingResultKey(std::string& output_pin, TimingArc& timing_arc,
                                                                     AnalysisType analysis_type, TransType output_trans_type,
                                                                     double input_slew);
  ParasiticArnoldiTimingResult calcParasiticArnoldiTimingResult(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type,
                                                                TransType output_trans_type, double input_slew, double output_load);
  void adjustParasiticLoadThreshold(TimingArc& timing_arc, std::string& load_pin, TransType trans_type, double& wire_delay, double& load_slew);
  TimingCell* getThresholdTimingCell(std::string& pin_name);
  double getTimingCellSlewLowerThreshold(TimingCell& timing_cell, TransType trans_type);
  double getTimingCellSlewUpperThreshold(TimingCell& timing_cell, TransType trans_type);
  double getTimingCellInputThreshold(TimingCell& timing_cell, TransType trans_type);
  void cacheParasiticArnoldiDriverResult(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type, double driver_slew,
                                         ParasiticArnoldiTimingResult& timing_result);
  ParasiticArnoldiDriverResultKey getParasiticArnoldiDriverResultKey(std::string& output_pin, AnalysisType analysis_type,
                                                                     TransType output_trans_type, double driver_slew);
  std::optional<double> getParasiticArnoldiCachedWireDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  std::optional<double> getParasiticArnoldiCachedLoadSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  ParasiticArnoldiModel& getParasiticArnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                  TransType trans_type);
  ParasiticArnoldiModelKey getParasiticArnoldiModelKey(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                        AnalysisType analysis_type, TransType trans_type);
  ParasiticArnoldiModel buildParasiticArnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                   TransType trans_type);
  void initParasiticArnoldiTree(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type,
                                std::vector<std::string>& node_name_list, std::vector<int32_t>& parent_idx_list,
                                std::vector<double>& resistance_list, std::vector<double>& capacitance_list);
  void initParasiticArnoldiTerm(ParasiticArnoldiModel& arnoldi_model, std::vector<std::string>& node_name_list, std::string& source_node_name);
  void updateParasiticArnoldiModel(ParasiticArnoldiModel& arnoldi_model, ParasiticNet& parasitic_net,
                                   std::vector<std::string>& node_name_list, std::vector<int32_t>& parent_idx_list,
                                   std::vector<double>& resistance_list, std::vector<double>& capacitance_list,
                                   std::vector<std::size_t>& term_point_idx_list);
  void updateParasiticArnoldiProjection(ParasiticArnoldiModel& arnoldi_model, std::vector<double>& basis_list,
                                        std::vector<std::size_t>& term_point_idx_list, std::size_t order_idx);
  double calcParasiticArnoldiElmore(ParasiticArnoldiModel& arnoldi_model, std::string& sink_node_name);
  double calcParasiticArnoldiElmore(ParasiticArnoldiModel& arnoldi_model, std::size_t term_idx);
  std::optional<double> calcParasiticArnoldiInputPortDelay(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                          std::string& sink_node_name, AnalysisType analysis_type, TransType trans_type);
  std::optional<double> calcParasiticArnoldiInputPortSlew(ParasiticNet& parasitic_net, std::string& source_node_name, std::string& sink_node_name,
                                                         AnalysisType analysis_type, TransType trans_type, double input_slew);
  double getParasiticArnoldiSlewScale(TransType trans_type);
  ParasiticArnoldiPoleResidue calcParasiticArnoldiPoleResidue(ParasiticArnoldiModel& arnoldi_model, double drive_resistance);
  bool solveParasiticArnoldiTridiagonalEigen(std::vector<double>& diagonal_list, std::vector<double>& off_diagonal_list,
                                             std::vector<double>& eigenvalue_list, std::vector<std::vector<double>>& eigenvector_list);
  void calcParasiticArnoldiThreshold(TimingArc& timing_arc, TransType trans_type, double& slew_derate, double& lower_threshold,
                                     double& upper_threshold, double& voltage_log, double& min_slew_factor, double& x1, double& y1);
  void calcParasiticArnoldiThreshold(TransType trans_type, double& slew_derate, double& lower_threshold, double& upper_threshold,
                                     double& voltage_log, double& min_slew_factor, double& x1, double& y1);
  void calcParasiticArnoldiThresholdFactor(double lower_threshold, double upper_threshold, double& min_slew_factor, double& x1, double& y1);
  double calcParasiticArnoldiHInverse(double value);
  double calcParasiticArnoldiTableResistance(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double total_capacitance,
                                             double voltage_log, double slew_derate, double delay_resistance);
  double calcParasiticArnoldiTableRamp(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double drive_resistance,
                                       double capacitance, double lower_threshold, double upper_threshold, double voltage_log, double slew_derate,
                                       double min_slew_factor);
  void solveParasiticArnoldiRamp(double pole, double transition_time, double lower_threshold, double upper_threshold, double& ramp);
  double solveParasiticArnoldiRampTime(double pole, double ramp, double voltage);
  void solveParasiticArnoldiRampPoint(double pole_ramp, double voltage, double& pole_time, double& derivative);
  double calcParasiticArnoldiEffectiveCapacitance(double driver_ramp, double drive_resistance, ParasiticArnoldiPoleResidue& pole_residue,
                                                  double effective_capacitance_time);
  void solveParasiticArnoldiWaveformTime(double driver_ramp, ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx, double voltage,
                                         double& waveform_time);
  void solveParasiticArnoldiWaveformTime(double driver_ramp, ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx,
                                         double upper_threshold, double& upper_time, double mid_threshold, double& mid_time,
                                         double lower_threshold, double& lower_time);
  double calcParasiticArnoldiWaveformVoltage(double time, double driver_ramp, std::vector<double>& pole_list, std::vector<double>& residue_list);
  void calcParasiticArnoldiWaveformVoltageAndDerivative(double time, double driver_ramp, std::vector<double>& pole_list,
                                                        std::vector<double>& residue_list, double& voltage, double& derivative);
  double solveParasiticArnoldiBracketedTime(double driver_ramp, std::vector<double>& pole_list, std::vector<double>& residue_list, double voltage,
                                            double lower_time, double upper_time, double lower_voltage, double upper_voltage);
  double getParasiticTotalResistance(ParasiticNet& parasitic_net);
  double calcTimingCellArcDelay(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                TransType input_trans_type, TransType output_trans_type, double input_slew);
  double calcTimingCellArcSlew(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                               TransType input_trans_type, TransType output_trans_type, double input_slew);
  double calcArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double input_slew);
  double calcArcSlew(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double input_slew);
  double calcNetArcSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  double calcParasiticSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
};

}  // namespace ista
