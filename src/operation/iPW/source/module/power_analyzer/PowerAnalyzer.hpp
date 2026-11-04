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
#include "PAInstanceModel.hpp"
#include "PAInstanceStaticContext.hpp"
#include "PALeakageSummary.hpp"
#include "TimingArcSense.hpp"

namespace ipw {

#define PWPA (ipw::PowerAnalyzer::getInst())

class PowerAnalyzer
{
 public:
  static void initInst();
  static PowerAnalyzer& getInst();
  static void destroyInst();
  // function
  void analyze();
  // One-time per-run preparation of the window-independent per-instance data
  // (power group, voltage, arc energies, output loads). Must be called before
  // any multi-threaded use (the peak engine prepares it before its workers).
  void prepareStaticContext();

 private:
  // self
  static PowerAnalyzer* _pa_instance;

  std::vector<PAInstanceStaticContext> _instance_static_context_list;
  // pin 索引 -> net 索引（kInvalidIndex = 该 pin 没有可记录的 net）
  std::vector<uint32_t> _pin_net_index_list;
  bool _is_static_context_ready = false;

  PowerAnalyzer() = default;
  PowerAnalyzer(const PowerAnalyzer& other) = delete;
  PowerAnalyzer(PowerAnalyzer&& other) = delete;
  ~PowerAnalyzer() = default;
  PowerAnalyzer& operator=(const PowerAnalyzer& other) = delete;
  PowerAnalyzer& operator=(PowerAnalyzer&& other) = delete;
  // function
  void analyzePower();
  void initPowerSummary();
  const PAInstanceStaticContext* getStaticContext(Instance& instance);
  InstancePower analyzeInstancePower(Instance& instance, PAInstanceModel& pa_instance_model);
  PowerValue getInstancePowerValue(Instance& instance, PAInstanceModel& pa_instance_model);
  void analyzeInternalPower(Instance& instance, PowerValue& power_value, PAInstanceModel& pa_instance_model);
  void buildOutputTimingPowerArcWeightMap(
      Instance& instance,
      TimingCell& timing_cell,
      PAInstanceModel& pa_instance_model
  );
  double getTimingPowerArcPower(
      Instance& instance,
      TimingPowerArc& timing_power_arc,
      const PAArcStaticContext& arc_static_context,
      PAInstanceModel& pa_instance_model
  );
  double getInputTimingPowerArcPower(
      Instance& instance,
      TimingPowerArc& timing_power_arc,
      const PAArcStaticContext& arc_static_context,
      PAInstanceModel& pa_instance_model
  );
  double getInputTimingPowerArcConditionProbability(
      Instance& instance,
      TimingPowerArc& timing_power_arc,
      PAInstanceModel& pa_instance_model
  );
  double getOutputTimingPowerArcPower(
      Instance& instance,
      TimingPowerArc& timing_power_arc,
      const PAArcStaticContext& arc_static_context,
      PAInstanceModel& pa_instance_model
  );
  double getOutputTimingPowerArcWeight(
      Instance& instance,
      TimingPowerArc& timing_power_arc,
      PAInstanceModel& pa_instance_model
  );
  double getOutputTimingPowerArcConditionProbability(
      Instance& instance,
      TimingPowerArc& timing_power_arc,
      PAInstanceModel& pa_instance_model
  );
  double getOutputTimingPowerArcWeightSum(
      TimingPowerArc& timing_power_arc,
      PAInstanceModel& pa_instance_model,
      TransType trans_type
  );
  double getTimingPowerArcEnergy(Instance& instance, TimingPowerArc& timing_power_arc, TransType trans_type);
  double getTimingPowerArcInputSlew(Instance& instance, TimingPowerArc& timing_power_arc, TransType trans_type);
  TimingArcSense getTimingPowerArcSense(Instance& instance, TimingPowerArc& timing_power_arc);
  double getTimingPowerArcOutputLoad(Instance& instance, TimingPowerArc& timing_power_arc, TransType trans_type);
  double getTimingPowerArcConditionProbability(
      Instance& instance,
      TimingPowerArc& timing_power_arc,
      PAInstanceModel& pa_instance_model
  );
  double getLogicExpressionStaticProbability(
      LogicExpression& logic_expression,
      Instance& instance,
      PAInstanceModel& pa_instance_model
  );
  double getSensitivityProbability(
      LogicExpression& logic_expression,
      const std::string& port_name,
      Instance& instance,
      PAInstanceModel& pa_instance_model
  );
  void analyzeSwitchingPower(Instance& instance, PowerValue& power_value, PAInstanceModel& pa_instance_model);
  void recordNetPower(
      uint32_t pin_index,
      const double voltage,
      const double output_load,
      PowerActivity& activity,
      const double switching_power
  );
  void analyzeLeakagePower(Instance& instance, PowerValue& power_value, PAInstanceModel& pa_instance_model);
  double getLeakageConditionProbability(
      Instance& instance,
      TimingLeakagePower& timing_leakage_power,
      PAInstanceModel& pa_instance_model
  );
  PowerActivity getOutputPortActivity(
      Instance& instance,
      const std::string& port_name,
      PAInstanceModel& pa_instance_model
  );
  double getDriverEnableProbability(Instance& instance, TimingCellPort& port, PAInstanceModel& pa_instance_model);
  PowerActivity getPortActivity(Instance& instance, const std::string& port_name, PAInstanceModel& pa_instance_model);
  std::map<std::string, PowerActivity>& getPortActivityMap(Instance& instance, PAInstanceModel& pa_instance_model);
  PowerActivity getPinActivity(const std::string& pin_name);
  PowerActivity getPinActivity(uint32_t pin_index);
  double getPinSlew(const std::string& pin_name, TransType trans_type);
  double getInstanceVoltage(Instance& instance);
  PowerGroupType getPowerGroupType(Instance& instance);
  bool isClockNetwork(Instance& instance);
  bool isActiveClockPin(Instance& instance, const std::string& port_name);
};

}  // namespace ipw
