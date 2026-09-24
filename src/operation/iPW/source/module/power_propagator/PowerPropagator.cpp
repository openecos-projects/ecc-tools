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
#include "PowerPropagator.hpp"

#include <bit>
#include <cstdlib>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ipw {

// public

void PowerPropagator::initInst()
{
  if (_pp_instance == nullptr) {
    _pp_instance = new PowerPropagator();
  }
}

PowerPropagator& PowerPropagator::getInst()
{
  if (_pp_instance == nullptr) {
    PWLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_pp_instance;
}

void PowerPropagator::destroyInst()
{
  if (_pp_instance != nullptr) {
    delete _pp_instance;
    _pp_instance = nullptr;
  }
}

// function

void PowerPropagator::propagate()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  PPModel pp_model = initPPModel();
  propagateActivity(pp_model);

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

PowerPropagator* PowerPropagator::_pp_instance = nullptr;

PPModel PowerPropagator::initPPModel()
{
  PPModel pp_model;
  buildMinimumClockPeriod(pp_model);
  buildSeedPinList(pp_model);
  buildSequentialInstanceNameList(pp_model);
  return pp_model;
}

void PowerPropagator::buildMinimumClockPeriod(PPModel& pp_model)
{
  Database& database = PWDM.getDatabase();
  double minimum_clock_period = 0.0;
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    double period = clock_pair.second.get_period();
    if (period <= PW_ERROR) {
      continue;
    }
    if (minimum_clock_period <= PW_ERROR || period < minimum_clock_period) {
      minimum_clock_period = period;
    }
  }
  if (minimum_clock_period <= PW_ERROR) {
    minimum_clock_period = 1.0;
  }
  pp_model.set_minimum_clock_period(minimum_clock_period);
}

void PowerPropagator::buildSeedPinList(PPModel& pp_model)
{
  Database& database = PWDM.getDatabase();
  std::vector<std::string>& seed_pin_list = pp_model.get_seed_pin_list();
  seed_pin_list.clear();
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    for (std::string& source_pin : clock_pair.second.get_source_list()) {
      if (database.get_pin_map().count(source_pin) > 0 && !PWUTIL.exist(seed_pin_list, source_pin)) {
        seed_pin_list.push_back(source_pin);
      }
    }
  }
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (!pin.get_is_port() || (pin.get_direction() != PinDirection::kInput && pin.get_direction() != PinDirection::kInout)) {
      continue;
    }
    if (!PWUTIL.exist(seed_pin_list, pin_pair.first)) {
      seed_pin_list.push_back(pin_pair.first);
    }
  }
}

void PowerPropagator::buildSequentialInstanceNameList(PPModel& pp_model)
{
  Database& database = PWDM.getDatabase();
  std::vector<std::string>& sequential_instance_name_list = pp_model.get_sequential_instance_name_list();
  sequential_instance_name_list.clear();
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    if (isSequentialForPower(instance_pair.second)) {
      sequential_instance_name_list.push_back(instance_pair.first);
    }
  }
}

bool PowerPropagator::isSequentialForPower(Instance& instance)
{
  if (instance.get_is_sequential()) return true;
  auto& cells = PWDM.getDatabase().get_timing_library().get_cell_map();
  auto cell = cells.find(instance.get_cell_name());
  return cell != cells.end() && cell->second.get_is_sequential_for_power();
}

void PowerPropagator::propagateActivity(PPModel& pp_model)
{
  clearPowerActivity();
  seedVcdActivity();
  seedCaseAnalysisActivity();
  seedActivity(pp_model);
  if (PWDM.getDatabase().get_vcd_activity_map().empty()) {
    if (simulateVectorlessActivity(pp_model.get_minimum_clock_period())) return;
    PWLOG.info(Loc::current(), "Vector simulation unavailable for this design; using analytical activity.");
  }
  seedSequentialStateActivity(pp_model);
  propagateCombinationalActivity(pp_model);
  propagateSequentialActivity(pp_model);
}

void PowerPropagator::clearPowerActivity()
{
  PWDM.getDatabase().get_power_activity_map().clear();
}

void PowerPropagator::seedVcdActivity()
{
  Database& database = PWDM.getDatabase();
  if (database.get_vcd_activity_map().empty()) {
    return;
  }
  std::size_t annotated_pin_num = 0;
  for (std::pair<const std::string, PowerActivity>& activity_pair : database.get_vcd_activity_map()) {
    std::string pin_name = activity_pair.first;
    PowerActivity activity = activity_pair.second;
    if (setPinActivity(pin_name, activity)) {
      annotated_pin_num++;
    }
  }
  PWLOG.info(Loc::current(), "Annotated ", annotated_pin_num, " power activities from VCD.");
}

bool PowerPropagator::setPinActivity(std::string& pin_name, PowerActivity& activity)
{
  Database& database = PWDM.getDatabase();
  limitTransitionDensity(pin_name, activity);
  if (database.get_power_activity_map().count(pin_name) == 0) {
    database.get_power_activity_map()[pin_name] = activity;
    return true;
  }
  PowerActivity& current_activity = database.get_power_activity_map()[pin_name];
  if (getActivityPriority(current_activity.get_origin()) > getActivityPriority(activity.get_origin())) {
    return false;
  }
  if (!isActivityChanged(current_activity, activity)) {
    return false;
  }
  current_activity = activity;
  return true;
}

void PowerPropagator::limitTransitionDensity(std::string& pin_name, PowerActivity& activity)
{
  double minimum_slew = getMinimumSlew(pin_name);
  if (minimum_slew <= PW_ERROR) {
    return;
  }
  double maximum_transition_density = 1.0 / minimum_slew;
  double transition_density = activity.get_transition_density();
  if (transition_density <= maximum_transition_density) {
    return;
  }
  double density_scale = maximum_transition_density / transition_density;
  activity.set_rise_transition_density(activity.get_rise_transition_density() * density_scale);
  activity.set_fall_transition_density(activity.get_fall_transition_density() * density_scale);
}

double PowerPropagator::getMinimumSlew(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  if (database.get_timing_point_map().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
  double data_minimum_slew = getMinimumSlew(timing_point.get_data_slew_map());
  double clock_minimum_slew = getMinimumSlew(timing_point.get_clock_slew_map());
  if (data_minimum_slew <= PW_ERROR) {
    return clock_minimum_slew;
  }
  if (clock_minimum_slew <= PW_ERROR) {
    return data_minimum_slew;
  }
  return std::min(data_minimum_slew, clock_minimum_slew);
}

double PowerPropagator::getMinimumSlew(std::map<AnalysisType, std::map<TransType, double>>& slew_map)
{
  double minimum_slew = std::numeric_limits<double>::infinity();
  for (AnalysisType analysis_type : {AnalysisType::kMax, AnalysisType::kMin}) {
    if (slew_map.count(analysis_type) == 0 || slew_map[analysis_type].count(TransType::kRise) == 0 || slew_map[analysis_type].count(TransType::kFall) == 0) {
      continue;
    }
    double average_slew = (slew_map[analysis_type][TransType::kRise] + slew_map[analysis_type][TransType::kFall]) / 2.0;
    if (average_slew > PW_ERROR) {
      minimum_slew = std::min(minimum_slew, average_slew);
    }
  }
  return std::isfinite(minimum_slew) ? minimum_slew : 0.0;
}

int32_t PowerPropagator::getActivityPriority(PowerActivityOrigin origin)
{
  switch (origin) {
    case PowerActivityOrigin::kVcd:
      return 4;
    case PowerActivityOrigin::kConstant:
      return 3;
    case PowerActivityOrigin::kClock:
      return 2;
    case PowerActivityOrigin::kInput:
      return 1;
    case PowerActivityOrigin::kPropagated:
    case PowerActivityOrigin::kSequential:
      return 0;
    case PowerActivityOrigin::kNone:
      return -1;
    default:
      PWLOG.error(Loc::current(), "Unrecognized type!");
      break;
  }
  return -1;
}

bool PowerPropagator::isActivityChanged(PowerActivity& left_activity, PowerActivity& right_activity)
{
  return left_activity.get_is_valid() != right_activity.get_is_valid() || left_activity.get_origin() != right_activity.get_origin()
         || getRelativeChange(left_activity.get_rise_transition_density(), right_activity.get_rise_transition_density()) > 0.01
         || getRelativeChange(left_activity.get_fall_transition_density(), right_activity.get_fall_transition_density()) > 0.01
         || getRelativeChange(left_activity.get_static_probability(), right_activity.get_static_probability()) > 0.01;
}

double PowerPropagator::getRelativeChange(double value, double previous_value)
{
  if (std::fabs(previous_value) <= PW_ERROR) {
    return std::fabs(value) <= PW_ERROR ? 0.0 : 1.0;
  }
  return std::fabs(value - previous_value) / std::fabs(previous_value);
}

void PowerPropagator::seedCaseAnalysisActivity()
{
  Database& database = PWDM.getDatabase();
  for (auto& case_pair : database.get_timing_constraint().get_case_analysis_map()) {
    if (case_pair.second != TimingCaseValue::kZero && case_pair.second != TimingCaseValue::kOne) {
      continue;
    }
    PowerActivity activity;
    activity.set_static_probability(case_pair.second == TimingCaseValue::kOne ? 1.0 : 0.0);
    activity.set_origin(PowerActivityOrigin::kConstant);
    activity.set_is_valid(true);
    std::string pin_name = case_pair.first;
    setPinActivity(pin_name, activity);
  }
}

void PowerPropagator::seedActivity(PPModel& pp_model)
{
  for (std::string& pin_name : pp_model.get_seed_pin_list()) {
    PowerActivity activity = getSeedActivity(pin_name, pp_model);
    if (activity.get_is_valid()) {
      setPinActivity(pin_name, activity);
    }
  }
  // Undriven nets are also activity roots. Without a default annotation their
  // loads have no activity at all, dropping clock-pin power and invalidating
  // every state-dependent table involving such a pin.
  Database& database = PWDM.getDatabase();
  for (auto& [net_name, net] : database.get_net_map()) {
    if (!net.get_driver_pin_list().empty()) {
      continue;
    }
    for (std::string& pin_name : net.get_load_pin_list()) {
      PowerActivity activity = getInputActivity(pp_model);
      setPinActivity(pin_name, activity);
    }
  }
}

PowerActivity PowerPropagator::getSeedActivity(std::string& pin_name, PPModel& pp_model)
{
  if (isClockSource(pin_name)) {
    PowerActivity activity = getClockActivity(pin_name);
    if (activity.get_is_valid()) {
      return activity;
    }
  }
  return getInputActivity(pp_model);
}

PowerActivity PowerPropagator::getClockActivity(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    TimingClock& timing_clock = clock_pair.second;
    if (!PWUTIL.exist(timing_clock.get_source_list(), pin_name) || timing_clock.get_period() <= PW_ERROR) {
      continue;
    }
    return getClockActivity(timing_clock);
  }
  return PowerActivity();
}

PowerActivity PowerPropagator::getClockActivity(TimingClock& timing_clock)
{
  if (timing_clock.get_period() <= PW_ERROR) {
    return PowerActivity();
  }

  double duty = (timing_clock.get_fall_edge() - timing_clock.get_rise_edge()) / timing_clock.get_period();
  if (duty <= PW_ERROR || duty >= 1.0 - PW_ERROR) {
    duty = 0.5;
  }

  PowerActivity activity;
  activity.set_transition_density(2.0 / timing_clock.get_period());
  activity.set_static_probability(duty);
  activity.set_origin(PowerActivityOrigin::kClock);
  activity.set_is_valid(true);
  return activity;
}

PowerActivity PowerPropagator::getInputActivity(PPModel& pp_model)
{
  return getDefaultInputActivity(pp_model.get_minimum_clock_period());
}

PowerActivity PowerPropagator::getDefaultInputActivity(double minimum_clock_period)
{
  PowerActivity activity;
  activity.set_transition_density(getDefaultTransitionDensity(minimum_clock_period));
  activity.set_static_probability(0.5);
  activity.set_origin(PowerActivityOrigin::kInput);
  activity.set_is_valid(minimum_clock_period > PW_ERROR);
  return activity;
}

double PowerPropagator::getDefaultTransitionDensity(double minimum_clock_period)
{
  return minimum_clock_period > PW_ERROR ? 0.1 / minimum_clock_period : 0.0;
}

void PowerPropagator::seedSequentialStateActivity(PPModel& pp_model)
{
  Database& database = PWDM.getDatabase();
  for (std::string& instance_name : pp_model.get_sequential_instance_name_list()) {
    if (database.get_instance_map().count(instance_name) == 0) {
      continue;
    }
    Instance& instance = database.get_instance_map()[instance_name];
    if (instance.get_output_pin_name().empty()) {
      continue;
    }
    std::string output_pin_name = instance.get_output_pin_name();
    PowerActivity activity = getInitialSequentialOutputActivity();
    setPinActivity(output_pin_name, activity);
  }
}

PowerActivity PowerPropagator::getInitialSequentialOutputActivity()
{
  PowerActivity activity;
  activity.set_static_probability(0.5);
  activity.set_origin(PowerActivityOrigin::kSequential);
  activity.set_is_valid(true);
  return activity;
}

void PowerPropagator::propagateCombinationalActivity(PPModel& pp_model)
{
  Database& database = PWDM.getDatabase();
  for (std::string& pin_name : database.get_signal_order_list()) {
    if (isOutputPin(pin_name)) {
      propagateOutputActivity(pin_name, pp_model);
    }
    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (arc.get_is_disable_arc() || arc.get_is_loop_disable()) {
        continue;
      }
      if (arc.get_type() == ArcType::kNet) {
        propagateNetActivity(arc);
      } else if (arc.get_type() == ArcType::kCell && !isOutputPin(arc.get_sink_pin())) {
        PowerActivity activity = getPinActivity(arc.get_source_pin());
        if (activity.get_is_valid()) {
          activity = getPropagatedActivity(activity);
          setPinActivity(arc.get_sink_pin(), activity);
        }
      }
    }
  }
}

PowerActivity PowerPropagator::getPropagatedActivity(PowerActivity source_activity)
{
  if (source_activity.get_origin() != PowerActivityOrigin::kConstant) {
    source_activity.set_origin(PowerActivityOrigin::kPropagated);
  }
  return source_activity;
}

void PowerPropagator::propagateOutputActivity(std::string& pin_name, PPModel& pp_model)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (database.get_instance_map().count(pin.get_instance_name()) > 0
      && isSequentialForPower(database.get_instance_map()[pin.get_instance_name()])) {
    return;
  }
  PowerActivity activity = getOutputActivity(pin_name, pp_model);
  if (activity.get_is_valid()) {
    setPinActivity(pin_name, activity);
  }
}

PowerActivity PowerPropagator::getOutputActivity(std::string& pin_name, PPModel& pp_model)
{
  Database& database = PWDM.getDatabase();
  if (database.get_pin_map().count(pin_name) == 0) {
    return PowerActivity();
  }
  Pin& pin = database.get_pin_map()[pin_name];
  if (database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return getFallbackInputActivity(pin_name);
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return getFallbackInputActivity(pin_name);
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  if (timing_cell.get_port_map().count(pin.get_pin_name()) == 0) {
    return getFallbackInputActivity(pin_name);
  }
  TimingCellPort& timing_cell_port = timing_cell.get_port_map()[pin.get_pin_name()];
  if (!timing_cell_port.get_function_expression().get_is_empty()) {
    std::map<std::string, PowerActivity> input_activity_map = getInputActivityMap(instance);
    PowerActivity activity = timing_cell_port.get_function_expression().evaluate_activity(input_activity_map);
    if (activity.get_is_valid()) {
      limitDataActivity(database, pin_name, activity, pp_model.get_minimum_clock_period());
      return normalizeConstantActivity(activity);
    }
  }
  if (isClockGateOutputPin(pin_name, instance)) {
    PowerActivity activity = getClockGateOutputActivity(pin_name, instance);
    if (activity.get_is_valid()) {
      return activity;
    }
  }
  PowerActivity activity = getFallbackInputActivity(pin_name);
  limitDataActivity(database, pin_name, activity, pp_model.get_minimum_clock_period());
  return activity;
}

void PowerPropagator::limitDataActivity(Database& database, std::string& pin_name, PowerActivity& activity, double minimum_clock_period)
{
  if (!shouldLimitDataActivity(database, pin_name, activity) || minimum_clock_period <= PW_ERROR) {
    return;
  }

  double default_transition_density = getDefaultTransitionDensity(minimum_clock_period);
  double probability_limited_transition_density = getProbabilityLimitedTransitionDensity(activity, minimum_clock_period);
  scaleTransitionDensity(activity, std::min(default_transition_density, probability_limited_transition_density));
}

bool PowerPropagator::shouldLimitDataActivity(Database& database, std::string& pin_name, PowerActivity& activity)
{
  if (!activity.get_is_valid() || activity.get_origin() == PowerActivityOrigin::kClock || activity.get_origin() == PowerActivityOrigin::kVcd
      || activity.get_origin() == PowerActivityOrigin::kConstant) {
    return false;
  }
  return database.get_timing_point_map().count(pin_name) == 0 || !database.get_timing_point_map()[pin_name].get_is_clock_point();
}

double PowerPropagator::getProbabilityLimitedTransitionDensity(PowerActivity& activity, double minimum_clock_period)
{
  if (minimum_clock_period <= PW_ERROR) {
    return 0.0;
  }

  double probability = activity.get_static_probability();
  return 2.0 * probability * (1.0 - probability) / minimum_clock_period;
}

void PowerPropagator::scaleTransitionDensity(PowerActivity& activity, double maximum_transition_density)
{
  double transition_density = activity.get_transition_density();
  if (maximum_transition_density <= PW_ERROR) {
    activity.set_transition_density(0.0);
    return;
  }
  if (transition_density <= maximum_transition_density) {
    return;
  }

  double density_scale = maximum_transition_density / transition_density;
  activity.set_rise_transition_density(activity.get_rise_transition_density() * density_scale);
  activity.set_fall_transition_density(activity.get_fall_transition_density() * density_scale);
}

PowerActivity PowerPropagator::getClockGateOutputActivity(std::string& pin_name, Instance& instance)
{
  Database& database = PWDM.getDatabase();
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return PowerActivity();
  }

  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  PowerActivity clock_activity;
  std::string clock_pin_name;
  for (auto& [port_name, timing_cell_port] : timing_cell.get_port_map()) {
    std::string candidate_pin_name = instance.get_instance_name() + ":" + port_name;
    if (!isClockGateClockPin(candidate_pin_name, timing_cell_port)) {
      continue;
    }
    PowerActivity candidate_activity = getPinActivity(candidate_pin_name);
    if (candidate_activity.get_is_valid()
        && (!clock_activity.get_is_valid() || candidate_activity.get_transition_density() > clock_activity.get_transition_density())) {
      clock_activity = candidate_activity;
      clock_pin_name = candidate_pin_name;
    }
  }
  if (!clock_activity.get_is_valid()) {
    return PowerActivity();
  }

  PowerActivity enable_activity = getClockGateEnableActivity(instance, clock_pin_name, pin_name);
  return getClockGateOutputActivity(clock_activity, enable_activity);
}

PowerActivity PowerPropagator::getClockGateOutputActivity(PowerActivity& clock_activity, PowerActivity& enable_activity)
{
  if (!clock_activity.get_is_valid() || !enable_activity.get_is_valid()) {
    return PowerActivity();
  }

  double clock_probability = clock_activity.get_static_probability();
  double enable_probability = enable_activity.get_static_probability();

  PowerActivity activity;
  activity.set_rise_transition_density(clock_activity.get_rise_transition_density() * enable_probability
                                       + enable_activity.get_rise_transition_density() * clock_probability);
  activity.set_fall_transition_density(clock_activity.get_fall_transition_density() * enable_probability
                                       + enable_activity.get_fall_transition_density() * clock_probability);
  activity.set_static_probability(clock_probability * enable_probability);
  activity.set_origin(PowerActivityOrigin::kPropagated);
  activity.set_is_valid(true);
  return activity;
}

PowerActivity PowerPropagator::getClockGateEnableActivity(Instance& instance, std::string& clock_pin_name,
                                                          std::string& output_pin_name)
{
  PowerActivity enable_activity;
  Database& database = PWDM.getDatabase();
  for (std::string& pin_name : instance.get_pin_name_list()) {
    if (pin_name == clock_pin_name || pin_name == output_pin_name || database.get_pin_map().count(pin_name) == 0) {
      continue;
    }
    Pin& pin = database.get_pin_map()[pin_name];
    if (pin.get_direction() != PinDirection::kInput && pin.get_direction() != PinDirection::kInout) {
      continue;
    }
    if (database.get_timing_point_map().count(pin_name) > 0 && database.get_timing_point_map()[pin_name].get_is_clock_point()) {
      continue;
    }
    PowerActivity candidate_activity = getPinActivity(pin_name);
    if (candidate_activity.get_is_valid()
        && (!enable_activity.get_is_valid() || candidate_activity.get_static_probability() > enable_activity.get_static_probability())) {
      enable_activity = candidate_activity;
    }
  }
  return enable_activity;
}

bool PowerPropagator::isClockGateOutputPin(std::string& pin_name, Instance& instance)
{
  if (!instance.get_is_clock_gating()) {
    return false;
  }
  Database& database = PWDM.getDatabase();
  if (database.get_pin_map().count(pin_name) == 0) {
    return false;
  }
  Pin& pin = database.get_pin_map()[pin_name];
  return pin.get_direction() == PinDirection::kOutput || pin.get_direction() == PinDirection::kInout;
}

bool PowerPropagator::isClockGateClockPin(std::string& pin_name, TimingCellPort& timing_cell_port)
{
  if (timing_cell_port.get_is_clock()) {
    return true;
  }
  Database& database = PWDM.getDatabase();
  return database.get_timing_point_map().count(pin_name) > 0 && database.get_timing_point_map()[pin_name].get_is_clock_point();
}

PowerActivity PowerPropagator::normalizeConstantActivity(PowerActivity activity)
{
  if (activity.get_transition_density() <= PW_ERROR
      && (activity.get_static_probability() <= PW_ERROR || activity.get_static_probability() >= 1.0 - PW_ERROR)) {
    activity.set_origin(PowerActivityOrigin::kConstant);
  }
  return activity;
}

std::map<std::string, PowerActivity> PowerPropagator::getInputActivityMap(Instance& instance)
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, PowerActivity> input_activity_map;
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return input_activity_map;
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  for (std::pair<const std::string, TimingCellPort>& port_pair : timing_cell.get_port_map()) {
    std::string pin_name = instance.get_instance_name() + ":" + port_pair.first;
    input_activity_map[port_pair.first] = getPinActivity(pin_name);
  }
  return input_activity_map;
}

PowerActivity PowerPropagator::getFallbackInputActivity(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  for (std::size_t arc_idx : database.get_incoming_arc_list_map()[pin_name]) {
    Arc& arc = database.get_arc_list()[arc_idx];
    if (arc.get_type() != ArcType::kCell || arc.get_is_disable_arc() || arc.get_is_loop_disable()) {
      continue;
    }
    PowerActivity activity = getPinActivity(arc.get_source_pin());
    if (activity.get_is_valid()) {
      return getPropagatedActivity(activity);
    }
  }
  return PowerActivity();
}

void PowerPropagator::propagateNetActivity(Arc& arc)
{
  PowerActivity activity = getPinActivity(arc.get_source_pin());
  if (!activity.get_is_valid()) {
    return;
  }
  activity = getPropagatedActivity(activity);
  setPinActivity(arc.get_sink_pin(), activity);
}

void PowerPropagator::propagateSequentialActivity(PPModel& pp_model)
{
  for (int32_t pass_idx = 0; pass_idx < pp_model.get_max_activity_pass_num(); pass_idx++) {
    bool has_activity_change = false;
    for (std::string& instance_name : pp_model.get_sequential_instance_name_list()) {
      Database& database = PWDM.getDatabase();
      Instance& instance = database.get_instance_map()[instance_name];
      if (instance.get_output_pin_name().empty()) {
        continue;
      }
      PowerActivity activity = getSequentialOutputActivity(instance);
      if (activity.get_is_valid() && setPinActivity(instance.get_output_pin_name(), activity)) {
        has_activity_change = true;
      }
    }
    if (!has_activity_change) {
      break;
    }
    propagateCombinationalActivity(pp_model);
  }
}

PowerActivity PowerPropagator::getSequentialOutputActivity(Instance& instance)
{
  PowerActivity data_activity = getPinActivity(instance.get_data_pin_name());
  if (!data_activity.get_is_valid()) {
    return PowerActivity();
  }
  PowerActivity output_activity = data_activity;
  PowerActivity clock_activity = getPinActivity(instance.get_clock_pin_name());
  limitSequentialOutputActivity(output_activity, data_activity, clock_activity);
  output_activity.set_origin(PowerActivityOrigin::kSequential);
  output_activity.set_is_valid(true);
  return normalizeConstantActivity(output_activity);
}

void PowerPropagator::limitSequentialOutputActivity(PowerActivity& output_activity, PowerActivity& data_activity,
                                                     PowerActivity& clock_activity)
{
  if (!clock_activity.get_is_valid()) {
    return;
  }

  double active_clock_transition_density = clock_activity.get_transition_density() / 2.0;
  if (data_activity.get_transition_density() <= active_clock_transition_density) {
    return;
  }

  double probability = data_activity.get_static_probability();
  output_activity.set_transition_density(2.0 * probability * (1.0 - probability) * active_clock_transition_density);
}

PowerActivity PowerPropagator::getPinActivity(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  if (database.get_power_activity_map().count(pin_name) == 0) {
    return PowerActivity();
  }
  return database.get_power_activity_map()[pin_name];
}

bool PowerPropagator::isOutputPin(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  if (database.get_pin_map().count(pin_name) == 0) {
    return false;
  }
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return false;
  }
  TimingCell& timing_cell = database.get_timing_library().get_cell_map()[instance.get_cell_name()];
  return timing_cell.get_port_map().count(pin.get_pin_name()) > 0 && timing_cell.get_port_map()[pin.get_pin_name()].get_is_output();
}

bool PowerPropagator::isClockSource(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    if (PWUTIL.exist(clock_pair.second.get_source_list(), pin_name)) {
      return true;
    }
  }
  return false;
}

namespace {
using Word = uint64_t;
constexpr Word kOnes = ~Word{0};

int simulationCount(const char* name, int fallback, int minimum, int maximum)
{
  const char* text = std::getenv(name);
  if (!text) return fallback;
  char* end = nullptr;
  long value = std::strtol(text, &end, 10);
  return end != text && *end == '\0' && value >= minimum && value <= maximum ? static_cast<int>(value) : fallback;
}

struct Instruction
{
  LogicOperationType op;
  std::size_t node = 0;
};
using Program = std::vector<Instruction>;
struct Gate
{
  std::size_t output;
  Program function;
};
struct Sequential
{
  std::size_t state;
  std::size_t inverse;
  bool latch;
  Program data, clock, clear, preset;
  Word previous_clock = 0;
};
struct Root
{
  std::size_t node;
  PowerActivity activity;
  TimingClock* clock = nullptr;
  double rise_probability = 0.0;
  double fall_probability = 0.0;
};

// Sixty-four trajectories share the same netlist operations.
// Each clock/input event settles at zero delay before transitions are counted.
// Gate-delay glitches are excluded, while distinct events retain both edges.
class Simulator
{
 public:
  Simulator(Database& database, double period) : _database(database), _period(period), _step(period / 2.0) {}
  bool run();

 private:
  std::size_t node(const std::string& name);
  bool compile(LogicExpression& expression, std::map<std::string, std::size_t>& ports, Program& program);
  Word evaluate(const Program& program) const;
  bool build();
  void orderGates();
  void settle();
  bool advance();
  Word randomWord();
  Word randomMask(double probability);
  Word randomSubset(Word available, int count);

  Database& _database;
  double _period, _step;
  int _steps_per_period = 2;
  std::size_t _implied_root_count = 0;
  std::map<std::string, std::size_t> _node_index;
  std::map<std::string, std::size_t> _pin_nodes;
  std::vector<Word> _values;
  std::vector<Gate> _gates;
  std::vector<std::size_t> _gate_order;
  std::vector<Sequential> _sequentials;
  std::vector<Word> _next_state;
  std::vector<Root> _roots;
  std::map<std::size_t, bool> _constants;
  uint64_t _random_state = 0x4d595df4d0f33173ULL;
};

std::size_t Simulator::node(const std::string& name)
{
  auto [it, inserted] = _node_index.emplace(name, _values.size());
  if (inserted) _values.push_back(0);
  return it->second;
}

bool Simulator::compile(LogicExpression& expression, std::map<std::string, std::size_t>& ports, Program& program)
{
  if (expression.get_is_empty()) return false;
  int depth = 0;
  for (auto& term : expression.get_term_list()) {
    Instruction instruction{term.get_operation_type()};
    switch (instruction.op) {
      case LogicOperationType::kPort: {
        auto port = ports.find(term.get_port_name());
        if (port == ports.end()) return false;
        instruction.node = port->second;
        ++depth;
        break;
      }
      case LogicOperationType::kOne:
      case LogicOperationType::kZero: ++depth; break;
      case LogicOperationType::kNot: if (depth < 1) return false; break;
      case LogicOperationType::kAnd:
      case LogicOperationType::kOr:
      case LogicOperationType::kXor: if (depth < 2) return false; --depth; break;
      default: return false;
    }
    if (depth >= 64) return false;
    program.push_back(instruction);
  }
  return depth == 1;
}

Word Simulator::evaluate(const Program& program) const
{
  Word stack[64];
  std::size_t count = 0;
  for (const auto& instruction : program) {
    switch (instruction.op) {
      case LogicOperationType::kPort: stack[count++] = _values[instruction.node]; break;
      case LogicOperationType::kOne: stack[count++] = kOnes; break;
      case LogicOperationType::kZero: stack[count++] = 0; break;
      case LogicOperationType::kNot: stack[count - 1] = ~stack[count - 1]; break;
      case LogicOperationType::kAnd: --count; stack[count - 1] &= stack[count]; break;
      case LogicOperationType::kOr: --count; stack[count - 1] |= stack[count]; break;
      case LogicOperationType::kXor: --count; stack[count - 1] ^= stack[count]; break;
      default: return 0;
    }
  }
  return count ? stack[0] : 0;
}

bool Simulator::build()
{
  if (!std::isfinite(_period) || _period <= 0.0 || !_database.get_vcd_activity_map().empty()) return false;
  for (auto& [name, clock] : _database.get_timing_constraint().get_clock_map()) {
    if (clock.get_period() > 0 && clock.get_period() < _period) _period = clock.get_period();
  }
  _steps_per_period = simulationCount("IPW_POWER_SIM_STEPS_PER_PERIOD", 2, 2, 64);
  _step = _period / _steps_per_period;
  // Fixed sampling must resolve every declared clock edge. Keep the existing
  // analytical flow for clock waveforms that need a finer event schedule.
  for (auto& [name, clock] : _database.get_timing_constraint().get_clock_map()) {
    for (double value : {clock.get_period(), clock.get_rise_edge(), clock.get_fall_edge()}) {
      double ticks = value / _step;
      if (!std::isfinite(ticks) || std::abs(ticks - std::round(ticks)) > 1e-9) return false;
    }
  }
  for (auto& [name, net] : _database.get_net_map()) {
    auto id = node("net:" + name);
    for (auto& pin : net.get_pin_name_list()) _pin_nodes[pin] = id;
  }
  for (auto& [name, pin] : _database.get_pin_map()) {
    if (!_pin_nodes.count(name)) _pin_nodes[name] = node("pin:" + name);
  }
  std::set<std::size_t> driven;
  for (auto& [name, instance] : _database.get_instance_map()) {
    auto cell_it = _database.get_timing_library().get_cell_map().find(instance.get_cell_name());
    if (cell_it == _database.get_timing_library().get_cell_map().end()) continue;
    auto& cell = cell_it->second;
    std::map<std::string, std::size_t> ports;
    for (auto& [port_name, port] : cell.get_port_map()) {
      std::string pin_name = name + ":" + port_name;
      auto pin = _pin_nodes.find(pin_name);
      if (pin != _pin_nodes.end()) ports[port_name] = pin->second;
    }
    for (auto& seq : cell.get_sequentials()) {
      ports[seq.get_state_port()] = node("state:" + name + ":" + seq.get_state_port());
      if (!seq.get_inverted_state_port().empty()) {
        ports[seq.get_inverted_state_port()] = node("state:" + name + ":" + seq.get_inverted_state_port());
      }
    }
    for (auto& seq : cell.get_sequentials()) {
      Sequential model;
      model.state = ports[seq.get_state_port()];
      model.inverse = seq.get_inverted_state_port().empty() ? model.state : ports[seq.get_inverted_state_port()];
      model.latch = seq.get_is_latch();
      if (!compile(seq.get_data(), ports, model.data) || !compile(seq.get_clock(), ports, model.clock)) return false;
      if (!seq.get_clear().get_is_empty() && !compile(seq.get_clear(), ports, model.clear)) return false;
      if (!seq.get_preset().get_is_empty() && !compile(seq.get_preset(), ports, model.preset)) return false;
      driven.insert(model.state);
      driven.insert(model.inverse);
      // Start vectorless simulation from a reproducible logic-zero state.
      // The warm-up phase lets reset and clock activity establish reachable
      // states; storage that is never written retains this initial assumption.
      _values[model.state] = 0;
      if (model.inverse != model.state) _values[model.inverse] = ~_values[model.state];
      _sequentials.push_back(std::move(model));
    }
    // A sequential cell without its state function cannot be simulated safely.
    if (cell.get_is_sequential() && cell.get_sequentials().empty() && !cell.get_is_clock_gating()) return false;
    for (auto& [port_name, port] : cell.get_port_map()) {
      if (!port.get_is_output() || !ports.count(port_name)) continue;
      Gate gate{ports[port_name], {}};
      if (compile(port.get_function_expression(), ports, gate.function)) {
        if (!driven.insert(gate.output).second) return false;
        _gates.push_back(std::move(gate));
      } else if (!port.get_function_expression().get_is_empty()) return false;
    }
  }
  std::map<std::size_t, PowerActivity> root_activity;
  for (auto& [name, id] : _pin_nodes) {
    if (driven.count(id)) continue;
    auto it = _database.get_power_activity_map().find(name);
    if (it != _database.get_power_activity_map().end() && it->second.get_is_valid()) {
      if (!root_activity.count(id) || it->second.get_origin() == PowerActivityOrigin::kInput
          || it->second.get_origin() == PowerActivityOrigin::kClock || it->second.get_origin() == PowerActivityOrigin::kConstant) {
        root_activity[id] = it->second;
      }
    }
  }
  for (auto& [name, id] : _node_index) {
    if (driven.count(id) || root_activity.count(id)) continue;
    PowerActivity activity;
    activity.set_is_valid(true);
    activity.set_static_probability(0.5);
    activity.set_transition_density(0.1 / _period);
    activity.set_origin(PowerActivityOrigin::kInput);
    root_activity[id] = activity;
  }
  for (auto& [name, clock] : _database.get_timing_constraint().get_clock_map()) {
    if (clock.get_period() <= 0.0) continue;
    for (auto& source : clock.get_source_list()) {
      auto pin = _pin_nodes.find(source);
      if (pin == _pin_nodes.end()) continue;
      double duty = (clock.get_fall_edge() - clock.get_rise_edge()) / clock.get_period();
      if (duty <= 0.0) duty += 1.0;
      PowerActivity activity;
      activity.set_is_valid(true);
      activity.set_static_probability(duty);
      activity.set_transition_density(2.0 / clock.get_period());
      activity.set_origin(PowerActivityOrigin::kClock);
      root_activity[pin->second] = activity;
    }
  }
  // Case analysis takes precedence even when it constrains an internal output.
  // Propagate that constant through the implied-activity network as well.
  for (auto& [name, value] : _database.get_timing_constraint().get_case_analysis_map()) {
    if (value != TimingCaseValue::kZero && value != TimingCaseValue::kOne) continue;
    auto pin = _pin_nodes.find(name);
    if (pin == _pin_nodes.end()) continue;
    const bool constant = value == TimingCaseValue::kOne;
    _constants[pin->second] = constant;
    PowerActivity activity;
    activity.set_is_valid(true);
    activity.set_static_probability(constant ? 1.0 : 0.0);
    activity.set_transition_density(0.0);
    activity.set_origin(PowerActivityOrigin::kConstant);
    root_activity[pin->second] = activity;
  }
  if (simulationCount("IPW_POWER_SIM_IMPLIED_ROOTS", 1, 0, 1)) {
    // Vectorless annotation first implies activity through buffers/inverters.
    // Non-clock implied nets become annotated simulation boundaries, each
    // retaining its exact marginal probability and density. Loads of one net
    // share a trajectory; separate annotated nets use separate random vectors.
    // Clock waveforms retain their phase relationship through the logic graph.
    orderGates();
    if (_gate_order.size() != _gates.size()) return false;
    std::set<std::size_t> implied;
    for (auto idx : _gate_order) {
      auto& gate = _gates[idx];
      auto& function = gate.function;
      bool invert = function.size() == 2 && function[1].op == LogicOperationType::kNot;
      if ((function.size() != 1 && !invert) || function[0].op != LogicOperationType::kPort) continue;
      auto input = root_activity.find(function[0].node);
      if (input == root_activity.end()) continue;
      PowerActivity activity = input->second;
      if (invert) {
        activity.set_static_probability(1.0 - activity.get_static_probability());
        double rise = activity.get_rise_transition_density();
        activity.set_rise_transition_density(activity.get_fall_transition_density());
        activity.set_fall_transition_density(rise);
      }
      auto existing = root_activity.find(gate.output);
      if (_constants.count(gate.output)
          || (existing != root_activity.end() && existing->second.get_origin() == PowerActivityOrigin::kClock)) activity = existing->second;
      root_activity[gate.output] = activity;
      if (activity.get_origin() != PowerActivityOrigin::kClock) {
        implied.insert(gate.output);
        driven.erase(gate.output);
      }
    }
    std::erase_if(_gates, [&](const Gate& gate) { return implied.count(gate.output); });
    _implied_root_count = implied.size();
    _gate_order.clear();
  }
  // Include roots with no prior annotation, using the documented input default.
  for (auto& [name, id] : _node_index) {
    if (driven.count(id)) continue;
    Root root;
    root.node = id;
    auto it = root_activity.find(id);
    if (it != root_activity.end()) root.activity = it->second;
    else {
      root.activity.set_is_valid(true);
      root.activity.set_static_probability(0.5);
      root.activity.set_transition_density(0.1 / _period);
      root.activity.set_origin(PowerActivityOrigin::kInput);
    }
    double probability = root.activity.get_static_probability();
    double transitions = root.activity.get_transition_density() * _step / 2.0;
    root.rise_probability = probability < 1.0 ? std::min(1.0, transitions / (1.0 - probability)) : 0.0;
    root.fall_probability = probability > 0.0 ? std::min(1.0, transitions / probability) : 0.0;
    for (auto& [clock_name, clock] : _database.get_timing_constraint().get_clock_map()) {
      for (auto& source : clock.get_source_list()) {
        auto pin = _pin_nodes.find(source);
        if (pin != _pin_nodes.end() && pin->second == id) root.clock = &clock;
      }
    }
    if (root.clock && root.clock->get_period() > 0.0) {
      double duty = (root.clock->get_fall_edge() - root.clock->get_rise_edge()) / root.clock->get_period();
      if (duty <= 0.0) duty += 1.0;
      root.activity.set_transition_density(2.0 / root.clock->get_period());
      root.activity.set_static_probability(duty);
      root.activity.set_origin(PowerActivityOrigin::kClock);
    }
    _values[id] = randomMask(probability);
    _roots.push_back(root);
  }
  orderGates();
  _next_state.resize(_sequentials.size());
  return _gate_order.size() == _gates.size();
}

void Simulator::orderGates()
{
  std::vector<std::vector<std::size_t>> fanout(_values.size());
  std::vector<int> indegree(_gates.size(), 0);
  std::set<std::size_t> gate_outputs;
  for (auto& gate : _gates) gate_outputs.insert(gate.output);
  for (std::size_t idx = 0; idx < _gates.size(); ++idx) {
    std::set<std::size_t> dependencies;
    for (auto& instruction : _gates[idx].function) {
      if (instruction.op == LogicOperationType::kPort && gate_outputs.count(instruction.node)) dependencies.insert(instruction.node);
    }
    indegree[idx] = dependencies.size();
    for (auto id : dependencies) fanout[id].push_back(idx);
  }
  std::queue<std::size_t> ready;
  for (std::size_t idx = 0; idx < _gates.size(); ++idx) if (!indegree[idx]) ready.push(idx);
  while (!ready.empty()) {
    auto idx = ready.front(); ready.pop();
    _gate_order.push_back(idx);
    for (auto next : fanout[_gates[idx].output]) if (--indegree[next] == 0) ready.push(next);
  }
}

void Simulator::settle()
{
  for (auto& [id, value] : _constants) _values[id] = value ? kOnes : 0;
  for (auto idx : _gate_order) {
    auto& gate = _gates[idx];
    auto constant = _constants.find(gate.output);
    _values[gate.output] = constant == _constants.end() ? evaluate(gate.function) : (constant->second ? kOnes : 0);
  }
}

bool Simulator::advance()
{
  // Additional passes handle clocks produced by sequential logic.
  for (int pass = 0; pass < 16; ++pass) {
    bool changed = false;
    for (std::size_t idx = 0; idx < _sequentials.size(); ++idx) {
      auto& seq = _sequentials[idx];
      Word clock = evaluate(seq.clock);
      Word enabled = seq.latch ? clock : (clock & ~seq.previous_clock);
      seq.previous_clock = clock;
      Word value = (_values[seq.state] & ~enabled) | (evaluate(seq.data) & enabled);
      Word clear = evaluate(seq.clear), preset = evaluate(seq.preset);
      // Conflicting async controls require Liberty clear_preset_var semantics,
      // including X states, which this two-state simulator cannot represent.
      if (clear & preset) return false;
      value = (value & ~(clear | preset)) | preset;
      _next_state[idx] = value;
      changed |= value != _values[seq.state];
    }
    if (!changed) return true;
    for (std::size_t idx = 0; idx < _sequentials.size(); ++idx) {
      auto& seq = _sequentials[idx];
      _values[seq.state] = _next_state[idx];
      if (seq.inverse != seq.state) _values[seq.inverse] = ~_next_state[idx];
    }
    settle();
  }
  // A latch loop or an unresolved generated-clock chain must not publish
  // partially settled activity. The caller can use analytical propagation.
  return false;
}

Word Simulator::randomWord()
{
  uint64_t value = (_random_state += 0x9e3779b97f4a7c15ULL);
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

Word Simulator::randomMask(double probability)
{
  if (probability <= 0.0) return 0;
  if (probability >= 1.0) return kOnes;
  if (probability == 0.5) return randomWord();
  uint32_t threshold = static_cast<uint32_t>(probability * 65536.0 + 0.5);
  if (threshold >= 65536) return kOnes;
  Word equal = kOnes, less = 0;
  for (int bit = 15; bit >= 0; --bit) {
    Word random = randomWord();
    if (threshold & (1U << bit)) { less |= equal & ~random; equal &= random; }
    else equal &= ~random;
  }
  return less;
}

Word Simulator::randomSubset(Word available, int count)
{
  int size = std::popcount(available);
  if (count <= 0) return 0;
  if (count >= size) return available;
  if (count > size / 2) return available ^ randomSubset(available, size - count);
  Word selected = 0;
  while (count) {
    Word bit = Word{1} << (randomWord() >> 58);
    if (available & bit) {
      selected |= bit;
      available ^= bit;
      --count;
    }
  }
  return selected;
}

bool Simulator::run()
{
  _random_state += simulationCount("IPW_POWER_SIM_SEED", 0, 0, 1000000);
  if (!build()) return false;
  const int warmup = simulationCount("IPW_POWER_SIM_WARMUP", 128 * _steps_per_period, 0, 1000000);
  const int samples = simulationCount("IPW_POWER_SIM_SAMPLES", 1024 * _steps_per_period, 128, 1000000);
  const bool balanced = simulationCount("IPW_POWER_SIM_BALANCED_INPUTS", 1, 0, 1);
  const int batches = simulationCount("IPW_POWER_SIM_BATCHES", 4, 1, 256);
  // Average short, independently initialized batches. Extending one stateful
  // trajectory instead would also change the simulated workload duration.
  auto initial_values = _values;
  std::vector<uint64_t> ones(_values.size()), rises(_values.size()), falls(_values.size());
  for (int batch = 0; batch < batches; ++batch) {
    _values = initial_values;
    for (auto& root : _roots) {
      if (balanced && !root.clock && root.activity.get_static_probability() == 0.5) {
        _values[root.node] = randomSubset(kOnes, 32);
      } else if (batch) _values[root.node] = randomMask(root.activity.get_static_probability());
    }
    settle();
    for (auto& seq : _sequentials) seq.previous_clock = evaluate(seq.clock);
    std::vector<Word> previous = _values;
    for (int sample = 0; sample < warmup + samples; ++sample) {
      // Clock edges are on the step boundary; random-input events occur one
      // quarter step later. Probe inside the clock interval to avoid round-off
      // ambiguity exactly on an edge. Count both settled event results, including
      // a pulse that returns to its initial value within this sampling step.
      double time = (sample + 0.25) * _step;
      auto collect = [&](uint64_t duration) {
        if (sample >= warmup) {
          for (std::size_t idx = 0; idx < _values.size(); ++idx) {
            Word value = _values[idx];
            ones[idx] += duration * std::popcount(value);
            rises[idx] += std::popcount(value & ~previous[idx]);
            falls[idx] += std::popcount(~value & previous[idx]);
          }
        }
        previous = _values;
      };
      for (auto& root : _roots) {
        if (root.clock && root.clock->get_period() > 0.0) {
          double phase = std::fmod(time - root.clock->get_rise_edge(), root.clock->get_period());
          if (phase < 0.0) phase += root.clock->get_period();
          double high_time = root.clock->get_fall_edge() - root.clock->get_rise_edge();
          if (high_time <= 0.0) high_time += root.clock->get_period();
          _values[root.node] = phase < high_time ? kOnes : 0;
        }
      }
      settle();
      if (!advance()) return false;
      collect(1);
      for (auto& root : _roots) {
        if (root.clock && root.clock->get_period() > 0.0) continue;
        Word value = _values[root.node];
        if (balanced && root.activity.get_static_probability() == 0.5) {
          // Preserve the annotated 50% probability across the population.
          // Independently round the transition count at each event so every
          // trajectory has the requested Markov transition probability. A
          // carried fractional quota would correlate successive event counts.
          double expected = 32.0 * root.rise_probability;
          int count = static_cast<int>(expected);
          double draw = static_cast<double>(randomWord() >> 11) / 9007199254740992.0;
          if (draw < expected - count) ++count;
          value ^= randomSubset(value, count) | randomSubset(~value, count);
        } else if (root.rise_probability == root.fall_probability) value ^= randomMask(root.rise_probability);
        else value = (value & ~randomMask(root.fall_probability)) | (~value & randomMask(root.rise_probability));
        _values[root.node] = value;
      }

      settle();
      if (!advance()) return false;
      collect(3);
    }
  }
  std::map<std::size_t, PowerActivity> annotated;
  for (auto& root : _roots) annotated[root.node] = root.activity;
  for (auto& [pin, id] : _pin_nodes) {
    PowerActivity activity;
    activity.set_is_valid(true);
    activity.set_static_probability(static_cast<double>(ones[id]) / (64.0 * samples * batches * 4.0));
    activity.set_rise_transition_density(static_cast<double>(rises[id]) / (64.0 * samples * batches * _step));
    activity.set_fall_transition_density(static_cast<double>(falls[id]) / (64.0 * samples * batches * _step));
    activity.set_origin(PowerActivityOrigin::kPropagated);
    if (annotated.count(id)) activity = annotated[id];
    if (_constants.count(id)) {
      activity.set_static_probability(_constants[id] ? 1.0 : 0.0);
      activity.set_transition_density(0.0);
      activity.set_origin(PowerActivityOrigin::kConstant);
    }
    _database.get_power_activity_map()[pin] = activity;
  }
  PWLOG.info(Loc::current(), "Simulated ", _gates.size(), " output functions and ", _sequentials.size(),
              " sequential states with 64 trajectories and ", samples, " samples in ", batches, " batches; ", _implied_root_count, " implied activity roots.");
  return true;
}
}  // namespace

bool PowerPropagator::simulateVectorlessActivity(double reference_period)
{
  return Simulator(PWDM.getDatabase(), reference_period).run();
}

}  // namespace ipw
