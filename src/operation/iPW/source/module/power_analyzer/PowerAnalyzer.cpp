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

#include <limits>

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace ipw {

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
    PWLOG.error(Loc::current(), "The instance not initialized!");
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
  PWLOG.info(Loc::current(), "Starting...");

  prepareStaticContext();
  analyzePower();

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

PowerAnalyzer* PowerAnalyzer::_pa_instance = nullptr;

void PowerAnalyzer::analyzePower()
{
  Database& database = PWDM.getDatabase();
  database.beginPowerWindow();
  initPowerSummary();
  PowerSummary& power_summary = database.get_power_summary();
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    Instance& instance = instance_pair.second;
    PAInstanceModel pa_instance_model;
    InstancePower instance_power = analyzeInstancePower(instance, pa_instance_model);
    database.get_instance_power_list()[instance.get_instance_id()] = instance_power;
    power_summary.get_group_power_map()[instance_power.get_power_group_type()].add_power_value(
        instance_power.get_power_value()
    );
    power_summary.get_total_power_value().add_power_value(instance_power.get_power_value());
    if (instance_power.get_power_group_type() == PowerGroupType::kRegister) {
      // Split the report groups only; retain the whole cell's power for
      // instance exports and downstream current injection. Attribute register
      // clock-pin internal power to the clock-network report group.
      double clock_power = pa_instance_model.get_clock_pin_internal_power();
      power_summary.get_group_power_map()[PowerGroupType::kRegister].add_internal_power(-clock_power);
      power_summary.get_group_power_map()[PowerGroupType::kClockNetwork].add_internal_power(clock_power);
    }
  }
}

void PowerAnalyzer::prepareStaticContext()
{
  if (_is_static_context_ready) {
    return;
  }
  Database& database = PWDM.getDatabase();
  // Window-independent per-instance data. Both the average flow (single shot)
  // and the peak flow (once per run, before the window loop) use this.
  // pin 索引 -> net 索引（等价于旧路径的 pin_map.find(name) + get_net_name() + net_map.find(name)
  // 三步；不存在/无 net 的情况与旧实现一样落到"不记录 net 功耗"）。
  _pin_net_index_list.assign(database.get_pin_map().size(), std::numeric_limits<uint32_t>::max());
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    if (pin_pair.second.get_net_name().empty()) {
      continue;
    }
    auto net_iter = database.get_net_map().find(pin_pair.second.get_net_name());
    if (net_iter == database.get_net_map().end()) {
      continue;
    }
    _pin_net_index_list[pin_pair.second.get_index()] = static_cast<uint32_t>(net_iter->second.get_index());
  }
  _instance_static_context_list.assign(database.get_instance_map().size(), PAInstanceStaticContext());
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    Instance& instance = instance_pair.second;
    PAInstanceStaticContext& static_context = _instance_static_context_list[instance.get_instance_id()];
    static_context.group = getPowerGroupType(instance);
    static_context.voltage = getInstanceVoltage(instance);
    auto timing_cell_iter = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
    if (timing_cell_iter == database.get_timing_library().get_cell_map().end()) {
      continue;
    }
    static_context.timing_cell = &timing_cell_iter->second;
    const uint64_t instance_id = instance.get_instance_id();
    std::vector<std::vector<Database::InstancePortPinIndex>>& instance_port_pin_index_list
        = database.get_instance_port_pin_index_list();
    if (instance_id < instance_port_pin_index_list.size()) {
      static_context.port_pin_index_list = &instance_port_pin_index_list[instance_id];
    }
    for (TimingPowerArc& timing_power_arc : timing_cell_iter->second.get_power_arc_list()) {
      PAArcStaticContext arc_static_context;
      arc_static_context.energy_rise = getTimingPowerArcEnergy(instance, timing_power_arc, TransType::kRise);
      arc_static_context.energy_fall = getTimingPowerArcEnergy(instance, timing_power_arc, TransType::kFall);
      arc_static_context.is_active_clock_pin
          = !timing_power_arc.get_sink_port().empty() && isActiveClockPin(instance, timing_power_arc.get_sink_port());
      static_context.arc_context_list.push_back(arc_static_context);
    }
    for (std::pair<const std::string, TimingCellPort>& port_pair : timing_cell_iter->second.get_port_map()) {
      if (!port_pair.second.get_is_output()) {
        continue;
      }
      std::string output_pin_name = instance.get_instance_name() + ":" + port_pair.first;
      static_context.port_output_load_map.emplace(
          port_pair.first,
          std::max(
              PWDC.getPowerOutputLoad(output_pin_name, AnalysisType::kMax, TransType::kRise),
              PWDC.getPowerOutputLoad(output_pin_name, AnalysisType::kMax, TransType::kFall)
          )
      );
    }
  }
  _is_static_context_ready = true;
}

const PAInstanceStaticContext* PowerAnalyzer::getStaticContext(Instance& instance)
{
  const uint64_t instance_id = instance.get_instance_id();
  if (instance_id >= _instance_static_context_list.size()) {
    return nullptr;
  }
  return &_instance_static_context_list[instance_id];
}

void PowerAnalyzer::initPowerSummary()
{
  PowerSummary& power_summary = PWDM.getDatabase().get_power_summary();
  power_summary = PowerSummary();
  for (PowerGroupType power_group_type : GetPowerGroupTypeList()()) {
    power_summary.get_group_power_map()[power_group_type] = PowerValue();
  }
}

InstancePower PowerAnalyzer::analyzeInstancePower(Instance& instance, PAInstanceModel& pa_instance_model)
{
  const PAInstanceStaticContext* static_context = getStaticContext(instance);
  pa_instance_model.set_static_context(static_context);
  // Build the per-instance port activity cache once, so the per-arc
  // getPortActivity() calls below hit the map instead of rebuilding
  // "<instance>:<port>" names and re-walking the pin database.
  getPortActivityMap(instance, pa_instance_model);
  InstancePower instance_power;
  if (static_context != nullptr) {
    instance_power.set_power_group_type(static_context->group);
    instance_power.set_voltage(static_context->voltage);
  } else {
    instance_power.set_power_group_type(getPowerGroupType(instance));
    instance_power.set_voltage(getInstanceVoltage(instance));
  }
  instance_power.set_power_value(getInstancePowerValue(instance, pa_instance_model));
  return instance_power;
}

PowerValue PowerAnalyzer::getInstancePowerValue(Instance& instance, PAInstanceModel& pa_instance_model)
{
  PowerValue power_value;
  analyzeInternalPower(instance, power_value, pa_instance_model);
  analyzeSwitchingPower(instance, power_value, pa_instance_model);
  analyzeLeakagePower(instance, power_value, pa_instance_model);
  return power_value;
}

void PowerAnalyzer::analyzeInternalPower(
    Instance& instance,
    PowerValue& power_value,
    PAInstanceModel& pa_instance_model
)
{
  const PAInstanceStaticContext* static_context = pa_instance_model.get_static_context();
  if (static_context == nullptr || static_context->timing_cell == nullptr) {
    return;
  }
  TimingCell& timing_cell = *static_context->timing_cell;
  buildOutputTimingPowerArcWeightMap(instance, timing_cell, pa_instance_model);
  std::vector<TimingPowerArc>& power_arc_list = timing_cell.get_power_arc_list();
  for (std::size_t arc_index = 0; arc_index < power_arc_list.size(); arc_index++) {
    TimingPowerArc& timing_power_arc = power_arc_list[arc_index];
    const PAArcStaticContext& arc_static_context = static_context->arc_context_list[arc_index];
    double internal_power = getTimingPowerArcPower(instance, timing_power_arc, arc_static_context, pa_instance_model);
    power_value.add_internal_power(internal_power);
    // An output arc related to CK still belongs to the output pin. Only
    // internal energy attached to a clock input is moved to clock_network.
    if (arc_static_context.is_active_clock_pin) {
      pa_instance_model.add_clock_pin_internal_power(internal_power);
    }
  }
}

void PowerAnalyzer::buildOutputTimingPowerArcWeightMap(
    Instance& instance,
    TimingCell& timing_cell,
    PAInstanceModel& pa_instance_model
)
{
  std::map<PAInstanceModel::OutputTimingPowerArcGroup, double>& timing_power_arc_weight_sum_map
      = pa_instance_model.get_output_timing_power_arc_weight_sum_map();
  for (TimingPowerArc& timing_power_arc : timing_cell.get_power_arc_list()) {
    if (timing_power_arc.get_source_port().empty() || timing_power_arc.get_sink_port().empty()) {
      continue;
    }
    PowerActivity sink_activity = getOutputPortActivity(instance, timing_power_arc.get_sink_port(), pa_instance_model);
    if (!sink_activity.get_is_valid()) {
      continue;
    }
    double timing_power_arc_weight = getOutputTimingPowerArcWeight(instance, timing_power_arc, pa_instance_model);
    // An asynchronous clear/preset arc contributes to only one output edge.
    // Including it in the other edge's denominator would lose output energy.
    for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
      if (timing_power_arc.get_sink_transition() != TransType::kNone
          && timing_power_arc.get_sink_transition() != trans_type)
        continue;
      PAInstanceModel::OutputTimingPowerArcGroup group
          = std::make_tuple(timing_power_arc.get_sink_port(), timing_power_arc.get_related_pg_port(), trans_type);
      timing_power_arc_weight_sum_map[group] += timing_power_arc_weight;
    }
  }
}

double PowerAnalyzer::getTimingPowerArcPower(
    Instance& instance,
    TimingPowerArc& timing_power_arc,
    const PAArcStaticContext& arc_static_context,
    PAInstanceModel& pa_instance_model
)
{
  if (timing_power_arc.get_source_port().empty()) {
    return getInputTimingPowerArcPower(instance, timing_power_arc, arc_static_context, pa_instance_model);
  }
  return getOutputTimingPowerArcPower(instance, timing_power_arc, arc_static_context, pa_instance_model);
}

double PowerAnalyzer::getInputTimingPowerArcPower(
    Instance& instance,
    TimingPowerArc& timing_power_arc,
    const PAArcStaticContext& arc_static_context,
    PAInstanceModel& pa_instance_model
)
{
  if (timing_power_arc.get_sink_port().empty()) {
    return 0.0;
  }
  PowerActivity sink_activity = getPortActivity(instance, timing_power_arc.get_sink_port(), pa_instance_model);
  if (!sink_activity.get_is_valid()) {
    return 0.0;
  }
  double condition_probability
      = getInputTimingPowerArcConditionProbability(instance, timing_power_arc, pa_instance_model);
  if (condition_probability <= 0.0) {
    return 0.0;
  }
  double internal_power = 0.0;
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    double transition_density = trans_type == TransType::kRise ? sink_activity.get_rise_transition_density()
                                                               : sink_activity.get_fall_transition_density();
    double energy = trans_type == TransType::kRise ? arc_static_context.energy_rise : arc_static_context.energy_fall;
    internal_power += energy * transition_density * condition_probability * 1E-3;
  }
  return internal_power;
}

double PowerAnalyzer::getInputTimingPowerArcConditionProbability(
    Instance& instance,
    TimingPowerArc& timing_power_arc,
    PAInstanceModel& pa_instance_model
)
{
  if (timing_power_arc.get_when_expression().get_is_empty()) {
    return 1.0;
  }
  return getTimingPowerArcConditionProbability(instance, timing_power_arc, pa_instance_model);
}

double PowerAnalyzer::getOutputTimingPowerArcPower(
    Instance& instance,
    TimingPowerArc& timing_power_arc,
    const PAArcStaticContext& arc_static_context,
    PAInstanceModel& pa_instance_model
)
{
  if (timing_power_arc.get_sink_port().empty()) {
    return 0.0;
  }
  PowerActivity sink_activity = getOutputPortActivity(instance, timing_power_arc.get_sink_port(), pa_instance_model);
  if (!sink_activity.get_is_valid()) {
    return 0.0;
  }
  double timing_power_arc_weight = getOutputTimingPowerArcWeight(instance, timing_power_arc, pa_instance_model);
  if (timing_power_arc_weight <= PW_ERROR) {
    return 0.0;
  }
  double internal_power = 0.0;
  for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
    if (timing_power_arc.get_sink_transition() != TransType::kNone
        && timing_power_arc.get_sink_transition() != trans_type)
      continue;
    double timing_power_arc_weight_sum
        = getOutputTimingPowerArcWeightSum(timing_power_arc, pa_instance_model, trans_type);
    if (timing_power_arc_weight_sum <= PW_ERROR)
      continue;
    double transition_density = trans_type == TransType::kRise ? sink_activity.get_rise_transition_density()
                                                               : sink_activity.get_fall_transition_density();
    double energy = trans_type == TransType::kRise ? arc_static_context.energy_rise : arc_static_context.energy_fall;
    internal_power += energy * transition_density * timing_power_arc_weight / timing_power_arc_weight_sum * 1E-3;
  }
  return internal_power;
}

double PowerAnalyzer::getOutputTimingPowerArcWeight(
    Instance& instance,
    TimingPowerArc& timing_power_arc,
    PAInstanceModel& pa_instance_model
)
{
  std::map<TimingPowerArc*, double>& timing_power_arc_weight_map
      = pa_instance_model.get_output_timing_power_arc_weight_map();
  auto timing_power_arc_weight = timing_power_arc_weight_map.find(&timing_power_arc);
  if (timing_power_arc_weight != timing_power_arc_weight_map.end()) {
    return timing_power_arc_weight->second;
  }

  PowerActivity source_activity = getPortActivity(instance, timing_power_arc.get_source_port(), pa_instance_model);
  double weight = 0.0;
  if (source_activity.get_is_valid()) {
    double condition_probability
        = getOutputTimingPowerArcConditionProbability(instance, timing_power_arc, pa_instance_model);
    weight = source_activity.get_transition_density() * condition_probability;
  }
  timing_power_arc_weight_map[&timing_power_arc] = weight;
  return weight;
}

double PowerAnalyzer::getOutputTimingPowerArcConditionProbability(
    Instance& instance,
    TimingPowerArc& timing_power_arc,
    PAInstanceModel& pa_instance_model
)
{
  Database& database = PWDM.getDatabase();
  auto timing_cell_iter = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
  if (timing_cell_iter == database.get_timing_library().get_cell_map().end()) {
    return 0.0;
  }
  TimingCell& timing_cell = timing_cell_iter->second;
  auto output_port_iter = timing_cell.get_port_map().find(timing_power_arc.get_sink_port());
  if (output_port_iter == timing_cell.get_port_map().end()) {
    return 0.0;
  }
  TimingCellPort& output_port = output_port_iter->second;
  if (!output_port.get_function_expression().get_is_empty()
      && output_port.get_function_expression().get_has_port(timing_power_arc.get_source_port())) {
    if (!timing_power_arc.get_when_expression().get_is_empty()) {
      return output_port.get_function_expression().get_sensitivity_probability(
          timing_power_arc.get_source_port(),
          getPortActivityMap(instance, pa_instance_model),
          &timing_power_arc.get_when_expression()
      );
    }
    return getSensitivityProbability(
        output_port.get_function_expression(),
        timing_power_arc.get_source_port(),
        instance,
        pa_instance_model
    );
  }
  if (!timing_power_arc.get_when_expression().get_is_empty()) {
    return getTimingPowerArcConditionProbability(instance, timing_power_arc, pa_instance_model);
  }
  return 0.5;
}

double PowerAnalyzer::getOutputTimingPowerArcWeightSum(
    TimingPowerArc& timing_power_arc,
    PAInstanceModel& pa_instance_model,
    TransType trans_type
)
{
  PAInstanceModel::OutputTimingPowerArcGroup timing_power_arc_group
      = std::make_tuple(timing_power_arc.get_sink_port(), timing_power_arc.get_related_pg_port(), trans_type);
  std::map<PAInstanceModel::OutputTimingPowerArcGroup, double>& timing_power_arc_weight_sum_map
      = pa_instance_model.get_output_timing_power_arc_weight_sum_map();
  auto timing_power_arc_weight_sum = timing_power_arc_weight_sum_map.find(timing_power_arc_group);
  if (timing_power_arc_weight_sum == timing_power_arc_weight_sum_map.end()) {
    return 0.0;
  }
  return timing_power_arc_weight_sum->second;
}

double PowerAnalyzer::getTimingPowerArcEnergy(
    Instance& instance,
    TimingPowerArc& timing_power_arc,
    TransType trans_type
)
{
  auto energy_table_iter = timing_power_arc.get_energy_table_map().find(trans_type);
  if (energy_table_iter == timing_power_arc.get_energy_table_map().end()) {
    return 0.0;
  }
  double input_slew = getTimingPowerArcInputSlew(instance, timing_power_arc, trans_type);
  double output_load = getTimingPowerArcOutputLoad(instance, timing_power_arc, trans_type);
  TimingTable& energy_table = energy_table_iter->second;
  return energy_table.findValue(
      input_slew * timing_power_arc.get_time_unit_scale(),
      output_load * timing_power_arc.get_cap_unit_scale()
  );
}

double PowerAnalyzer::getTimingPowerArcInputSlew(
    Instance& instance,
    TimingPowerArc& timing_power_arc,
    TransType trans_type
)
{
  std::string port_name = timing_power_arc.get_source_port();
  if (port_name.empty()) {
    port_name = timing_power_arc.get_sink_port();
  }
  std::string pin_name = instance.get_instance_name() + ":" + port_name;
  TransType input_trans_type = trans_type;
  if (timing_power_arc.get_source_transition() != TransType::kNone) {
    input_trans_type = timing_power_arc.get_source_transition();
  } else if (!timing_power_arc.get_source_port().empty()) {
    TimingArcSense sense = timing_power_arc.get_source_sense();
    if (sense == TimingArcSense::kNone)
      sense = getTimingPowerArcSense(instance, timing_power_arc);
    if (sense == TimingArcSense::kNegative)
      input_trans_type = trans_type == TransType::kRise ? TransType::kFall : TransType::kRise;
  }
  return getPinSlew(pin_name, input_trans_type);
}

TimingArcSense PowerAnalyzer::getTimingPowerArcSense(Instance& instance, TimingPowerArc& timing_power_arc)
{
  Database& database = PWDM.getDatabase();
  auto timing_cell_iter = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
  if (timing_cell_iter == database.get_timing_library().get_cell_map().end()) {
    return TimingArcSense::kNone;
  }
  TimingCell& timing_cell = timing_cell_iter->second;
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

double PowerAnalyzer::getTimingPowerArcOutputLoad(Instance& instance, TimingPowerArc& timing_power_arc, TransType)
{
  if (timing_power_arc.get_source_port().empty()) {
    return 0.0;
  }
  std::string output_pin_name = instance.get_instance_name() + ":" + timing_power_arc.get_sink_port();
  // Match OpenSTA's power loadCap(..., MinMax::max()) convention: use the
  // maximum of the two total net loads. This is a tool/model convention,
  // not a general physical rule for reducing edge-dependent capacitance.
  return std::max(
      PWDC.getPowerOutputLoad(output_pin_name, AnalysisType::kMax, TransType::kRise),
      PWDC.getPowerOutputLoad(output_pin_name, AnalysisType::kMax, TransType::kFall)
  );
}

double PowerAnalyzer::getTimingPowerArcConditionProbability(
    Instance& instance,
    TimingPowerArc& timing_power_arc,
    PAInstanceModel& pa_instance_model
)
{
  if (timing_power_arc.get_when_expression().get_is_empty()) {
    return 1.0;
  }
  return getLogicExpressionStaticProbability(timing_power_arc.get_when_expression(), instance, pa_instance_model);
}

double PowerAnalyzer::getLogicExpressionStaticProbability(
    LogicExpression& logic_expression,
    Instance& instance,
    PAInstanceModel& pa_instance_model
)
{
  std::map<LogicExpression*, PowerActivity>& logic_expression_activity_map
      = pa_instance_model.get_logic_expression_activity_map();
  auto logic_expression_activity = logic_expression_activity_map.find(&logic_expression);
  if (logic_expression_activity != logic_expression_activity_map.end()) {
    PowerActivity activity = logic_expression_activity->second;
    return activity.get_is_valid() ? activity.get_static_probability() : 0.0;
  }

  std::map<std::string, PowerActivity>& port_activity_map = getPortActivityMap(instance, pa_instance_model);
  auto probability = logic_expression.evaluate_probability(port_activity_map);
  PowerActivity activity;
  activity.set_is_valid(probability.has_value());
  activity.set_static_probability(probability.value_or(0.0));
  logic_expression_activity_map[&logic_expression] = activity;
  return activity.get_is_valid() ? activity.get_static_probability() : 0.0;
}

double PowerAnalyzer::getSensitivityProbability(
    LogicExpression& logic_expression,
    const std::string& port_name,
    Instance& instance,
    PAInstanceModel& pa_instance_model
)
{
  PAInstanceModel::SensitivityProbabilityKey sensitivity_probability_key = std::make_pair(&logic_expression, port_name);
  std::map<PAInstanceModel::SensitivityProbabilityKey, double>& sensitivity_probability_map
      = pa_instance_model.get_sensitivity_probability_map();
  auto sensitivity_probability = sensitivity_probability_map.find(sensitivity_probability_key);
  if (sensitivity_probability != sensitivity_probability_map.end()) {
    return sensitivity_probability->second;
  }

  std::map<std::string, PowerActivity>& port_activity_map = getPortActivityMap(instance, pa_instance_model);
  std::string mutable_port_name = port_name;
  double probability = logic_expression.get_sensitivity_probability(mutable_port_name, port_activity_map);
  sensitivity_probability_map[sensitivity_probability_key] = probability;
  return probability;
}

void PowerAnalyzer::analyzeSwitchingPower(
    Instance& instance,
    PowerValue& power_value,
    PAInstanceModel& pa_instance_model
)
{
  const PAInstanceStaticContext* static_context = pa_instance_model.get_static_context();
  if (static_context == nullptr || static_context->timing_cell == nullptr) {
    return;
  }
  double voltage = static_context->voltage;
  const std::vector<Database::InstancePortPinIndex>* port_pin_index_list = static_context->port_pin_index_list;
  if (port_pin_index_list == nullptr) {
    return;
  }
  const std::vector<Database::PinStaticContext>& pin_static_context_list = PWDM.getDatabase().get_pin_static_context_list();
  // 与旧实现同样的顺序（cell port_map 的迭代序），只是改成按 port->pin 索引走，
  // 省掉每个输出脚拼 "instance:port" 与 pin_map 查找。
  for (const Database::InstancePortPinIndex& port_pin_index : *port_pin_index_list) {
    const uint32_t pin_index = port_pin_index.pin_index;
    if (pin_index >= pin_static_context_list.size()) {
      continue;
    }
    const Database::PinStaticContext& pin_static_context = pin_static_context_list[pin_index];
    if (pin_static_context.cell_port == nullptr || !pin_static_context.cell_port->get_is_output()) {
      continue;
    }
    const std::string& port_name = *port_pin_index.port_name;
    if (pin_static_context.pin->get_is_driver_disabled()) {
      continue;
    }
    PowerActivity activity = getOutputPortActivity(instance, port_name, pa_instance_model);
    auto output_load_iter = static_context->port_output_load_map.find(port_name);
    double output_load = 0.0;
    if (output_load_iter != static_context->port_output_load_map.end()) {
      output_load = output_load_iter->second;
    } else {
      std::string output_pin_name = instance.get_instance_name() + ":" + port_name;
      output_load = std::max(
          PWDC.getPowerOutputLoad(output_pin_name, AnalysisType::kMax, TransType::kRise),
          PWDC.getPowerOutputLoad(output_pin_name, AnalysisType::kMax, TransType::kFall)
      );
    }
    if (!activity.get_is_valid()) {
      recordNetPower(pin_index, voltage, output_load, activity, 0.0);
      continue;
    }
    double enable_probability = getDriverEnableProbability(instance, *pin_static_context.cell_port, pa_instance_model);
    double switching_power
        = 0.5 * output_load * voltage * voltage * activity.get_transition_density() * enable_probability * 1E-3;
    power_value.add_switching_power(switching_power);
    recordNetPower(pin_index, voltage, output_load, activity, switching_power);
  }
}

void PowerAnalyzer::recordNetPower(
    const uint32_t pin_index,
    const double voltage,
    const double output_load,
    PowerActivity& activity,
    const double switching_power
)
{
  Database& database = PWDM.getDatabase();
  if (pin_index >= _pin_net_index_list.size() || _pin_net_index_list[pin_index] == std::numeric_limits<uint32_t>::max()) {
    return;
  }
  const uint32_t net_index = _pin_net_index_list[pin_index];
  NetPower net_power;
  net_power.set_voltage(voltage);
  net_power.set_total_net_load(output_load);
  net_power.set_switching_power(switching_power);
  net_power.set_activity_origin(activity.get_origin());
  net_power.set_is_activity_valid(activity.get_is_valid());
  if (activity.get_is_valid()) {
    net_power.set_static_probability(activity.get_static_probability());
    net_power.set_transition_density(activity.get_transition_density());
  }
  database.get_net_power_list()[net_index] = net_power;
  database.get_net_power_written()[net_index] = 1;
}

void PowerAnalyzer::analyzeLeakagePower(Instance& instance, PowerValue& power_value, PAInstanceModel& pa_instance_model)
{
  const PAInstanceStaticContext* static_context = pa_instance_model.get_static_context();
  if (static_context == nullptr || static_context->timing_cell == nullptr) {
    return;
  }
  TimingCell& timing_cell = *static_context->timing_cell;
  if (timing_cell.get_leakage_power_list().empty()) {
    power_value.add_leakage_power(timing_cell.get_cell_leakage_power());
    return;
  }
  // Conditions on different supply rails describe the same logic states.
  // Accumulating their probabilities together counts those states twice.
  std::map<std::string, PALeakageSummary> rail_leakage;
  for (TimingLeakagePower& timing_leakage_power : timing_cell.get_leakage_power_list()) {
    PALeakageSummary& leakage_summary = rail_leakage[timing_leakage_power.get_related_pg_port()];
    if (timing_leakage_power.get_when_expression().get_is_empty()) {
      leakage_summary.add_unconditional_leakage_power(timing_leakage_power.get_leakage_power());
    } else {
      leakage_summary.add_conditional_leakage_power(
          timing_leakage_power.get_leakage_power(),
          getLeakageConditionProbability(instance, timing_leakage_power, pa_instance_model)
      );
    }
  }
  for (auto& [rail, leakage_summary] : rail_leakage) {
    // An unconditional rail value is the fallback for uncovered states on
    // that rail. In particular, ground rail tables must not inherit VDD power.
    double fallback = leakage_summary.get_has_unconditional()
                          ? leakage_summary.get_unconditional_leakage_power()
                          : (rail_leakage.size() == 1 ? timing_cell.get_cell_leakage_power() : 0.0);
    power_value.add_leakage_power(leakage_summary.get_leakage_power(fallback));
  }
}

double PowerAnalyzer::getLeakageConditionProbability(
    Instance& instance,
    TimingLeakagePower& timing_leakage_power,
    PAInstanceModel& pa_instance_model
)
{
  return getLogicExpressionStaticProbability(timing_leakage_power.get_when_expression(), instance, pa_instance_model);
}

PowerActivity PowerAnalyzer::getOutputPortActivity(
    Instance& instance,
    const std::string& port_name,
    PAInstanceModel& pa_instance_model
)
{
  Database& database = PWDM.getDatabase();
  auto pin = database.get_pin_map().find(instance.get_instance_name() + ":" + port_name);
  if (pin != database.get_pin_map().end()) {
    if (pin->second.get_is_driver_disabled()) {
      return PowerActivity();
    }
    if (pin->second.get_direction() == PinDirection::kInout) {
      auto activity = database.get_workspace().driver_activity_map.find(static_cast<uint32_t>(pin->second.get_index()));
      return activity == database.get_workspace().driver_activity_map.end() ? PowerActivity() : activity->second;
    }
  }
  return getPortActivity(instance, port_name, pa_instance_model);
}

double PowerAnalyzer::getDriverEnableProbability(
    Instance& instance,
    TimingCellPort& port,
    PAInstanceModel& pa_instance_model
)
{
  LogicExpression& three_state = port.get_three_state_expression();
  if (three_state.get_is_empty()) {
    return 1.0;
  }
  PowerActivity disabled = three_state.evaluate_activity(getPortActivityMap(instance, pa_instance_model));
  if (!disabled.get_is_valid()) {
    PWLOG.warn(
        Loc::current(),
        "Unknown output-enable activity: ",
        instance.get_instance_name(),
        "; switching power is not attributed to this driver."
    );
    return 0.0;
  }
  return 1.0 - disabled.get_static_probability();
}

PowerActivity PowerAnalyzer::getPortActivity(
    Instance& instance,
    const std::string& port_name,
    PAInstanceModel& pa_instance_model
)
{
  if (pa_instance_model.get_is_port_activity_map_built()) {
    std::map<std::string, PowerActivity>& port_activity_map = pa_instance_model.get_port_activity_map();
    auto port_activity = port_activity_map.find(port_name);
    if (port_activity != port_activity_map.end()) {
      return port_activity->second;
    }
  }
  std::string pin_name = instance.get_instance_name() + ":" + port_name;
  return getPinActivity(pin_name);
}

std::map<std::string, PowerActivity>& PowerAnalyzer::getPortActivityMap(
    Instance& instance,
    PAInstanceModel& pa_instance_model
)
{
  std::map<std::string, PowerActivity>& port_activity_map = pa_instance_model.get_port_activity_map();
  if (pa_instance_model.get_is_port_activity_map_built()) {
    return port_activity_map;
  }

  Database& database = PWDM.getDatabase();
  const uint64_t instance_id = instance.get_instance_id();
  std::vector<std::vector<Database::InstancePortPinIndex>>& instance_port_pin_index_list
      = database.get_instance_port_pin_index_list();
  if (instance_id < instance_port_pin_index_list.size()) {
    for (Database::InstancePortPinIndex& port_pin_index : instance_port_pin_index_list[instance_id]) {
      port_activity_map[*port_pin_index.port_name] = getPinActivity(port_pin_index.pin_index);
    }
  }
  pa_instance_model.set_is_port_activity_map_built(true);
  return port_activity_map;
}

PowerActivity PowerAnalyzer::getPinActivity(const std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  auto pin_iter = database.get_pin_map().find(pin_name);
  if (pin_iter == database.get_pin_map().end()) {
    return PowerActivity();
  }
  return getPinActivity(static_cast<uint32_t>(pin_iter->second.get_index()));
}

PowerActivity PowerAnalyzer::getPinActivity(const uint32_t pin_index)
{
  const PowerActivity* activity = PWDM.getDatabase().findPowerActivity(pin_index);
  return activity != nullptr ? *activity : PowerActivity();
}

double PowerAnalyzer::getPinSlew(const std::string& pin_name, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  auto timing_point = database.get_timing_point_map().find(pin_name);
  if (timing_point == database.get_timing_point_map().end()) {
    return 0.0;
  }
  TimingPoint& timing_point_value = timing_point->second;
  // At a sequential clock pin the data start-point slew represents the
  // triggering edge for clock-to-Q. It is not the physical slew of both CK
  // edges. Internal clock-pin energy must use each edge's clock slew.
  auto& primary = timing_point_value.get_is_clock_point() ? timing_point_value.get_clock_slew_map()
                                                          : timing_point_value.get_data_slew_map();
  auto& fallback = timing_point_value.get_is_clock_point() ? timing_point_value.get_data_slew_map()
                                                           : timing_point_value.get_clock_slew_map();
  for (auto* slew_map : {&primary, &fallback}) {
    auto max_slew = slew_map->find(AnalysisType::kMax);
    if (max_slew != slew_map->end()) {
      auto slew = max_slew->second.find(trans_type);
      if (slew != max_slew->second.end())
        return slew->second;
    }
  }
  return 0.0;
}

double PowerAnalyzer::getInstanceVoltage(Instance& instance)
{
  Database& database = PWDM.getDatabase();
  auto cell_iter = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
  if (cell_iter != database.get_timing_library().get_cell_map().end()) {
    double voltage = cell_iter->second.get_nom_voltage();
    if (voltage > PW_ERROR) {
      return voltage;
    }
  }
  if (database.get_timing_library().get_nom_voltage() > PW_ERROR) {
    return database.get_timing_library().get_nom_voltage();
  }
  return 1.0;
}

PowerGroupType PowerAnalyzer::getPowerGroupType(Instance& instance)
{
  Database& database = PWDM.getDatabase();
  if (instance.get_is_io_cell()) {
    return PowerGroupType::kIOPad;
  }
  auto timing_cell_iter = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
  if (timing_cell_iter == database.get_timing_library().get_cell_map().end()) {
    return PowerGroupType::kBlackBox;
  }
  TimingCell& timing_cell = timing_cell_iter->second;
  if (timing_cell.get_is_macro()) {
    return PowerGroupType::kMemory;
  }
  if (isClockNetwork(instance)) {
    return PowerGroupType::kClockNetwork;
  }
  if (timing_cell.get_is_sequential_for_power()) {
    for (auto& [port_name, port] : timing_cell.get_port_map()) {
      if (isActiveClockPin(instance, port_name))
        return PowerGroupType::kRegister;
    }
    return PowerGroupType::kSequential;
  }
  return PowerGroupType::kCombinational;
}

bool PowerAnalyzer::isActiveClockPin(Instance& instance, const std::string& port_name)
{
  auto& database = PWDM.getDatabase();
  auto cell = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
  if (cell == database.get_timing_library().get_cell_map().end())
    return false;
  auto port = cell->second.get_port_map().find(port_name);
  if (port == cell->second.get_port_map().end() || !port->second.get_is_input())
    return false;
  bool is_clock = port->second.get_is_clock();
  std::string clock_port_name = port_name;
  // Explicit state functions also identify clocks in libraries without
  // clock:true or timing checks (including latch enables).
  for (auto& sequential : cell->second.get_sequentials()) {
    is_clock = is_clock || sequential.get_clock().get_has_port(clock_port_name);
  }
  auto point = database.get_timing_point_map().find(instance.get_instance_name() + ":" + port_name);
  return is_clock && point != database.get_timing_point_map().end() && point->second.get_is_clock_point();
}

bool PowerAnalyzer::isClockNetwork(Instance& instance)
{
  Database& database = PWDM.getDatabase();
  bool has_output_pin = false;
  for (std::string& pin_name : instance.get_pin_name_list()) {
    auto pin_iter = database.get_pin_map().find(pin_name);
    if (pin_iter == database.get_pin_map().end()) {
      continue;
    }
    Pin& pin = pin_iter->second;
    if (pin.get_direction() != PinDirection::kOutput && pin.get_direction() != PinDirection::kInout) {
      continue;
    }
    has_output_pin = true;
    auto point_iter = database.get_timing_point_map().find(pin_name);
    if (point_iter == database.get_timing_point_map().end() || !point_iter->second.get_is_clock_point()) {
      return false;
    }
  }
  return has_output_pin;
}

}  // namespace ipw
