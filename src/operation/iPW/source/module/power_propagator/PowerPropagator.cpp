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
#include <cstdint>
#include <cstdlib>

#ifdef _OPENMP
#include <omp.h>
#endif

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

void PowerPropagator::prepareStaticContext()
{
  // The peak engine calls this once before its workers start; propagate() then
  // only hits the ready flag for every window (the list is read-only afterwards).
  if (_is_static_context_ready) {
    return;
  }
  Database& database = PWDM.getDatabase();
  _pin_minimum_slew_list.assign(database.get_pin_map().size(), 0.0);
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    std::string pin_name = pin_pair.first;
    _pin_minimum_slew_list[pin_pair.second.get_index()] = getMinimumSlew(pin_name);
  }
  _is_static_context_ready = true;
}

double PowerPropagator::getCachedMinimumSlew(std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  auto pin_iter = database.get_pin_map().find(pin_name);
  if (pin_iter == database.get_pin_map().end()) {
    return getMinimumSlew(pin_name);
  }
  return getCachedMinimumSlew(static_cast<uint32_t>(pin_iter->second.get_index()));
}

double PowerPropagator::getCachedMinimumSlew(const uint32_t pin_index)
{
  // The average flow keeps the original on-demand path (nothing to prepare);
  // only the peak flow, which queries this for every pin of every window,
  // fills the list up front.
  if (_pin_minimum_slew_list.empty() || pin_index >= _pin_minimum_slew_list.size()) {
    Database& database = PWDM.getDatabase();
    const std::vector<Database::PinStaticContext>& pin_static_context_list = database.get_pin_static_context_list();
    if (pin_index < pin_static_context_list.size() && pin_static_context_list[pin_index].full_name != nullptr) {
      return getMinimumSlew(*const_cast<std::string*>(pin_static_context_list[pin_index].full_name));
    }
    return 0.0;
  }
  return _pin_minimum_slew_list[pin_index];
}

void PowerPropagator::propagate()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  PPModel& pp_model = initPPModel();
  propagateActivity(pp_model);

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

PowerPropagator* PowerPropagator::_pp_instance = nullptr;

PPModel& PowerPropagator::initPPModel()
{
  // 这三个列表都只依赖设计（窗口无关），而峰值流程每个窗口都会调到这里。
  // 按 (设计名, 实例数) 缓存在 thread_local：整个 run 只建一次，调用方只读它。
  static thread_local PPModel cached_model;
  static thread_local std::string cached_design_name;
  static thread_local std::size_t cached_instance_num = 0;
  Database& database = PWDM.getDatabase();
  const std::size_t instance_num = database.get_instance_map().size();
  if (cached_instance_num != instance_num || cached_design_name != database.get_design_name()) {
    cached_model = PPModel();
    buildMinimumClockPeriod(cached_model);
    buildSeedPinList(cached_model);
    buildSequentialInstanceNameList(cached_model);
    cached_design_name = database.get_design_name();
    cached_instance_num = instance_num;
  }
  return cached_model;
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
    if (!pin.get_is_port()
        || (pin.get_direction() != PinDirection::kInput && pin.get_direction() != PinDirection::kInout)) {
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
  if (instance.get_is_sequential())
    return true;
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
  seedDriverActivity(pp_model);
  if (!PWDM.getDatabase().hasVcdActivity()) {
    if (simulateVectorlessActivity(pp_model.get_minimum_clock_period()))
      return;
    PWLOG.info(Loc::current(), "Vector simulation unavailable for this design; using analytical activity.");
  }
  seedSequentialStateActivity(pp_model);
  propagateCombinationalActivity(pp_model);
  propagateSequentialActivity(pp_model);
}

void PowerPropagator::clearPowerActivity()
{
  // 世代号 +1 即"清空"：上一窗口的条目自然失效，省掉每窗口 clear+重建的分配。
  PWDM.getDatabase().beginActivityWindow();
}

void PowerPropagator::seedVcdActivity()
{
  Database& database = PWDM.getDatabase();
  if (!database.hasVcdActivity()) {
    return;
  }
  std::size_t annotated_pin_num = 0;
  for (uint32_t pin_index : database.get_vcd_touched_pin_list()) {
    const PowerActivity* vcd_activity = database.findVcdActivity(pin_index);
    if (vcd_activity == nullptr) {
      continue;
    }
    PowerActivity activity = *vcd_activity;
    if (setPinActivity(pin_index, activity)) {
      annotated_pin_num++;
    }
  }
  PWLOG.info(Loc::current(), "Annotated ", annotated_pin_num, " power activities from VCD.");
}

std::size_t PowerPropagator::annotateVcdActivity(
    const std::map<std::string, vcd::VcdSignalActivity>& signal_activity_map
)
{
  PWDM.getDatabase().beginVcdAnnotation();
  std::size_t matched_net_num = 0;
  for (const std::pair<const std::string, vcd::VcdSignalActivity>& activity_pair : signal_activity_map) {
    PowerActivity activity;
    activity.set_transition_density(activity_pair.second.get_transition_density());
    activity.set_static_probability(activity_pair.second.get_static_probability());
    if (annotateVcdSignal(activity_pair.first, activity)) {
      matched_net_num++;
    }
  }
  return matched_net_num;
}

std::size_t PowerPropagator::annotateVcdActivity(const vcd::VcdWindowSignalActivity& window_activity)
{
  Database& database = PWDM.getDatabase();
  database.beginVcdAnnotation();
  const std::vector<std::string>& signal_name_list = window_activity.get_signal_name_list();
  const std::size_t signal_num = signal_name_list.size();
  // 波形里的信号 -> 该 net 上所有 pin 的索引列表，在 run 内是固定的：
  // 第一个窗口解析一次（net_map/pin_map 字符串查找），之后每个窗口只写活动值。
  static thread_local std::vector<std::vector<uint32_t>> signal_pin_index_list;
  static thread_local std::vector<uint8_t> signal_is_matched;
  static thread_local std::string cached_design_name;
  static thread_local std::size_t cached_signal_num = 0;
  if (cached_signal_num != signal_num || cached_design_name != database.get_design_name()) {
    signal_pin_index_list.assign(signal_num, std::vector<uint32_t>());
    signal_is_matched.assign(signal_num, 0);
    cached_signal_num = signal_num;
    cached_design_name = database.get_design_name();
    for (std::size_t signal_index = 0; signal_index < signal_num; signal_index++) {
      std::string net_name = wrapVcdPinName(signal_name_list[signal_index]);
      if (net_name.empty()) {
        continue;
      }
      auto net_iter = database.get_net_map().find(net_name);
      if (net_iter == database.get_net_map().end()) {
        continue;
      }
      signal_is_matched[signal_index] = 1;
      std::vector<uint32_t>& pin_index_list = signal_pin_index_list[signal_index];
      for (std::string& pin_name : net_iter->second.get_pin_name_list()) {
        auto pin_iter = database.get_pin_map().find(pin_name);
        if (pin_iter == database.get_pin_map().end()) {
          continue;
        }
        pin_index_list.push_back(static_cast<uint32_t>(pin_iter->second.get_index()));
      }
    }
  }
  std::size_t matched_net_num = 0;
  for (std::size_t signal_index = 0; signal_index < signal_num; signal_index++) {
    if (signal_is_matched[signal_index] == 0) {
      continue;
    }
    matched_net_num++;
    const std::vector<uint32_t>& pin_index_list = signal_pin_index_list[signal_index];
    if (pin_index_list.empty()) {
      continue;
    }
    PowerActivity activity;
    activity.set_rise_transition_density(window_activity.get_rise_transition_density(signal_index));
    activity.set_fall_transition_density(window_activity.get_fall_transition_density(signal_index));
    activity.set_static_probability(window_activity.get_static_probability(signal_index));
    activity.set_origin(PowerActivityOrigin::kVcd);
    activity.set_is_valid(true);
    for (uint32_t pin_index : pin_index_list) {
      database.setVcdActivity(pin_index, activity);
    }
  }
  return matched_net_num;
}

bool PowerPropagator::annotateVcdSignal(const std::string& vcd_signal_name, PowerActivity& activity)
{
  Database& database = PWDM.getDatabase();
  std::string net_name = wrapVcdPinName(vcd_signal_name);
  if (net_name.empty()) {
    return false;
  }
  auto net_iter = database.get_net_map().find(net_name);
  if (net_iter == database.get_net_map().end()) {
    return false;
  }
  activity.set_origin(PowerActivityOrigin::kVcd);
  activity.set_is_valid(true);
  for (std::string& pin_name : net_iter->second.get_pin_name_list()) {
    auto pin_iter = database.get_pin_map().find(pin_name);
    if (pin_iter == database.get_pin_map().end()) {
      continue;
    }
    database.setVcdActivity(static_cast<uint32_t>(pin_iter->second.get_index()), activity);
  }
  return true;
}

std::string PowerPropagator::wrapVcdPinName(const std::string& vcd_signal_name)
{
  Database& database = PWDM.getDatabase();
  // The design-scope prefix never changes during a run: build it once per thread
  // instead of concatenating it for every signal of every window.
  static thread_local std::string top_scope_path;
  if (top_scope_path.empty()) {
    top_scope_path = database.get_design_name() + "/";
  }
  std::size_t top_scope_pos = vcd_signal_name.find(top_scope_path);
  if (top_scope_pos == std::string::npos || (top_scope_pos != 0 && vcd_signal_name[top_scope_pos - 1] != '/')) {
    return "";
  }
  std::string pin_name = vcd_signal_name.substr(top_scope_pos + top_scope_path.size());
  std::replace(pin_name.begin(), pin_name.end(), '/', ':');
  return pin_name;
}

bool PowerPropagator::setPinActivity(std::string& pin_name, PowerActivity& activity)
{
  Database& database = PWDM.getDatabase();
  auto pin_iter = database.get_pin_map().find(pin_name);
  if (pin_iter == database.get_pin_map().end()) {
    return false;
  }
  return setPinActivity(static_cast<uint32_t>(pin_iter->second.get_index()), activity);
}

bool PowerPropagator::setPinActivity(const uint32_t pin_index, PowerActivity& activity)
{
  Database& database = PWDM.getDatabase();
  limitTransitionDensity(pin_index, activity);
  bool is_created = false;
  PowerActivity& current_activity = database.getOrCreatePowerActivity(pin_index, is_created);
  if (is_created) {
    current_activity = activity;
    return true;
  }
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
  Database& database = PWDM.getDatabase();
  auto pin_iter = database.get_pin_map().find(pin_name);
  if (pin_iter == database.get_pin_map().end()) {
    return;
  }
  limitTransitionDensity(static_cast<uint32_t>(pin_iter->second.get_index()), activity);
}

void PowerPropagator::limitTransitionDensity(const uint32_t pin_index, PowerActivity& activity)
{
  double minimum_slew = getCachedMinimumSlew(pin_index);
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
  auto timing_point_iter = database.get_timing_point_map().find(pin_name);
  if (timing_point_iter == database.get_timing_point_map().end()) {
    return 0.0;
  }
  TimingPoint& timing_point = timing_point_iter->second;
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
    auto analysis_iter = slew_map.find(analysis_type);
    if (analysis_iter == slew_map.end()) {
      continue;
    }
    auto rise_iter = analysis_iter->second.find(TransType::kRise);
    auto fall_iter = analysis_iter->second.find(TransType::kFall);
    if (rise_iter == analysis_iter->second.end() || fall_iter == analysis_iter->second.end()) {
      continue;
    }
    double average_slew = (rise_iter->second + fall_iter->second) / 2.0;
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
  return left_activity.get_is_valid() != right_activity.get_is_valid()
         || left_activity.get_origin() != right_activity.get_origin()
         || getRelativeChange(left_activity.get_rise_transition_density(), right_activity.get_rise_transition_density())
                > 0.01
         || getRelativeChange(left_activity.get_fall_transition_density(), right_activity.get_fall_transition_density())
                > 0.01
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
  for (auto& case_pair : database.get_timing_constraint().get_effective_case_analysis_map()) {
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

void PowerPropagator::seedDriverActivity(PPModel& pp_model)
{
  Database& database = PWDM.getDatabase();
  for (std::string& pin_name : pp_model.get_seed_pin_list()) {
    Pin& pin = database.get_pin_map().at(pin_name);
    if (!pin.get_is_port() || pin.get_direction() != PinDirection::kInout) {
      continue;
    }
    uint32_t pin_idx = static_cast<uint32_t>(pin.get_index());
    PowerActivity activity = getSeedActivity(pin_name, pp_model);
    if (const PowerActivity* vcd_activity = database.findVcdActivity(pin_idx)) {
      activity = *vcd_activity;
    }
    auto& values = database.get_timing_constraint().get_driver_case_analysis_map();
    auto value = values.find(pin_name);
    if (value != values.end() && (value->second == TimingCaseValue::kZero || value->second == TimingCaseValue::kOne)) {
      activity = PowerActivity();
      activity.set_static_probability(value->second == TimingCaseValue::kOne ? 1.0 : 0.0);
      activity.set_origin(PowerActivityOrigin::kConstant);
      activity.set_is_valid(true);
    }
    // Internal activity arriving at the load vertex must not overwrite this source.
    database.get_workspace().driver_activity_map[pin_idx] = activity;
  }
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
  for (int32_t vertex_idx : database.get_graph_order_list()) {
    GraphVertex& vertex = database.get_graph_vertex_list()[vertex_idx];
    Pin& pin = database.get_pin_map().at(vertex.get_pin_name());
    uint32_t pin_index = static_cast<uint32_t>(pin.get_index());
    if (vertex.get_is_driver() && isOutputPin(pin_index) && !pin.get_is_driver_disabled()) {
      propagateOutputActivity(pin_index, pp_model);
    }
    for (std::size_t arc_idx : vertex.get_outgoing_arc_list()) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (arc.get_is_disable_arc() || arc.get_is_loop_disable()) {
        continue;
      }
      if (arc.get_type() == ArcType::kNet) {
        propagateNetActivity(arc);
      } else if (arc.get_type() == ArcType::kCell && !isOutputPin(arc.get_sink_pin_index())) {
        PowerActivity activity = getPinActivity(arc.get_source_pin_index());
        if (activity.get_is_valid()) {
          activity = getPropagatedActivity(activity);
          setPinActivity(arc.get_sink_pin_index(), activity);
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

void PowerPropagator::propagateOutputActivity(const uint32_t pin_index, PPModel& pp_model)
{
  Database& database = PWDM.getDatabase();
  const Database::PinStaticContext& pin_static_context = database.get_pin_static_context_list()[pin_index];
  if (pin_static_context.instance != nullptr && isSequentialForPower(*pin_static_context.instance)) {
    return;
  }
  PowerActivity activity = getOutputActivity(pin_index, pp_model);
  if (activity.get_is_valid()) {
    if (pin_static_context.pin->get_direction() == PinDirection::kInout) {
      database.get_workspace().driver_activity_map[pin_index] = activity;
    } else {
      setPinActivity(pin_index, activity);
    }
  }
}

PowerActivity PowerPropagator::getOutputActivity(const uint32_t pin_index, PPModel& pp_model)
{
  Database& database = PWDM.getDatabase();
  const Database::PinStaticContext& pin_static_context = database.get_pin_static_context_list()[pin_index];
  if (pin_static_context.instance == nullptr || pin_static_context.timing_cell == nullptr
      || pin_static_context.cell_port == nullptr) {
    return getFallbackInputActivity(pin_index);
  }
  Instance& instance = *pin_static_context.instance;
  TimingCellPort& timing_cell_port = *pin_static_context.cell_port;
  if (!timing_cell_port.get_function_expression().get_is_empty()) {
    std::map<std::string, PowerActivity> input_activity_map = getInputActivityMap(instance);
    PowerActivity activity = timing_cell_port.get_function_expression().evaluate_activity(input_activity_map);
    if (activity.get_is_valid()) {
      limitDataActivity(database, pin_index, activity, pp_model.get_minimum_clock_period());
      return normalizeConstantActivity(activity);
    }
  }
  std::string& pin_name = *const_cast<std::string*>(pin_static_context.full_name);
  if (isClockGateOutputPin(pin_name, instance)) {
    PowerActivity activity = getClockGateOutputActivity(pin_name, instance);
    if (activity.get_is_valid()) {
      return activity;
    }
  }
  PowerActivity activity = getFallbackInputActivity(pin_index);
  limitDataActivity(database, pin_index, activity, pp_model.get_minimum_clock_period());
  return activity;
}

void PowerPropagator::limitDataActivity(
    Database& database,
    const uint32_t pin_index,
    PowerActivity& activity,
    double minimum_clock_period
)
{
  if (!shouldLimitDataActivity(database, pin_index, activity) || minimum_clock_period <= PW_ERROR) {
    return;
  }

  double default_transition_density = getDefaultTransitionDensity(minimum_clock_period);
  double probability_limited_transition_density
      = getProbabilityLimitedTransitionDensity(activity, minimum_clock_period);
  scaleTransitionDensity(activity, std::min(default_transition_density, probability_limited_transition_density));
}

bool PowerPropagator::shouldLimitDataActivity(Database& database, const uint32_t pin_index, PowerActivity& activity)
{
  if (!activity.get_is_valid() || activity.get_origin() == PowerActivityOrigin::kClock
      || activity.get_origin() == PowerActivityOrigin::kVcd
      || activity.get_origin() == PowerActivityOrigin::kConstant) {
    return false;
  }
  return !database.get_pin_static_context_list()[pin_index].is_clock_point;
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
        && (!clock_activity.get_is_valid()
            || candidate_activity.get_transition_density() > clock_activity.get_transition_density())) {
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
  activity.set_rise_transition_density(
      clock_activity.get_rise_transition_density() * enable_probability
      + enable_activity.get_rise_transition_density() * clock_probability
  );
  activity.set_fall_transition_density(
      clock_activity.get_fall_transition_density() * enable_probability
      + enable_activity.get_fall_transition_density() * clock_probability
  );
  activity.set_static_probability(clock_probability * enable_probability);
  activity.set_origin(PowerActivityOrigin::kPropagated);
  activity.set_is_valid(true);
  return activity;
}

PowerActivity PowerPropagator::getClockGateEnableActivity(
    Instance& instance,
    std::string& clock_pin_name,
    std::string& output_pin_name
)
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
    if (database.get_timing_point_map().count(pin_name) > 0
        && database.get_timing_point_map()[pin_name].get_is_clock_point()) {
      continue;
    }
    PowerActivity candidate_activity = getPinActivity(pin_name);
    if (candidate_activity.get_is_valid()
        && (!enable_activity.get_is_valid()
            || candidate_activity.get_static_probability() > enable_activity.get_static_probability())) {
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
  return database.get_timing_point_map().count(pin_name) > 0
         && database.get_timing_point_map()[pin_name].get_is_clock_point();
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
  const uint64_t instance_id = instance.get_instance_id();
  std::vector<std::vector<Database::InstancePortPinIndex>>& instance_port_pin_index_list
      = database.get_instance_port_pin_index_list();
  if (instance_id >= instance_port_pin_index_list.size()) {
    return input_activity_map;
  }
  for (Database::InstancePortPinIndex& port_pin_index : instance_port_pin_index_list[instance_id]) {
    input_activity_map[*port_pin_index.port_name] = getPinActivity(port_pin_index.pin_index);
  }
  return input_activity_map;
}

PowerActivity PowerPropagator::getFallbackInputActivity(const uint32_t pin_index)
{
  Database& database = PWDM.getDatabase();
  std::vector<std::vector<uint32_t>>& incoming_arc_index_list = database.get_incoming_arc_index_list();
  for (uint32_t arc_idx : incoming_arc_index_list[pin_index]) {
    Arc& arc = database.get_arc_list()[arc_idx];
    if (arc.get_type() != ArcType::kCell || arc.get_is_disable_arc() || arc.get_is_loop_disable()) {
      continue;
    }
    PowerActivity activity = getPinActivity(arc.get_source_pin_index());
    if (activity.get_is_valid()) {
      return getPropagatedActivity(activity);
    }
  }
  return PowerActivity();
}

void PowerPropagator::propagateNetActivity(Arc& arc)
{
  Database& database = PWDM.getDatabase();
  Pin& driver = database.get_pin_map().at(arc.get_source_pin());
  PowerActivity activity;
  if (driver.get_direction() == PinDirection::kInout) {
    auto iter = database.get_workspace().driver_activity_map.find(arc.get_source_pin_index());
    if (iter != database.get_workspace().driver_activity_map.end()) {
      activity = iter->second;
    }
  } else {
    activity = getPinActivity(arc.get_source_pin_index());
  }
  if (!activity.get_is_valid()) {
    return;
  }
  activity = getPropagatedActivity(activity);
  setPinActivity(arc.get_sink_pin_index(), activity);
}

void PowerPropagator::propagateSequentialActivity(PPModel& pp_model)
{
  bool valid_quit = false;
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
      valid_quit = true;
      break;
    }
    propagateCombinationalActivity(pp_model);
  }

  if (!valid_quit) {
    PWLOG.warn(
        Loc::current(),
        "Sequential activity propagation did not converge in ",
        pp_model.get_max_activity_pass_num(),
        "; result may be inaccurate"
    );
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

void PowerPropagator::limitSequentialOutputActivity(
    PowerActivity& output_activity,
    PowerActivity& data_activity,
    PowerActivity& clock_activity
)
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
  auto pin_iter = database.get_pin_map().find(pin_name);
  if (pin_iter == database.get_pin_map().end()) {
    return PowerActivity();
  }
  return getPinActivity(static_cast<uint32_t>(pin_iter->second.get_index()));
}

PowerActivity PowerPropagator::getPinActivity(const uint32_t pin_index)
{
  const PowerActivity* activity = PWDM.getDatabase().findPowerActivity(pin_index);
  return activity != nullptr ? *activity : PowerActivity();
}

bool PowerPropagator::isOutputPin(const uint32_t pin_index)
{
  const std::vector<Database::PinStaticContext>& pin_static_context_list
      = PWDM.getDatabase().get_pin_static_context_list();
  return pin_index < pin_static_context_list.size() && pin_static_context_list[pin_index].is_output_pin;
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
  if (!text)
    return fallback;
  char* end = nullptr;
  long value = std::strtol(text, &end, 10);
  return end != text && *end == '\0' && value >= minimum && value <= maximum ? static_cast<int>(value) : fallback;
}

// 向量less 仿真器的线程数：优先 IPW_POWER_SIM_THREADS，其次配置里的 thread_number；
// 按 OpenMP 可用线程数封顶，并限制到 32（该循环是内存访问密集型，再多的线程收益递减）。
int resolveSimulatorThreadNum()
{
  int thread_num = 0;
  if (const char* text = std::getenv("IPW_POWER_SIM_THREADS")) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end != text && *end == '\0' && value > 0 && value <= 1024) {
      thread_num = static_cast<int>(value);
    }
  }
  if (thread_num <= 0) {
    thread_num = PWDM.getConfig().thread_number;
  }
  if (thread_num <= 0) {
    thread_num = 1;
  }
#ifdef _OPENMP
  const int max_threads = omp_get_max_threads();
  if (thread_num > max_threads) {
    thread_num = max_threads;
  }
#endif
  if (thread_num > 32) {
    thread_num = 32;
  }
  return thread_num;
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
  // 产生这个 gate 的实例名（指向 instance_map 的 key，生命周期与 database 一致），
  // 只用于多 driver net 的诊断输出。
  const std::string* owner = nullptr;
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
  std::size_t orderGates();
  void buildGateLevels();
  static bool isSelfReferential(const Gate& gate);
  void settle();
  void fullSettle();
  void eventSettle();
  void markDirty(std::size_t node);
  void setValue(std::size_t node, Word value);
  void pushDirtyGate(std::size_t gate_idx);
  void accumulateNode(
      std::size_t idx,
      uint64_t duration,
      std::vector<uint64_t>& ones,
      std::vector<uint64_t>& rises,
      std::vector<uint64_t>& falls,
      const std::vector<Word>& previous
  );
  bool advance();
  Word randomWord();
  Word randomMask(double probability);
  Word randomSubset(Word available, int count);
  // Case-analysis constants: -1 = not constant, 0/1 = constant value. A flat
  // vector indexed by node id replaces the former std::map lookups in settle().
  void setConstant(std::size_t id, bool value);
  bool isConstant(std::size_t id) const { return id < _constants.size() && _constants[id] >= 0; }
  bool getConstantValue(std::size_t id) const { return _constants[id] != 0; }

  Database& _database;
  double _period, _step;
  int _steps_per_period = 2;
  std::size_t _implied_root_count = 0;
  // 多 driver net 合并（PTPX 的 multi driver net consolidation）的统计与样例
  std::size_t _consolidated_net_count = 0;
  std::vector<std::string> _consolidated_net_details;
  std::map<std::string, std::size_t> _node_index;
  std::map<std::string, std::size_t> _pin_nodes;
  // pin 名字 -> pin 索引：活动按 pin 写回（同一 net 上的多个 pin 共享 node，
  // 但旧行为是每个 pin 各持一份活动），因此这里按名字映射而不是按 node。
  std::map<std::string, uint32_t> _pin_name_index;
  // 事件驱动 settle 与层内并行（见 buildGateLevels()）：只在没有组合环时启用，
  // 此时 level 与 gate_order 一致，跳过"输入未变"的门与串行逐门求值逐位等价。
  bool _levels_valid = false;
  bool _full_settle_pending = true;
  bool _event_driven = true;
  int _thread_num = 1;
  std::size_t _parallel_min_gates = 4096;
  std::size_t _parallel_min_nodes = 65536;
  // fullSettle 的分层并行：组合环只会让 _gate_order 的尾部不可拓扑序化，
  // 所以前缀按层并行、尾部保持原序串行（环上求值顺序本身就是语义）。
  std::size_t _ordered_gate_num = 0;
  std::size_t _broken_gate_num = 0;
  std::vector<uint32_t> _ordered_settle_gate_list;
  std::vector<uint32_t> _ordered_level_begin_list;
  // 只有 _event_driven && _levels_valid 时才需要维护脏表（eventSettle 的唯一读者）。
  bool _track_dirty = true;
  int _advance_conflict = 0;
  std::vector<std::int32_t> _gate_level;
  std::vector<std::vector<uint32_t>> _level_gates;
  std::vector<std::int32_t> _node_driver_gate;
  std::vector<std::vector<uint32_t>> _node_fanout_gates;
  std::vector<uint8_t> _node_dirty;
  std::vector<std::size_t> _dirty_node_list;
  std::vector<std::vector<uint32_t>> _dirty_buckets;
  std::vector<uint32_t> _gate_bucket_stamp;
  uint32_t _bucket_generation = 0;
  std::vector<Word> _parallel_values;
  std::vector<Word> _values;
  std::vector<Gate> _gates;
  std::vector<std::size_t> _gate_order;
  std::vector<Sequential> _sequentials;
  std::vector<Word> _next_state;
  std::vector<Root> _roots;
  std::vector<std::int8_t> _constants;
  uint64_t _random_state = 0x4d595df4d0f33173ULL;
};

void Simulator::setConstant(std::size_t id, bool value)
{
  if (_constants.size() <= id) {
    _constants.resize(id + 1, -1);
  }
  _constants[id] = value ? 1 : 0;
}

std::size_t Simulator::node(const std::string& name)
{
  auto [it, inserted] = _node_index.emplace(name, _values.size());
  if (inserted)
    _values.push_back(0);
  return it->second;
}

bool Simulator::compile(LogicExpression& expression, std::map<std::string, std::size_t>& ports, Program& program)
{
  if (expression.get_is_empty())
    return false;
  int depth = 0;
  for (auto& term : expression.get_term_list()) {
    Instruction instruction{term.get_operation_type()};
    switch (instruction.op) {
      case LogicOperationType::kPort: {
        auto port = ports.find(term.get_port_name());
        if (port == ports.end())
          return false;
        instruction.node = port->second;
        ++depth;
        break;
      }
      case LogicOperationType::kOne:
      case LogicOperationType::kZero:
        ++depth;
        break;
      case LogicOperationType::kNot:
        if (depth < 1)
          return false;
        break;
      case LogicOperationType::kAnd:
      case LogicOperationType::kOr:
      case LogicOperationType::kXor:
        if (depth < 2)
          return false;
        --depth;
        break;
      default:
        return false;
    }
    if (depth >= 64)
      return false;
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
      case LogicOperationType::kPort:
        stack[count++] = _values[instruction.node];
        break;
      case LogicOperationType::kOne:
        stack[count++] = kOnes;
        break;
      case LogicOperationType::kZero:
        stack[count++] = 0;
        break;
      case LogicOperationType::kNot:
        stack[count - 1] = ~stack[count - 1];
        break;
      case LogicOperationType::kAnd:
        --count;
        stack[count - 1] &= stack[count];
        break;
      case LogicOperationType::kOr:
        --count;
        stack[count - 1] |= stack[count];
        break;
      case LogicOperationType::kXor:
        --count;
        stack[count - 1] ^= stack[count];
        break;
      default:
        return 0;
    }
  }
  return count ? stack[0] : 0;
}

bool Simulator::build()
{
  if (!std::isfinite(_period) || _period <= 0.0 || _database.hasVcdActivity()) {
    PWLOG.warn(
        Loc::current(),
        "Vector simulation unavailable: invalid period or VCD activity present (period=",
        _period,
        " has_vcd=",
        _database.hasVcdActivity() ? "true" : "false",
        ")"
    );
    return false;
  }
  for (auto& [name, clock] : _database.get_timing_constraint().get_clock_map()) {
    if (clock.get_period() > 0 && clock.get_period() < _period)
      _period = clock.get_period();
  }
  _steps_per_period = simulationCount("IPW_POWER_SIM_STEPS_PER_PERIOD", 2, 2, 64);
  _step = _period / _steps_per_period;
  // Fixed sampling must resolve every declared clock edge. Keep the existing
  // analytical flow for clock waveforms that need a finer event schedule.
  for (auto& [name, clock] : _database.get_timing_constraint().get_clock_map()) {
    for (double value : {clock.get_period(), clock.get_rise_edge(), clock.get_fall_edge()}) {
      double ticks = value / _step;
      if (!std::isfinite(ticks) || std::abs(ticks - std::round(ticks)) > 1e-9) {
        PWLOG.warn(
            Loc::current(),
            "Vector simulation unavailable: clock '",
            name,
            "' does not fit the simulation step (period=",
            clock.get_period(),
            " rise=",
            clock.get_rise_edge(),
            " fall=",
            clock.get_fall_edge(),
            " step=",
            _step,
            ")"
        );
        return false;
      }
    }
  }
  for (auto& [name, net] : _database.get_net_map()) {
    auto id = node("net:" + name);
    for (auto& pin : net.get_pin_name_list())
      _pin_nodes[pin] = id;
  }
  for (auto& [name, pin] : _database.get_pin_map()) {
    if (!_pin_nodes.count(name))
      _pin_nodes[name] = node("pin:" + name);
    _pin_name_index[name] = static_cast<uint32_t>(pin.get_index());
  }
  std::set<std::size_t> driven;
  for (auto& [name, instance] : _database.get_instance_map()) {
    auto cell_it = _database.get_timing_library().get_cell_map().find(instance.get_cell_name());
    if (cell_it == _database.get_timing_library().get_cell_map().end())
      continue;
    auto& cell = cell_it->second;
    std::map<std::string, std::size_t> ports;
    for (auto& [port_name, port] : cell.get_port_map()) {
      std::string pin_name = name + ":" + port_name;
      auto pin = _pin_nodes.find(pin_name);
      if (pin != _pin_nodes.end())
        ports[port_name] = pin->second;
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
      if (!compile(seq.get_data(), ports, model.data) || !compile(seq.get_clock(), ports, model.clock)) {
        PWLOG.warn(
            Loc::current(),
            "Vector simulation unavailable: sequential data/clock expression does not compile (instance=",
            name,
            " cell=",
            instance.get_cell_name(),
            " port=",
            seq.get_state_port(),
            ")"
        );
        return false;
      }
      if (!seq.get_clear().get_is_empty() && !compile(seq.get_clear(), ports, model.clear))
        return false;
      if (!seq.get_preset().get_is_empty() && !compile(seq.get_preset(), ports, model.preset))
        return false;
      driven.insert(model.state);
      driven.insert(model.inverse);
      // Start vectorless simulation from a reproducible logic-zero state.
      // The warm-up phase lets reset and clock activity establish reachable
      // states; storage that is never written retains this initial assumption.
      _values[model.state] = 0;
      if (model.inverse != model.state)
        _values[model.inverse] = ~_values[model.state];
      _sequentials.push_back(std::move(model));
    }
    // A sequential cell without its state function cannot be simulated safely.
    if (cell.get_is_sequential() && cell.get_sequentials().empty() && !cell.get_is_clock_gating()) {
      PWLOG.warn(
          Loc::current(),
          "Vector simulation unavailable: sequential cell has no state function (instance=",
          name,
          " cell=",
          instance.get_cell_name(),
          ")"
      );
      return false;
    }
    for (auto& [port_name, port] : cell.get_port_map()) {
      if (!port.get_is_output() || !ports.count(port_name))
        continue;
      Gate gate{ports[port_name], {}, &name};
      if (compile(port.get_function_expression(), ports, gate.function)) {
        if (driven.count(gate.output)) {
          // 一条 net 上有多个 driver（例如修复 buffer 的 A/Y 被短接在同一条 net 上）：
          // PTPX 的语义是 multi driver net consolidation —— 合并该 net、只保留一个
          // driver 的活动，并断掉环上的时序弧（PWR-303 / PTE-003），而不是放弃整个
          // 设计的向量less 仿真。这里按同样的规则挑一个 driver 保留，其余从仿真图里摘掉；
          // 被摘掉的实例其输出 pin 仍会写上"合并后 net 的活动"（活动按 pin 写回）。
          std::size_t previous = _gates.size();
          for (std::size_t idx = 0; idx < _gates.size(); ++idx) {
            if (_gates[idx].output == gate.output) {
              previous = idx;
              break;
            }
          }
          const bool new_self_loop = isSelfReferential(gate);
          const bool old_self_loop = previous != _gates.size() && isSelfReferential(_gates[previous]);
          // 非自环者优先（自环那条正对应 PTPX 断掉的弧）；都是或都不是自环时保留先入者，
          // instance_map 是 std::map（实例名字典序），所以结果始终是确定的。
          const bool keep_new = previous != _gates.size() && new_self_loop != old_self_loop && !new_self_loop;
          ++_consolidated_net_count;
          if (_consolidated_net_details.size() < 8) {
            std::string net_name;
            auto pin_iter = _database.get_pin_map().find(name + ":" + port_name);
            if (pin_iter != _database.get_pin_map().end())
              net_name = pin_iter->second.get_net_name();
            const std::string previous_owner = (previous != _gates.size() && _gates[previous].owner != nullptr)
                                                   ? *_gates[previous].owner
                                                   : std::string("?");
            _consolidated_net_details.push_back(
                "net=" + net_name + " keep=" + (keep_new ? name : previous_owner) + " drop=" + (keep_new ? previous_owner : name)
                + (new_self_loop || old_self_loop ? " (self-loop driver on the net)" : "")
            );
          }
          if (keep_new) {
            _gates.erase(_gates.begin() + static_cast<std::ptrdiff_t>(previous));
            _gates.push_back(std::move(gate));
          }
          continue;
        }
        driven.insert(gate.output);
        _gates.push_back(std::move(gate));
      } else if (!port.get_function_expression().get_is_empty()) {
        PWLOG.warn(
            Loc::current(),
            "Vector simulation unavailable: output function does not compile (instance=",
            name,
            " cell=",
            instance.get_cell_name(),
            " port=",
            port_name,
            ")"
        );
        return false;
      }
    }
  }
  std::map<std::size_t, PowerActivity> root_activity;
  for (auto& [name, id] : _pin_nodes) {
    auto pin_index_iter = _pin_name_index.find(name);
    if (driven.count(id) || pin_index_iter == _pin_name_index.end()) {
      continue;
    }
    const PowerActivity* stored_activity = _database.findPowerActivity(pin_index_iter->second);
    if (stored_activity != nullptr && stored_activity->get_is_valid()) {
      if (!root_activity.count(id) || stored_activity->get_origin() == PowerActivityOrigin::kInput
          || stored_activity->get_origin() == PowerActivityOrigin::kClock
          || stored_activity->get_origin() == PowerActivityOrigin::kConstant) {
        root_activity[id] = *stored_activity;
      }
    }
  }
  for (auto& [name, id] : _node_index) {
    if (driven.count(id) || root_activity.count(id))
      continue;
    PowerActivity activity;
    activity.set_is_valid(true);
    activity.set_static_probability(0.5);
    activity.set_transition_density(0.1 / _period);
    activity.set_origin(PowerActivityOrigin::kInput);
    root_activity[id] = activity;
  }
  for (auto& [name, clock] : _database.get_timing_constraint().get_clock_map()) {
    if (clock.get_period() <= 0.0)
      continue;
    for (auto& source : clock.get_source_list()) {
      auto pin = _pin_nodes.find(source);
      if (pin == _pin_nodes.end())
        continue;
      double duty = (clock.get_fall_edge() - clock.get_rise_edge()) / clock.get_period();
      if (duty <= 0.0)
        duty += 1.0;
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
    if (value != TimingCaseValue::kZero && value != TimingCaseValue::kOne)
      continue;
    auto pin = _pin_nodes.find(name);
    if (pin == _pin_nodes.end())
      continue;
    const bool constant = value == TimingCaseValue::kOne;
    setConstant(pin->second, constant);
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
    const std::size_t broken_cycles = orderGates();
    if (broken_cycles > 0) {
      PWLOG.warn(
          Loc::current(),
          "Broke ",
          broken_cycles,
          " gate cycle(s) in the vectorless simulator (PTPX semantics: timing arcs on a loop are disabled)"
      );
    }
    std::set<std::size_t> implied;
    for (auto idx : _gate_order) {
      auto& gate = _gates[idx];
      auto& function = gate.function;
      bool invert = function.size() == 2 && function[1].op == LogicOperationType::kNot;
      if ((function.size() != 1 && !invert) || function[0].op != LogicOperationType::kPort)
        continue;
      auto input = root_activity.find(function[0].node);
      if (input == root_activity.end())
        continue;
      PowerActivity activity = input->second;
      if (invert) {
        activity.set_static_probability(1.0 - activity.get_static_probability());
        double rise = activity.get_rise_transition_density();
        activity.set_rise_transition_density(activity.get_fall_transition_density());
        activity.set_fall_transition_density(rise);
      }
      auto existing = root_activity.find(gate.output);
      if (isConstant(gate.output)
          || (existing != root_activity.end() && existing->second.get_origin() == PowerActivityOrigin::kClock))
        activity = existing->second;
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
    if (driven.count(id))
      continue;
    Root root;
    root.node = id;
    auto it = root_activity.find(id);
    if (it != root_activity.end())
      root.activity = it->second;
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
        if (pin != _pin_nodes.end() && pin->second == id)
          root.clock = &clock;
      }
    }
    if (root.clock && root.clock->get_period() > 0.0) {
      double duty = (root.clock->get_fall_edge() - root.clock->get_rise_edge()) / root.clock->get_period();
      if (duty <= 0.0)
        duty += 1.0;
      root.activity.set_transition_density(2.0 / root.clock->get_period());
      root.activity.set_static_probability(duty);
      root.activity.set_origin(PowerActivityOrigin::kClock);
    }
    _values[id] = randomMask(probability);
    _roots.push_back(root);
  }
  const std::size_t broken_cycles = orderGates();
  if (broken_cycles > 0) {
    PWLOG.warn(
        Loc::current(),
        "Broke ",
        broken_cycles,
        " gate cycle(s) in the vectorless simulator (PTPX semantics: timing arcs on a loop are disabled)"
    );
  }
  if (_consolidated_net_count > 0) {
    PWLOG.warn(
        Loc::current(),
        "Consolidated ",
        _consolidated_net_count,
        " multi-driver net(s) in the vectorless simulator (keep one driver, drop the rest; same semantics as PTPX)"
    );
    for (auto& detail : _consolidated_net_details)
      PWLOG.warn(Loc::current(), "  ", detail);
  }
  _thread_num = resolveSimulatorThreadNum();
  _event_driven = simulationCount("IPW_POWER_SIM_EVENT_DRIVEN", 1, 0, 1) != 0;
  _parallel_min_gates
      = static_cast<std::size_t>(simulationCount("IPW_POWER_SIM_PARALLEL_MIN_GATES", 4096, 0, 1000000000));
  _parallel_min_nodes
      = static_cast<std::size_t>(simulationCount("IPW_POWER_SIM_PARALLEL_MIN_NODES", 65536, 0, 1000000000));
  PWLOG.info(
      Loc::current(),
      "Vectorless simulation: ",
      _gates.size(),
      " gates, ",
      _sequentials.size(),
      " sequential states, ",
      _level_gates.size(),
      " levels, ",
      _thread_num,
      " thread(s), event-driven settle ",
      (_event_driven && _levels_valid) ? "enabled" : "disabled"
  );
  _next_state.resize(_sequentials.size());
  return _gate_order.size() == _gates.size();
}

bool Simulator::isSelfReferential(const Gate& gate)
{
  for (auto& instruction : gate.function) {
    if (instruction.op == LogicOperationType::kPort && instruction.node == gate.output)
      return true;
  }
  return false;
}

// 返回为打破组合环而"断掉"的 gate 条数。没被拓扑排序覆盖的 gate 按构建顺序
// （= 实例名字典序）追加到末尾：settle() 对它们相当于用上一步的值求值，等价于
// PTPX 把环上的时序弧 disable 掉，而不是放弃整个设计的仿真。
std::size_t Simulator::orderGates()
{
  _gate_order.clear();
  std::vector<std::vector<std::size_t>> fanout(_values.size());
  std::vector<int> indegree(_gates.size(), 0);
  std::set<std::size_t> gate_outputs;
  for (auto& gate : _gates)
    gate_outputs.insert(gate.output);
  for (std::size_t idx = 0; idx < _gates.size(); ++idx) {
    std::set<std::size_t> dependencies;
    for (auto& instruction : _gates[idx].function) {
      if (instruction.op == LogicOperationType::kPort && gate_outputs.count(instruction.node))
        dependencies.insert(instruction.node);
    }
    indegree[idx] = dependencies.size();
    for (auto id : dependencies)
      fanout[id].push_back(idx);
  }
  std::queue<std::size_t> ready;
  for (std::size_t idx = 0; idx < _gates.size(); ++idx)
    if (!indegree[idx])
      ready.push(idx);
  while (!ready.empty()) {
    auto idx = ready.front();
    ready.pop();
    _gate_order.push_back(idx);
    for (auto next : fanout[_gates[idx].output])
      if (--indegree[next] == 0)
        ready.push(next);
  }
  std::size_t broken = 0;
  if (_gate_order.size() != _gates.size()) {
    std::vector<char> ordered(_gates.size(), 0);
    for (auto idx : _gate_order)
      ordered[idx] = 1;
    for (std::size_t idx = 0; idx < _gates.size(); ++idx) {
      if (!ordered[idx]) {
        _gate_order.push_back(idx);
        ++broken;
      }
    }
  }
  _broken_gate_num = broken;
  _ordered_gate_num = _gate_order.size() - broken;
  buildGateLevels();
  _levels_valid = broken == 0;
  return broken;
}

// 为每个门算 level（1 + 驱动其输入的门的最大 level；根/常量输入算 0），
// 并建"节点 -> 读它的门"的反向索引。只在无组合环时启用事件驱动。
void Simulator::buildGateLevels()
{
  _node_driver_gate.assign(_values.size(), -1);
  for (std::size_t idx = 0; idx < _gates.size(); ++idx) {
    _node_driver_gate[_gates[idx].output] = static_cast<std::int32_t>(idx);
  }
  _node_fanout_gates.assign(_values.size(), std::vector<uint32_t>());
  _gate_level.assign(_gates.size(), 0);
  // 按拓扑序（_gate_order）计算，保证输入门的 level 已经定值
  for (std::size_t order_pos = 0; order_pos < _gate_order.size(); ++order_pos) {
    const std::size_t idx = _gate_order[order_pos];
    std::int32_t level = 0;
    for (const Instruction& instruction : _gates[idx].function) {
      if (instruction.op != LogicOperationType::kPort) {
        continue;
      }
      _node_fanout_gates[instruction.node].push_back(static_cast<uint32_t>(idx));
      const std::int32_t driver = _node_driver_gate[instruction.node];
      if (driver < 0) {
        level = std::max(level, 1);
      } else {
        level = std::max(level, _gate_level[static_cast<std::size_t>(driver)] + 1);
      }
    }
    _gate_level[idx] = level;
  }
  std::int32_t max_level = 0;
  for (std::int32_t level : _gate_level) {
    max_level = std::max(max_level, level);
  }
  _level_gates.assign(static_cast<std::size_t>(max_level) + 1, std::vector<uint32_t>());
  for (std::size_t idx = 0; idx < _gates.size(); ++idx) {
    _level_gates[static_cast<std::size_t>(_gate_level[idx])].push_back(static_cast<uint32_t>(idx));
  }
  _dirty_buckets.assign(_level_gates.size(), std::vector<uint32_t>());
  _gate_bucket_stamp.assign(_gates.size(), 0);
  _node_dirty.assign(_values.size(), 0);
  _dirty_node_list.clear();

  // 有序前缀按层分组。注意：不能假设 Kahn 产出对层单调（实测大设计不成立），
  // 所以直接按层号做一次计数排序来保证"同层连续"。
  // 层内顺序无关结果：DAG 前缀里同层门互不依赖，输入都在严格更小的层。
  _ordered_settle_gate_list.assign(_ordered_gate_num, 0);
  std::int32_t prefix_max_level = 0;
  for (std::size_t pos = 0; pos < _ordered_gate_num; pos++) {
    prefix_max_level = std::max(prefix_max_level, _gate_level[_gate_order[pos]]);
  }
  _ordered_level_begin_list.assign(static_cast<std::size_t>(prefix_max_level) + 2, 0);
  for (std::size_t pos = 0; pos < _ordered_gate_num; pos++) {
    ++_ordered_level_begin_list[static_cast<std::size_t>(_gate_level[_gate_order[pos]]) + 1];
  }
  for (std::size_t level = 1; level < _ordered_level_begin_list.size(); level++) {
    _ordered_level_begin_list[level] += _ordered_level_begin_list[level - 1];
  }
  std::vector<uint32_t> level_cursor_list = _ordered_level_begin_list;
  for (std::size_t pos = 0; pos < _ordered_gate_num; pos++) {
    const uint32_t gate_idx = static_cast<uint32_t>(_gate_order[pos]);
    _ordered_settle_gate_list[level_cursor_list[static_cast<std::size_t>(_gate_level[gate_idx])]++]
        = gate_idx;
  }
  _track_dirty = _event_driven && _levels_valid;

  // 观测：层规模分布（决定层内并行阈值与负载均衡）
  std::size_t min_level_gate_num = std::numeric_limits<std::size_t>::max();
  std::size_t max_level_gate_num = 0;
  std::size_t small_level_num = 0;
  for (std::size_t bucket = 0; bucket + 1 < _ordered_level_begin_list.size(); bucket++) {
    const std::size_t count = _ordered_level_begin_list[bucket + 1] - _ordered_level_begin_list[bucket];
    min_level_gate_num = std::min(min_level_gate_num, count);
    max_level_gate_num = std::max(max_level_gate_num, count);
    if (count < _parallel_min_gates) {
      ++small_level_num;
    }
  }
  if (min_level_gate_num == std::numeric_limits<std::size_t>::max()) {
    min_level_gate_num = 0;
  }
  PWLOG.info(
      Loc::current(),
      "Simulator settle plan: ordered gates ",
      _ordered_gate_num,
      ", cyclic gates ",
      _broken_gate_num,
      ", level buckets ",
      _ordered_level_begin_list.size() - 1,
      ", gates per level ",
      min_level_gate_num,
      "-",
      max_level_gate_num,
      ", levels below ",
      _parallel_min_gates,
      " gates: ",
      small_level_num
  );
}

void Simulator::markDirty(std::size_t node)
{
  if (!_track_dirty) {
    return;
  }
  if (_node_dirty[node] == 0) {
    _node_dirty[node] = 1;
    _dirty_node_list.push_back(node);
  }
}

// 所有对 _values 的写入都走这里：值确实变化才标记"下游需要重算"。
void Simulator::setValue(std::size_t node, Word value)
{
  if (_values[node] == value) {
    return;
  }
  _values[node] = value;
  markDirty(node);
}

void Simulator::pushDirtyGate(std::size_t gate_idx)
{
  if (_gate_bucket_stamp[gate_idx] == _bucket_generation) {
    return;
  }
  _gate_bucket_stamp[gate_idx] = _bucket_generation;
  _dirty_buckets[static_cast<std::size_t>(_gate_level[gate_idx])].push_back(static_cast<uint32_t>(gate_idx));
}

// 全扫：与改造前逐门求值完全一致（批次初始化、首次 settle、有组合环时使用）。
void Simulator::fullSettle()
{
  // 前缀（可拓扑序化部分）按层并行：层的输入严格在更小的层，所以"层内并行 + 层间
  // barrier"与原来的逐门前向扫描逐位相同。尾部（环上门）保持 _gate_order 原序串行。
  if (_thread_num > 1 && _ordered_level_begin_list.size() >= 2) {
#pragma omp parallel num_threads(_thread_num)
    {
      for (std::size_t bucket = 0; bucket + 1 < _ordered_level_begin_list.size(); bucket++) {
        const std::size_t begin = _ordered_level_begin_list[bucket];
        const std::size_t end = _ordered_level_begin_list[bucket + 1];
        const std::int64_t count = static_cast<std::int64_t>(end) - static_cast<std::int64_t>(begin);
        if (count <= 0) {
          continue;
        }
#pragma omp for schedule(dynamic, 64) nowait
        for (std::int64_t i = 0; i < count; i++) {
          Gate& gate = _gates[_ordered_settle_gate_list[begin + static_cast<std::size_t>(i)]];
          _values[gate.output]
              = isConstant(gate.output) ? (getConstantValue(gate.output) ? kOnes : 0) : evaluate(gate.function);
        }
#pragma omp barrier
      }
#pragma omp master
      {
        for (std::size_t pos = _ordered_gate_num; pos < _gate_order.size(); pos++) {
          Gate& gate = _gates[_gate_order[pos]];
          _values[gate.output]
              = isConstant(gate.output) ? (getConstantValue(gate.output) ? kOnes : 0) : evaluate(gate.function);
        }
      }
    }
  } else {
    for (std::size_t pos = 0; pos < _ordered_gate_num; pos++) {
      Gate& gate = _gates[_gate_order[pos]];
      _values[gate.output]
          = isConstant(gate.output) ? (getConstantValue(gate.output) ? kOnes : 0) : evaluate(gate.function);
    }
    for (std::size_t pos = _ordered_gate_num; pos < _gate_order.size(); pos++) {
      Gate& gate = _gates[_gate_order[pos]];
      _values[gate.output]
          = isConstant(gate.output) ? (getConstantValue(gate.output) ? kOnes : 0) : evaluate(gate.function);
    }
  }
  for (std::size_t node : _dirty_node_list) {
    _node_dirty[node] = 0;
  }
  _dirty_node_list.clear();
  ++_bucket_generation;
}

// 事件驱动：只重算"输入确实变化"的门所在的下游锥。DAG 下与全扫逐位等价：
// 每个门在它的 level 上被求值至多一次，且此时所有前驱都已是最终值。
void Simulator::eventSettle()
{
  if (_dirty_node_list.empty()) {
    return;
  }
  ++_bucket_generation;
  for (std::size_t node : _dirty_node_list) {
    for (uint32_t gate_idx : _node_fanout_gates[node]) {
      pushDirtyGate(gate_idx);
    }
  }
  for (std::size_t node : _dirty_node_list) {
    _node_dirty[node] = 0;
  }
  _dirty_node_list.clear();

  for (std::size_t level = 0; level < _dirty_buckets.size(); ++level) {
    std::vector<uint32_t>& bucket = _dirty_buckets[level];
    if (bucket.empty()) {
      continue;
    }
    const std::size_t count = bucket.size();
    if (_thread_num > 1 && count >= _parallel_min_gates) {
      // 同一 level 的门写集不相交（每个门只写自己的输出），读的是更早 level 的值，
      // 因此并行求值无竞争；apply 阶段串行且顺序固定，结果与串行逐位一致。
      _parallel_values.resize(count);
      const Gate* gate_list = _gates.data();
      const std::int64_t parallel_count = static_cast<std::int64_t>(count);
#pragma omp parallel for schedule(dynamic, 64) num_threads(_thread_num)
      for (std::int64_t i = 0; i < parallel_count; ++i) {
        const Gate& gate = gate_list[bucket[static_cast<std::size_t>(i)]];
        _parallel_values[static_cast<std::size_t>(i)]
            = isConstant(gate.output) ? (getConstantValue(gate.output) ? kOnes : 0) : evaluate(gate.function);
      }
      for (std::size_t i = 0; i < count; ++i) {
        const std::size_t output = gate_list[bucket[i]].output;
        const Word value = _parallel_values[i];
        if (value == _values[output]) {
          continue;
        }
        _values[output] = value;
        for (uint32_t next : _node_fanout_gates[output]) {
          pushDirtyGate(next);
        }
      }
    } else {
      for (std::size_t i = 0; i < count; ++i) {
        const Gate& gate = _gates[bucket[i]];
        const Word value
            = isConstant(gate.output) ? (getConstantValue(gate.output) ? kOnes : 0) : evaluate(gate.function);
        if (value == _values[gate.output]) {
          continue;
        }
        _values[gate.output] = value;
        for (uint32_t next : _node_fanout_gates[gate.output]) {
          pushDirtyGate(next);
        }
      }
    }
    bucket.clear();
  }
}

void Simulator::settle()
{
  for (std::size_t id = 0; id < _constants.size() && id < _values.size(); id++) {
    if (_constants[id] >= 0) {
      setValue(id, _constants[id] ? kOnes : 0);
    }
  }
  if (!_event_driven || !_levels_valid || _full_settle_pending) {
    fullSettle();
    _full_settle_pending = false;
    return;
  }
  eventSettle();
}

bool Simulator::advance()
{
  // Additional passes handle clocks produced by sequential logic.
  for (int pass = 0; pass < 16; ++pass) {
    bool changed = false;
    _advance_conflict = 0;
    // 每个 idx 只写自己的 _next_state[idx] / 自己的 seq 成员，所以可以并行；
    // 冲突(clear&preset)用标志汇总，循环结束后统一 return false —— 与原"立即返回"
    // 等价，因为返回 false 后整场仿真结果会被丢弃。
#pragma omp parallel for schedule(static) num_threads(_thread_num) reduction(| : changed)
    for (std::int64_t i = 0; i < static_cast<std::int64_t>(_sequentials.size()); i++) {
      const std::size_t idx = static_cast<std::size_t>(i);
      auto& seq = _sequentials[idx];
      Word clock = evaluate(seq.clock);
      Word enabled = seq.latch ? clock : (clock & ~seq.previous_clock);
      seq.previous_clock = clock;
      Word value = (_values[seq.state] & ~enabled) | (evaluate(seq.data) & enabled);
      Word clear = evaluate(seq.clear), preset = evaluate(seq.preset);
      // Conflicting async controls require Liberty clear_preset_var semantics,
      // including X states, which this two-state simulator cannot represent.
      if (clear & preset) {
#pragma omp atomic write
        _advance_conflict = 1;
        continue;
      }
      value = (value & ~(clear | preset)) | preset;
      _next_state[idx] = value;
      changed |= value != _values[seq.state];
    }
    if (_advance_conflict != 0) {
      return false;
    }
    if (!changed)
      return true;
    // 应用段：_track_dirty 关闭时（脏表无人读）setValue 不碰共享结构，可以并行；
    // 否则 setValue -> markDirty 会并发写共享的 _dirty_node_list，必须串行。
    if (_track_dirty) {
      for (std::size_t idx = 0; idx < _sequentials.size(); ++idx) {
        auto& seq = _sequentials[idx];
        setValue(seq.state, _next_state[idx]);
        if (seq.inverse != seq.state)
          setValue(seq.inverse, ~_next_state[idx]);
      }
    } else {
#pragma omp parallel for schedule(static) num_threads(_thread_num)
      for (std::int64_t i = 0; i < static_cast<std::int64_t>(_sequentials.size()); i++) {
        const std::size_t idx = static_cast<std::size_t>(i);
        auto& seq = _sequentials[idx];
        setValue(seq.state, _next_state[idx]);
        if (seq.inverse != seq.state)
          setValue(seq.inverse, ~_next_state[idx]);
      }
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
  if (probability <= 0.0)
    return 0;
  if (probability >= 1.0)
    return kOnes;
  if (probability == 0.5)
    return randomWord();
  uint32_t threshold = static_cast<uint32_t>(probability * 65536.0 + 0.5);
  if (threshold >= 65536)
    return kOnes;
  Word equal = kOnes, less = 0;
  for (int bit = 15; bit >= 0; --bit) {
    Word random = randomWord();
    if (threshold & (1U << bit)) {
      less |= equal & ~random;
      equal &= random;
    } else
      equal &= ~random;
  }
  return less;
}

Word Simulator::randomSubset(Word available, int count)
{
  int size = std::popcount(available);
  if (count <= 0)
    return 0;
  if (count >= size)
    return available;
  if (count > size / 2)
    return available ^ randomSubset(available, size - count);
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

void Simulator::accumulateNode(
    std::size_t idx,
    uint64_t duration,
    std::vector<uint64_t>& ones,
    std::vector<uint64_t>& rises,
    std::vector<uint64_t>& falls,
    const std::vector<Word>& previous
)
{
  const Word value = _values[idx];
  ones[idx] += duration * std::popcount(value);
  rises[idx] += std::popcount(value & ~previous[idx]);
  falls[idx] += std::popcount(~value & previous[idx]);
}

bool Simulator::run()
{
  _random_state += simulationCount("IPW_POWER_SIM_SEED", 0, 0, 1000000);
  if (!build())
    return false;
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
    // 整个数组被改写：本轮必须全扫，之后才能走事件驱动
    _full_settle_pending = true;
    for (auto& root : _roots) {
      if (balanced && !root.clock && root.activity.get_static_probability() == 0.5) {
        setValue(root.node, randomSubset(kOnes, 32));
      } else if (batch)
        setValue(root.node, randomMask(root.activity.get_static_probability()));
    }
    settle();
    for (auto& seq : _sequentials)
      seq.previous_clock = evaluate(seq.clock);
    std::vector<Word> previous = _values;
    for (int sample = 0; sample < warmup + samples; ++sample) {
      // Clock edges are on the step boundary; random-input events occur one
      // quarter step later. Probe inside the clock interval to avoid round-off
      // ambiguity exactly on an edge. Count both settled event results, including
      // a pulse that returns to its initial value within this sampling step.
      double time = (sample + 0.25) * _step;
      auto collect = [&](uint64_t duration) {
        if (sample >= warmup) {
          const std::int64_t node_count = static_cast<std::int64_t>(_values.size());
          if (_thread_num > 1 && _values.size() >= _parallel_min_nodes) {
            // 每个 idx 只被一个线程访问（按 static 分块），所以无需原子；
            // 整数计数的最终值与串行逐位相同。
#pragma omp parallel for schedule(static) num_threads(_thread_num)
            for (std::int64_t i = 0; i < node_count; ++i) {
              accumulateNode(static_cast<std::size_t>(i), duration, ones, rises, falls, previous);
            }
          } else {
            for (std::int64_t i = 0; i < node_count; ++i) {
              accumulateNode(static_cast<std::size_t>(i), duration, ones, rises, falls, previous);
            }
          }
        }
        previous = _values;
      };
      for (auto& root : _roots) {
        if (root.clock && root.clock->get_period() > 0.0) {
          double phase = std::fmod(time - root.clock->get_rise_edge(), root.clock->get_period());
          if (phase < 0.0)
            phase += root.clock->get_period();
          double high_time = root.clock->get_fall_edge() - root.clock->get_rise_edge();
          if (high_time <= 0.0)
            high_time += root.clock->get_period();
          setValue(root.node, phase < high_time ? kOnes : 0);
        }
      }
      settle();
      if (!advance())
        return false;
      collect(1);
      for (auto& root : _roots) {
        if (root.clock && root.clock->get_period() > 0.0)
          continue;
        Word value = _values[root.node];
        if (balanced && root.activity.get_static_probability() == 0.5) {
          // Preserve the annotated 50% probability across the population.
          // Independently round the transition count at each event so every
          // trajectory has the requested Markov transition probability. A
          // carried fractional quota would correlate successive event counts.
          double expected = 32.0 * root.rise_probability;
          int count = static_cast<int>(expected);
          double draw = static_cast<double>(randomWord() >> 11) / 9007199254740992.0;
          if (draw < expected - count)
            ++count;
          value ^= randomSubset(value, count) | randomSubset(~value, count);
        } else if (root.rise_probability == root.fall_probability)
          value ^= randomMask(root.rise_probability);
        else
          value = (value & ~randomMask(root.fall_probability)) | (~value & randomMask(root.rise_probability));
        setValue(root.node, value);
      }

      settle();
      if (!advance())
        return false;
      collect(3);
    }
  }
  std::map<std::size_t, PowerActivity> annotated;
  for (auto& root : _roots)
    annotated[root.node] = root.activity;
  for (auto& [pin, id] : _pin_nodes) {
    PowerActivity activity;
    activity.set_is_valid(true);
    activity.set_static_probability(static_cast<double>(ones[id]) / (64.0 * samples * batches * 4.0));
    activity.set_rise_transition_density(static_cast<double>(rises[id]) / (64.0 * samples * batches * _step));
    activity.set_fall_transition_density(static_cast<double>(falls[id]) / (64.0 * samples * batches * _step));
    activity.set_origin(PowerActivityOrigin::kPropagated);
    if (annotated.count(id))
      activity = annotated[id];
    if (isConstant(id)) {
      activity.set_static_probability(getConstantValue(id) ? 1.0 : 0.0);
      activity.set_transition_density(0.0);
      activity.set_origin(PowerActivityOrigin::kConstant);
    }
    auto pin_index_iter = _pin_name_index.find(pin);
    if (pin_index_iter != _pin_name_index.end()) {
      _database.getOrCreatePowerActivity(pin_index_iter->second) = activity;
    }
  }
  PWLOG.info(
      Loc::current(),
      "Simulated ",
      _gates.size(),
      " output functions and ",
      _sequentials.size(),
      " sequential states with 64 trajectories and ",
      samples,
      " samples in ",
      batches,
      " batches; ",
      _implied_root_count,
      " implied activity roots."
  );
  return true;
}
}  // namespace

bool PowerPropagator::simulateVectorlessActivity(double reference_period)
{
  // The Boolean simulator aliases an entire net to one variable. It cannot
  // represent independent receive/drive roles; use graph activity propagation.
  if (!PWDM.getDatabase().get_driver_timing_point_map().empty()) {
    return false;
  }
  return Simulator(PWDM.getDatabase(), reference_period).run();
}

}  // namespace ipw
