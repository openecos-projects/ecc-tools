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

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ista {

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
    STALOG.error(Loc::current(), "The instance not initialized!");
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
  STALOG.info(Loc::current(), "Starting...");

  PPModel pp_model = initPPModel();
  propagateActivity(pp_model);

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
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
  Database& database = STADM.getDatabase();
  double minimum_clock_period = 0.0;
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    double period = clock_pair.second.get_period();
    if (period <= STA_ERROR) {
      continue;
    }
    if (minimum_clock_period <= STA_ERROR || period < minimum_clock_period) {
      minimum_clock_period = period;
    }
  }
  if (minimum_clock_period <= STA_ERROR) {
    minimum_clock_period = 1.0;
  }
  pp_model.set_minimum_clock_period(minimum_clock_period);
}

void PowerPropagator::buildSeedPinList(PPModel& pp_model)
{
  Database& database = STADM.getDatabase();
  std::vector<std::string> seed_pin_list;
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    for (std::string& source_pin : clock_pair.second.get_source_list()) {
      if (database.get_pin_map().count(source_pin) > 0 && !STAUTIL.exist(seed_pin_list, source_pin)) {
        seed_pin_list.push_back(source_pin);
      }
    }
  }
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (!pin.get_is_port() || (pin.get_direction() != PinDirection::kInput && pin.get_direction() != PinDirection::kInout)) {
      continue;
    }
    if (!STAUTIL.exist(seed_pin_list, pin_pair.first)) {
      seed_pin_list.push_back(pin_pair.first);
    }
  }
  pp_model.set_seed_pin_list(seed_pin_list);
}

void PowerPropagator::buildSequentialInstanceNameList(PPModel& pp_model)
{
  Database& database = STADM.getDatabase();
  std::vector<std::string> sequential_instance_name_list;
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    if (instance_pair.second.get_is_sequential()) {
      sequential_instance_name_list.push_back(instance_pair.first);
    }
  }
  pp_model.set_sequential_instance_name_list(sequential_instance_name_list);
}

void PowerPropagator::propagateActivity(PPModel& pp_model)
{
  clearPowerActivity();
  seedVcdActivity();
  seedCaseAnalysisActivity();
  seedActivity(pp_model);
  propagateCombinationalActivity();
  propagateSequentialActivity(pp_model);
}

void PowerPropagator::clearPowerActivity()
{
  STADM.getDatabase().get_power_activity_map().clear();
}

void PowerPropagator::seedVcdActivity()
{
  Database& database = STADM.getDatabase();
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
  STALOG.info(Loc::current(), "Annotated ", annotated_pin_num, " power activities from VCD.");
}

bool PowerPropagator::setPinActivity(std::string& pin_name, PowerActivity& activity)
{
  Database& database = STADM.getDatabase();
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
  if (minimum_slew <= STA_ERROR) {
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
  Database& database = STADM.getDatabase();
  if (database.get_timing_point_map().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.get_timing_point_map()[pin_name];
  double data_minimum_slew = getMinimumSlew(timing_point.get_data_slew_map());
  double clock_minimum_slew = getMinimumSlew(timing_point.get_clock_slew_map());
  if (data_minimum_slew <= STA_ERROR) {
    return clock_minimum_slew;
  }
  if (clock_minimum_slew <= STA_ERROR) {
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
    if (average_slew > STA_ERROR) {
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
      STALOG.error(Loc::current(), "Unrecognized type!");
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
  if (std::fabs(previous_value) <= STA_ERROR) {
    return std::fabs(value) <= STA_ERROR ? 0.0 : 1.0;
  }
  return std::fabs(value - previous_value) / std::fabs(previous_value);
}

void PowerPropagator::seedCaseAnalysisActivity()
{
  Database& database = STADM.getDatabase();
  for (std::pair<const std::string, bool>& case_pair : database.get_timing_constraint().get_case_analysis_map()) {
    PowerActivity activity;
    activity.set_static_probability(case_pair.second ? 1.0 : 0.0);
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
  Database& database = STADM.getDatabase();
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    TimingClock& timing_clock = clock_pair.second;
    if (!STAUTIL.exist(timing_clock.get_source_list(), pin_name) || timing_clock.get_period() <= STA_ERROR) {
      continue;
    }
    double duty = (timing_clock.get_fall_edge() - timing_clock.get_rise_edge()) / timing_clock.get_period();
    if (duty <= STA_ERROR || duty >= 1.0 - STA_ERROR) {
      duty = 0.5;
    }
    PowerActivity activity;
    activity.set_transition_density(2.0 / timing_clock.get_period());
    activity.set_static_probability(duty);
    activity.set_origin(PowerActivityOrigin::kClock);
    activity.set_is_valid(true);
    return activity;
  }
  return PowerActivity();
}

PowerActivity PowerPropagator::getInputActivity(PPModel& pp_model)
{
  PowerActivity activity;
  activity.set_transition_density(0.1 / pp_model.get_minimum_clock_period());
  activity.set_static_probability(0.5);
  activity.set_origin(PowerActivityOrigin::kInput);
  activity.set_is_valid(true);
  return activity;
}

void PowerPropagator::propagateCombinationalActivity()
{
  Database& database = STADM.getDatabase();
  for (std::string& pin_name : database.get_timing_order_list()) {
    if (isOutputPin(pin_name)) {
      propagateOutputActivity(pin_name);
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

void PowerPropagator::propagateOutputActivity(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (database.get_instance_map().count(pin.get_instance_name()) > 0 && database.get_instance_map()[pin.get_instance_name()].get_is_sequential()) {
    return;
  }
  PowerActivity activity = getOutputActivity(pin_name);
  if (activity.get_is_valid()) {
    setPinActivity(pin_name, activity);
  }
}

PowerActivity PowerPropagator::getOutputActivity(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
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
      limitDataTransitionDensity(pin_name, activity);
      return normalizeConstantActivity(activity);
    }
  }
  PowerActivity activity = getFallbackInputActivity(pin_name);
  limitDataTransitionDensity(pin_name, activity);
  return activity;
}

void PowerPropagator::limitDataTransitionDensity(std::string& pin_name, PowerActivity& activity)
{
  if (!activity.get_is_valid() || activity.get_origin() == PowerActivityOrigin::kClock || activity.get_origin() == PowerActivityOrigin::kVcd
      || activity.get_origin() == PowerActivityOrigin::kConstant) {
    return;
  }

  Database& database = STADM.getDatabase();
  if (database.get_timing_point_map().count(pin_name) > 0 && database.get_timing_point_map()[pin_name].get_is_clock_point()) {
    return;
  }

  double minimum_clock_period = getMinimumClockPeriod();
  if (minimum_clock_period <= STA_ERROR) {
    return;
  }

  double probability = activity.get_static_probability();
  double default_transition_density = 0.1 / minimum_clock_period;
  double probability_limited_transition_density = 2.0 * probability * (1.0 - probability) / minimum_clock_period;
  double maximum_transition_density = std::min(default_transition_density, probability_limited_transition_density);
  double transition_density = activity.get_transition_density();
  if (maximum_transition_density <= STA_ERROR) {
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

double PowerPropagator::getMinimumClockPeriod()
{
  Database& database = STADM.getDatabase();
  double minimum_clock_period = 0.0;
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    double period = clock_pair.second.get_period();
    if (period <= STA_ERROR) {
      continue;
    }
    if (minimum_clock_period <= STA_ERROR || period < minimum_clock_period) {
      minimum_clock_period = period;
    }
  }
  return minimum_clock_period;
}

PowerActivity PowerPropagator::normalizeConstantActivity(PowerActivity activity)
{
  if (activity.get_transition_density() <= STA_ERROR
      && (activity.get_static_probability() <= STA_ERROR || activity.get_static_probability() >= 1.0 - STA_ERROR)) {
    activity.set_origin(PowerActivityOrigin::kConstant);
  }
  return activity;
}

std::map<std::string, PowerActivity> PowerPropagator::getInputActivityMap(Instance& instance)
{
  Database& database = STADM.getDatabase();
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
  Database& database = STADM.getDatabase();
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
      Database& database = STADM.getDatabase();
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
    propagateCombinationalActivity();
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
  double active_clock_transition_density = clock_activity.get_transition_density() / 2.0;
  if (clock_activity.get_is_valid() && data_activity.get_transition_density() > active_clock_transition_density) {
    double probability = data_activity.get_static_probability();
    output_activity.set_transition_density(2.0 * probability * (1.0 - probability) * active_clock_transition_density);
  }
  output_activity.set_origin(PowerActivityOrigin::kSequential);
  output_activity.set_is_valid(true);
  return normalizeConstantActivity(output_activity);
}

PowerActivity PowerPropagator::getPinActivity(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  if (database.get_power_activity_map().count(pin_name) == 0) {
    return PowerActivity();
  }
  return database.get_power_activity_map()[pin_name];
}

bool PowerPropagator::isOutputPin(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
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
  Database& database = STADM.getDatabase();
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    if (STAUTIL.exist(clock_pair.second.get_source_list(), pin_name)) {
      return true;
    }
  }
  return false;
}

}  // namespace ista
