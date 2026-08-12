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
#include "PALeakageSummary.hpp"
#include "PAModel.hpp"
#include "TimingArcSense.hpp"

namespace ista {

#define STAPA (ista::PowerAnalyzer::getInst())

class PowerAnalyzer
{
 public:
  static void initInst();
  static PowerAnalyzer& getInst();
  static void destroyInst();
  // function
  void analyze();

 private:
  // self
  static PowerAnalyzer* _pa_instance;

  PowerAnalyzer() = default;
  PowerAnalyzer(const PowerAnalyzer& other) = delete;
  PowerAnalyzer(PowerAnalyzer&& other) = delete;
  ~PowerAnalyzer() = default;
  PowerAnalyzer& operator=(const PowerAnalyzer& other) = delete;
  PowerAnalyzer& operator=(PowerAnalyzer&& other) = delete;
  // function
  PAModel initPAModel();
  void buildInstanceNameList(PAModel& pa_model);
  void analyzePower(PAModel& pa_model);
  InstancePower analyzeInstancePower(std::string& instance_name);
  PowerValue getInstancePowerValue(Instance& instance);
  void analyzeInternalPower(Instance& instance, PowerValue& power_value);
  double getTimingPowerArcPower(Instance& instance, TimingPowerArc& timing_power_arc);
  double getInputTimingPowerArcPower(Instance& instance, TimingPowerArc& timing_power_arc);
  double getInputTimingPowerArcConditionProbability(Instance& instance, TimingPowerArc& timing_power_arc);
  double getOutputTimingPowerArcPower(Instance& instance, TimingPowerArc& timing_power_arc);
  double getOutputTimingPowerArcWeight(Instance& instance, TimingPowerArc& timing_power_arc);
  double getOutputTimingPowerArcConditionProbability(Instance& instance, TimingPowerArc& timing_power_arc);
  double getOutputTimingPowerArcWeightSum(Instance& instance, TimingPowerArc& timing_power_arc);
  double getTimingPowerArcEnergy(Instance& instance, TimingPowerArc& timing_power_arc, TransType trans_type);
  double getTimingPowerArcInputSlew(Instance& instance, TimingPowerArc& timing_power_arc, TransType trans_type);
  TimingArcSense getTimingPowerArcSense(Instance& instance, TimingPowerArc& timing_power_arc);
  double getTimingPowerArcOutputLoad(Instance& instance, TimingPowerArc& timing_power_arc, TransType trans_type);
  double getTimingPowerArcConditionProbability(Instance& instance, TimingPowerArc& timing_power_arc);
  void analyzeSwitchingPower(Instance& instance, PowerValue& power_value);
  void analyzeLeakagePower(Instance& instance, PowerValue& power_value);
  double getLeakageConditionProbability(Instance& instance, TimingLeakagePower& timing_leakage_power);
  std::map<std::string, PowerActivity> getPortActivityMap(Instance& instance);
  PowerActivity getPinActivity(std::string& pin_name);
  double getPinSlew(std::string& pin_name, TransType trans_type);
  double getInstanceVoltage(Instance& instance);
  PowerGroupType getPowerGroupType(Instance& instance);
  bool isClockNetwork(Instance& instance);
  void updatePowerSummary(PAModel& pa_model);
};

}  // namespace ista
