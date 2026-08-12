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
#include "PPModel.hpp"

namespace ista {

#define STAPP (ista::PowerPropagator::getInst())

class PowerPropagator
{
 public:
  static void initInst();
  static PowerPropagator& getInst();
  static void destroyInst();
  // function
  void propagate();

 private:
  // self
  static PowerPropagator* _pp_instance;

  PowerPropagator() = default;
  PowerPropagator(const PowerPropagator& other) = delete;
  PowerPropagator(PowerPropagator&& other) = delete;
  ~PowerPropagator() = default;
  PowerPropagator& operator=(const PowerPropagator& other) = delete;
  PowerPropagator& operator=(PowerPropagator&& other) = delete;
  // function
  PPModel initPPModel();
  void buildMinimumClockPeriod(PPModel& pp_model);
  void buildSeedPinList(PPModel& pp_model);
  void buildSequentialInstanceNameList(PPModel& pp_model);
  void propagateActivity(PPModel& pp_model);
  void clearPowerActivity();
  void seedVcdActivity();
  bool setPinActivity(std::string& pin_name, PowerActivity& activity);
  void limitTransitionDensity(std::string& pin_name, PowerActivity& activity);
  double getMinimumSlew(std::string& pin_name);
  double getMinimumSlew(std::map<AnalysisType, std::map<TransType, double>>& slew_map);
  int32_t getActivityPriority(PowerActivityOrigin origin);
  bool isActivityChanged(PowerActivity& left_activity, PowerActivity& right_activity);
  double getRelativeChange(double value, double previous_value);
  void seedCaseAnalysisActivity();
  void seedActivity(PPModel& pp_model);
  PowerActivity getSeedActivity(std::string& pin_name, PPModel& pp_model);
  PowerActivity getClockActivity(std::string& pin_name);
  PowerActivity getInputActivity(PPModel& pp_model);
  void propagateCombinationalActivity();
  PowerActivity getPropagatedActivity(PowerActivity source_activity);
  void propagateOutputActivity(std::string& pin_name);
  PowerActivity getOutputActivity(std::string& pin_name);
  PowerActivity normalizeConstantActivity(PowerActivity activity);
  std::map<std::string, PowerActivity> getInputActivityMap(Instance& instance);
  PowerActivity getFallbackInputActivity(std::string& pin_name);
  void propagateNetActivity(Arc& arc);
  void propagateSequentialActivity(PPModel& pp_model);
  PowerActivity getSequentialOutputActivity(Instance& instance);
  PowerActivity getPinActivity(std::string& pin_name);
  bool isOutputPin(std::string& pin_name);
  bool isClockSource(std::string& pin_name);
};

}  // namespace ista
