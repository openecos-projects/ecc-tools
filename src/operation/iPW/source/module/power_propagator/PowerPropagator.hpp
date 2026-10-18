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
#include "VcdParser.hh"

namespace ipw {

#define PWPP (ipw::PowerPropagator::getInst())

class PowerPropagator
{
 public:
  static void initInst();
  // Once per run: per-pin minimum slew is window independent (slews only change
  // in signal propagation), so the peak flow can read it from a flat vector
  // instead of re-walking the timing point slew maps for every window.
  void prepareStaticContext();
  static PowerPropagator& getInst();
  static void destroyInst();
  // function
  void propagate();
  static std::size_t annotateVcdActivity(const std::map<std::string, vcd::VcdSignalActivity>& signal_activity_map);
  static std::size_t annotateVcdActivity(const vcd::VcdWindowSignalActivity& window_activity);

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
  PPModel& initPPModel();
  void buildMinimumClockPeriod(PPModel& pp_model);
  void buildSeedPinList(PPModel& pp_model);
  void buildSequentialInstanceNameList(PPModel& pp_model);
  bool isSequentialForPower(Instance& instance);
  void propagateActivity(PPModel& pp_model);
  void clearPowerActivity();
  void seedVcdActivity();
  static bool annotateVcdSignal(const std::string& vcd_signal_name, PowerActivity& activity);
  static std::string wrapVcdPinName(const std::string& vcd_signal_name);
  bool setPinActivity(std::string& pin_name, PowerActivity& activity);
  bool setPinActivity(uint32_t pin_index, PowerActivity& activity);
  void limitTransitionDensity(std::string& pin_name, PowerActivity& activity);
  void limitTransitionDensity(uint32_t pin_index, PowerActivity& activity);
  double getMinimumSlew(std::string& pin_name);
  double getCachedMinimumSlew(std::string& pin_name);
  double getCachedMinimumSlew(uint32_t pin_index);
  double getMinimumSlew(std::map<AnalysisType, std::map<TransType, double>>& slew_map);
  int32_t getActivityPriority(PowerActivityOrigin origin);
  bool isActivityChanged(PowerActivity& left_activity, PowerActivity& right_activity);
  double getRelativeChange(double value, double previous_value);
  void seedCaseAnalysisActivity();
  void seedActivity(PPModel& pp_model);
  PowerActivity getSeedActivity(std::string& pin_name, PPModel& pp_model);
  PowerActivity getClockActivity(std::string& pin_name);
  PowerActivity getClockActivity(TimingClock& timing_clock);
  PowerActivity getInputActivity(PPModel& pp_model);
  PowerActivity getDefaultInputActivity(double minimum_clock_period);
  double getDefaultTransitionDensity(double minimum_clock_period);
  void seedDriverActivity(PPModel& pp_model);
  void seedSequentialStateActivity(PPModel& pp_model);
  PowerActivity getInitialSequentialOutputActivity();
  void propagateCombinationalActivity(PPModel& pp_model);
  PowerActivity getPropagatedActivity(PowerActivity source_activity);
  void propagateOutputActivity(uint32_t pin_index, PPModel& pp_model);
  PowerActivity getOutputActivity(uint32_t pin_index, PPModel& pp_model);
  void limitDataActivity(
      Database& database,
      uint32_t pin_index,
      PowerActivity& activity,
      double minimum_clock_period
  );
  bool shouldLimitDataActivity(Database& database, uint32_t pin_index, PowerActivity& activity);
  double getProbabilityLimitedTransitionDensity(PowerActivity& activity, double minimum_clock_period);
  void scaleTransitionDensity(PowerActivity& activity, double maximum_transition_density);
  PowerActivity getClockGateOutputActivity(std::string& pin_name, Instance& instance);
  PowerActivity getClockGateOutputActivity(PowerActivity& clock_activity, PowerActivity& enable_activity);
  PowerActivity getClockGateEnableActivity(
      Instance& instance,
      std::string& clock_pin_name,
      std::string& output_pin_name
  );
  bool isClockGateOutputPin(std::string& pin_name, Instance& instance);
  bool isClockGateClockPin(std::string& pin_name, TimingCellPort& timing_cell_port);
  PowerActivity normalizeConstantActivity(PowerActivity activity);
  std::map<std::string, PowerActivity> getInputActivityMap(Instance& instance);
  PowerActivity getFallbackInputActivity(uint32_t pin_index);
  void propagateNetActivity(Arc& arc);
  void propagateSequentialActivity(PPModel& pp_model);
  PowerActivity getSequentialOutputActivity(Instance& instance);
  void limitSequentialOutputActivity(
      PowerActivity& output_activity,
      PowerActivity& data_activity,
      PowerActivity& clock_activity
  );
  PowerActivity getPinActivity(std::string& pin_name);
  PowerActivity getPinActivity(uint32_t pin_index);
  bool isOutputPin(uint32_t pin_index);
  bool isClockSource(std::string& pin_name);
  bool simulateVectorlessActivity(double reference_period);

  // indexed by Pin::get_index()
  std::vector<double> _pin_minimum_slew_list;
  bool _is_static_context_ready = false;
};

}  // namespace ipw
