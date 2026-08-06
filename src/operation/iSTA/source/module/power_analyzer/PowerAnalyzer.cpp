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
#include "PowerAnalyzer.hpp"

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace ista {

// public

void PowerAnalyzer::initInst()
{
  if (_pa_instance == nullptr) {
    _pa_instance = new PowerAnalyzer();
  }
}

PowerAnalyzer& PowerAnalyzer::getInst()
{
  if (_pa_instance == nullptr) {
    STALOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_pa_instance;
}

void PowerAnalyzer::destroyInst()
{
  if (_pa_instance != nullptr) {
    delete _pa_instance;
    _pa_instance = nullptr;
  }
}

// function

void PowerAnalyzer::analyze()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  PAModel pa_model = initPAModel();
  analyzePower(pa_model);

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

PowerAnalyzer* PowerAnalyzer::_pa_instance = nullptr;

PAModel PowerAnalyzer::initPAModel()
{
  PAModel pa_model;
  buildInstanceNameList(pa_model);
  return pa_model;
}

void PowerAnalyzer::buildInstanceNameList(PAModel& pa_model)
{
  Database& database = STADM.getDatabase();
  std::vector<std::string> instance_name_list;
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    instance_name_list.push_back(instance_pair.first);
  }
  pa_model.set_instance_name_list(instance_name_list);
  for (PowerGroupType power_group_type : GetPowerGroupTypeList()()) {
    pa_model.get_group_power_map()[power_group_type] = PowerValue();
  }
}

void PowerAnalyzer::analyzePower(PAModel& pa_model)
{
  Database& database = STADM.getDatabase();
  database.get_instance_power_map().clear();
  for (std::string& instance_name : pa_model.get_instance_name_list()) {
    InstancePower instance_power = analyzeInstancePower(instance_name);
    database.get_instance_power_map()[instance_name] = instance_power;
    pa_model.get_group_power_map()[instance_power.get_power_group_type()].add_power_value(instance_power.get_power_value());
  }
  updatePowerSummary(pa_model);
}

InstancePower PowerAnalyzer::analyzeInstancePower(std::string& instance_name)
{
  Database& database = STADM.getDatabase();
  InstancePower instance_power;
  if (database.get_instance_map().count(instance_name) == 0) {
    return instance_power;
  }
  Instance& instance = database.get_instance_map()[instance_name];
  instance_power.set_instance_id(instance.get_instance_id());
  instance_power.set_instance_name(instance.get_instance_name());
  instance_power.set_power_group_type(getPowerGroupType(instance));
  instance_power.set_voltage(getInstanceVoltage(instance));
  instance_power.set_power_value(getInstancePowerValue(instance));
  return instance_power;
}

PowerValue PowerAnalyzer::getInstancePowerValue(Instance& instance)
{
  PowerValue power_value;
  analyzeInternalPower(instance, power_value);
  analyzeSwitchingPower(instance, power_value);
  analyzeLeakagePower(instance, power_value);
  return power_value;
}

void PowerAnalyzer::analyzeInternalPower(Instance& instance, PowerValue& power_value)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return;
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  for (TimingPowerArc& timing_power_arc : timing_cell.get_power_arc_list()) {
    power_value.add_internal_power(getTimingPowerArcPower(instance, timing_power_arc));
  }
}

double PowerAnalyzer::getTimingPowerArcPower(Instance& instance, TimingPowerArc& timing_power_arc)
{
  if (timing_power_arc.get_source_port().empty()) {
    return getInputTimingPowerArcPower(instance, timing_power_arc);
  }
  return getOutputTimingPowerArcPower(instance, timing_power_arc);
}

double PowerAnalyzer::getInputTimingPowerArcPower(Instance& instance, TimingPowerArc& timing_power_arc)
{
  if (timing_power_arc.get_sink_port().empty()) {
    return 0.0;
  }
  std::string sink_pin_name = instance.get_instance_name() + ":" + timing_power_arc.get_sink_port();
  PowerActivity sink_activity = getPinActivity(sink_pin_name);
  if (!sink_activity.get_is_valid()) {
    return 0.0;
  }
  double condition_probability = getInputTimingPowerArcConditionProbability(instance, timing_power_arc);
  if (condition_probability <= 0.0) {
    return 0.0;
  }
  double internal_power = 0.0;
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    double transition_density = trans_type == TransType::kRise ? sink_activity.get_rise_transition_density()
                                                                 : sink_activity.get_fall_transition_density();
    double energy = getTimingPowerArcEnergy(instance, timing_power_arc, trans_type);
    internal_power += energy * transition_density * condition_probability * 1E-3;
  }
  return internal_power;
}

double PowerAnalyzer::getInputTimingPowerArcConditionProbability(Instance& instance, TimingPowerArc& timing_power_arc)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return 0.0;
  }
  if (timing_power_arc.get_when_expression().get_is_empty()) {
    return 1.0;
  }

  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  std::string input_port_name = timing_power_arc.get_sink_port();
  for (std::pair<const std::string, TimingCellPort>& port_pair : timing_cell.get_port_map()) {
    TimingCellPort& timing_cell_port = port_pair.second;
    std::string output_port_name = port_pair.first;
    if (!timing_cell_port.get_is_output() || !timing_power_arc.get_when_expression().get_has_port(output_port_name)
        || timing_cell_port.get_function_expression().get_is_empty()
        || !timing_cell_port.get_function_expression().get_has_port(input_port_name)) {
      continue;
    }
    std::map<std::string, PowerActivity> port_activity_map = getPortActivityMap(instance);
    return timing_cell_port.get_function_expression().get_sensitivity_probability(input_port_name, port_activity_map);
  }
  return getTimingPowerArcConditionProbability(instance, timing_power_arc);
}

double PowerAnalyzer::getOutputTimingPowerArcPower(Instance& instance, TimingPowerArc& timing_power_arc)
{
  if (timing_power_arc.get_sink_port().empty()) {
    return 0.0;
  }
  std::string sink_pin_name = instance.get_instance_name() + ":" + timing_power_arc.get_sink_port();
  PowerActivity sink_activity = getPinActivity(sink_pin_name);
  if (!sink_activity.get_is_valid()) {
    return 0.0;
  }
  double timing_power_arc_weight = getOutputTimingPowerArcWeight(instance, timing_power_arc);
  double timing_power_arc_weight_sum = getOutputTimingPowerArcWeightSum(instance, timing_power_arc);
  if (timing_power_arc_weight <= STA_ERROR || timing_power_arc_weight_sum <= STA_ERROR) {
    return 0.0;
  }
  double internal_power = 0.0;
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    double transition_density = trans_type == TransType::kRise ? sink_activity.get_rise_transition_density()
                                                                 : sink_activity.get_fall_transition_density();
    double energy = getTimingPowerArcEnergy(instance, timing_power_arc, trans_type);
    internal_power += energy * transition_density * timing_power_arc_weight / timing_power_arc_weight_sum * 1E-3;
  }
  return internal_power;
}

double PowerAnalyzer::getOutputTimingPowerArcWeight(Instance& instance, TimingPowerArc& timing_power_arc)
{
  std::string source_pin_name = instance.get_instance_name() + ":" + timing_power_arc.get_source_port();
  PowerActivity source_activity = getPinActivity(source_pin_name);
  if (!source_activity.get_is_valid()) {
    return 0.0;
  }
  double condition_probability = getOutputTimingPowerArcConditionProbability(instance, timing_power_arc);
  return source_activity.get_transition_density() * condition_probability;
}

double PowerAnalyzer::getOutputTimingPowerArcConditionProbability(Instance& instance, TimingPowerArc& timing_power_arc)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return 0.0;
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  if (timing_cell.get_port_map().count(timing_power_arc.get_sink_port()) == 0) {
    return 0.0;
  }
  TimingCellPort& output_port = timing_cell.get_port_map()[timing_power_arc.get_sink_port()];
  if (!output_port.get_function_expression().get_is_empty()
      && output_port.get_function_expression().get_has_port(timing_power_arc.get_source_port())) {
    std::map<std::string, PowerActivity> port_activity_map = getPortActivityMap(instance);
    return output_port.get_function_expression().get_sensitivity_probability(timing_power_arc.get_source_port(), port_activity_map);
  }
  if (!timing_power_arc.get_when_expression().get_is_empty()) {
    return getTimingPowerArcConditionProbability(instance, timing_power_arc);
  }
  return 0.5;
}

double PowerAnalyzer::getOutputTimingPowerArcWeightSum(Instance& instance, TimingPowerArc& timing_power_arc)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return 0.0;
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  double timing_power_arc_weight_sum = 0.0;
  for (TimingPowerArc& candidate_power_arc : timing_cell.get_power_arc_list()) {
    if (candidate_power_arc.get_source_port().empty() || candidate_power_arc.get_sink_port() != timing_power_arc.get_sink_port()
        || candidate_power_arc.get_related_pg_port() != timing_power_arc.get_related_pg_port()) {
      continue;
    }
    timing_power_arc_weight_sum += getOutputTimingPowerArcWeight(instance, candidate_power_arc);
  }
  return timing_power_arc_weight_sum;
}

double PowerAnalyzer::getTimingPowerArcEnergy(Instance& instance, TimingPowerArc& timing_power_arc, TransType trans_type)
{
  if (timing_power_arc.get_energy_table_map().count(trans_type) == 0) {
    return 0.0;
  }
  double input_slew = getTimingPowerArcInputSlew(instance, timing_power_arc, trans_type);
  double output_load = getTimingPowerArcOutputLoad(instance, timing_power_arc, trans_type);
  TimingTable& energy_table = timing_power_arc.get_energy_table_map()[trans_type];
  return energy_table.findValue(input_slew * timing_power_arc.get_time_unit_scale(), output_load * timing_power_arc.get_cap_unit_scale());
}

double PowerAnalyzer::getTimingPowerArcInputSlew(Instance& instance, TimingPowerArc& timing_power_arc, TransType trans_type)
{
  std::string port_name = timing_power_arc.get_source_port();
  if (port_name.empty()) {
    port_name = timing_power_arc.get_sink_port();
  }
  std::string pin_name = instance.get_instance_name() + ":" + port_name;
  TransType input_trans_type = trans_type;
  if (!timing_power_arc.get_source_port().empty() && getTimingPowerArcSense(instance, timing_power_arc) == TimingArcSense::kNegative) {
    input_trans_type = trans_type == TransType::kRise ? TransType::kFall : TransType::kRise;
  }
  return getPinSlew(pin_name, input_trans_type);
}

TimingArcSense PowerAnalyzer::getTimingPowerArcSense(Instance& instance, TimingPowerArc& timing_power_arc)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return TimingArcSense::kNone;
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_source_port() != timing_power_arc.get_source_port()
        || timing_cell_arc.get_sink_port() != timing_power_arc.get_sink_port()) {
      continue;
    }
    for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
      if (timing_arc.get_sense() != TimingArcSense::kNone) {
        return timing_arc.get_sense();
      }
    }
  }
  return TimingArcSense::kNone;
}

double PowerAnalyzer::getTimingPowerArcOutputLoad(Instance& instance, TimingPowerArc& timing_power_arc, TransType trans_type)
{
  if (timing_power_arc.get_source_port().empty()) {
    return 0.0;
  }
  std::string output_pin_name = instance.get_instance_name() + ":" + timing_power_arc.get_sink_port();
  return STADC.getPowerOutputLoad(output_pin_name, AnalysisType::kMax, trans_type);
}

double PowerAnalyzer::getTimingPowerArcConditionProbability(Instance& instance, TimingPowerArc& timing_power_arc)
{
  if (timing_power_arc.get_when_expression().get_is_empty()) {
    return 1.0;
  }
  std::map<std::string, PowerActivity> port_activity_map = getPortActivityMap(instance);
  PowerActivity activity = timing_power_arc.get_when_expression().evaluate_activity(port_activity_map);
  return activity.get_is_valid() ? activity.get_static_probability() : 0.0;
}

void PowerAnalyzer::analyzeSwitchingPower(Instance& instance, PowerValue& power_value)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return;
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  double voltage = getInstanceVoltage(instance);
  for (std::pair<const std::string, TimingCellPort>& port_pair : timing_cell.get_port_map()) {
    TimingCellPort& timing_cell_port = port_pair.second;
    if (!timing_cell_port.get_is_output()) {
      continue;
    }
    std::string output_pin_name = instance.get_instance_name() + ":" + port_pair.first;
    PowerActivity activity = getPinActivity(output_pin_name);
    if (!activity.get_is_valid()) {
      continue;
    }
    for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
      double transition_density = trans_type == TransType::kRise ? activity.get_rise_transition_density() : activity.get_fall_transition_density();
      double output_load = STADC.getPowerOutputLoad(output_pin_name, AnalysisType::kMax, trans_type);
      power_value.add_switching_power(0.5 * output_load * voltage * voltage * transition_density * 1E-3);
    }
  }
}

void PowerAnalyzer::analyzeLeakagePower(Instance& instance, PowerValue& power_value)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return;
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  if (timing_cell.get_leakage_power_list().empty()) {
    power_value.add_leakage_power(timing_cell.get_cell_leakage_power());
    return;
  }
  PALeakageSummary leakage_summary;
  for (TimingLeakagePower& timing_leakage_power : timing_cell.get_leakage_power_list()) {
    if (timing_leakage_power.get_when_expression().get_is_empty()) {
      leakage_summary.add_unconditional_leakage_power(timing_leakage_power.get_leakage_power());
    } else {
      leakage_summary.add_conditional_leakage_power(timing_leakage_power.get_leakage_power(),
                                                     getLeakageConditionProbability(instance, timing_leakage_power));
    }
  }
  power_value.add_leakage_power(leakage_summary.get_leakage_power(timing_cell.get_cell_leakage_power()));
}

double PowerAnalyzer::getLeakageConditionProbability(Instance& instance, TimingLeakagePower& timing_leakage_power)
{
  std::map<std::string, PowerActivity> port_activity_map = getPortActivityMap(instance);
  PowerActivity activity = timing_leakage_power.get_when_expression().evaluate_activity(port_activity_map);
  return activity.get_is_valid() ? activity.get_static_probability() : 0.0;
}

std::map<std::string, PowerActivity> PowerAnalyzer::getPortActivityMap(Instance& instance)
{
  Database& database = STADM.getDatabase();
  std::map<std::string, PowerActivity> port_activity_map;
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return port_activity_map;
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  for (std::pair<const std::string, TimingCellPort>& port_pair : timing_cell.get_port_map()) {
    std::string pin_name = instance.get_instance_name() + ":" + port_pair.first;
    port_activity_map[port_pair.first] = getPinActivity(pin_name);
  }
  return port_activity_map;
}

PowerActivity PowerAnalyzer::getPinActivity(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  if (database.get_power_activity_map().count(pin_name) == 0) {
    return PowerActivity();
  }
  return database.get_power_activity_map()[pin_name];
}

double PowerAnalyzer::getPinSlew(std::string& pin_name, TransType trans_type)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_point_map().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
  if (timing_point.get_data_slew_map().count(AnalysisType::kMax) > 0
      && timing_point.get_data_slew_map()[AnalysisType::kMax].count(trans_type) > 0) {
    return timing_point.get_data_slew_map()[AnalysisType::kMax][trans_type];
  }
  if (timing_point.get_clock_slew_map().count(AnalysisType::kMax) > 0
      && timing_point.get_clock_slew_map()[AnalysisType::kMax].count(trans_type) > 0) {
    return timing_point.get_clock_slew_map()[AnalysisType::kMax][trans_type];
  }
  return 0.0;
}

double PowerAnalyzer::getInstanceVoltage(Instance& instance)
{
  Database& database = STADM.getDatabase();
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) > 0) {
    double voltage = database.get_timing_library().get_cell_map()[instance.get_cell_name()].get_nom_voltage();
    if (voltage > STA_ERROR) {
      return voltage;
    }
  }
  if (database.get_timing_library().get_nom_voltage() > STA_ERROR) {
    return database.get_timing_library().get_nom_voltage();
  }
  return 1.0;
}

PowerGroupType PowerAnalyzer::getPowerGroupType(Instance& instance)
{
  Database& database = STADM.getDatabase();
  if (instance.get_is_io_cell()) {
    return PowerGroupType::kIOPad;
  }
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return PowerGroupType::kBlackBox;
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  if (timing_cell.get_is_macro()) {
    return PowerGroupType::kMemory;
  }
  if (isClockNetwork(instance)) {
    return PowerGroupType::kClockNetwork;
  }
  if (timing_cell.get_is_sequential()) {
    return PowerGroupType::kRegister;
  }
  return PowerGroupType::kCombinational;
}

bool PowerAnalyzer::isClockNetwork(Instance& instance)
{
  Database& database = STADM.getDatabase();
  bool has_output_pin = false;
  for (std::string& pin_name : instance.get_pin_name_list()) {
    if (database.get_pin_map().count(pin_name) == 0) {
      continue;
    }
    Pin& pin = database.get_pin_map()[pin_name];
    if (pin.get_direction() != PinDirection::kOutput && pin.get_direction() != PinDirection::kInout) {
      continue;
    }
    has_output_pin = true;
    if (database.get_timing_point_map().count(pin_name) == 0 || !database.get_timing_point_map()[pin_name].get_is_clock_point()) {
      return false;
    }
  }
  return has_output_pin;
}

void PowerAnalyzer::updatePowerSummary(PAModel& pa_model)
{
  Database& database = STADM.getDatabase();
  PowerSummary power_summary;
  power_summary.set_group_power_map(pa_model.get_group_power_map());
  for (std::pair<const PowerGroupType, PowerValue>& power_pair : pa_model.get_group_power_map()) {
    PowerValue power_value = power_pair.second;
    power_summary.get_total_power_value().add_power_value(power_value);
  }
  database.get_power_summary() = power_summary;
}

}  // namespace ista
