// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#include "TimingCaseAnalysis.hpp"

namespace ista {

namespace {

bool isConstant(TimingCaseValue value)
{
  return value == TimingCaseValue::kZero || value == TimingCaseValue::kOne;
}

bool isStatic(TimingCaseValue value)
{
  return isConstant(value) || value == TimingCaseValue::kStatic;
}

bool updateEffectiveValue(TimingConstraint& constraint, const std::string& pin_name, TimingCaseValue value)
{
  if (constraint.get_case_analysis_map().contains(pin_name)) {
    return false;
  }
  auto& effective = constraint.get_effective_case_analysis_map();
  const auto current = effective.find(pin_name);
  if (current != effective.end() && current->second == value) {
    return false;
  }
  if (current != effective.end() && current->second == TimingCaseValue::kZero) {
    return false;
  }
  effective[pin_name] = value;
  return true;
}

std::optional<TimingCaseValue> driverValue(TimingConstraint& constraint, Net& net)
{
  auto& effective = constraint.get_effective_case_analysis_map();
  std::vector<std::string> drivers = net.get_driver_pin_list();
  if (!net.get_driver_pin().empty()) {
    drivers.push_back(net.get_driver_pin());
  }
  for (const std::string& driver : drivers) {
    const auto value = effective.find(driver);
    if (value != effective.end()) {
      return value->second;
    }
  }
  return std::nullopt;
}

std::map<std::string, bool> constantInputs(Database& database, Instance& instance)
{
  std::map<std::string, bool> values;
  auto& effective = database.get_timing_constraint().get_effective_case_analysis_map();
  for (const std::string& pin_name : instance.get_pin_name_list()) {
    const auto pin = database.get_pin_map().find(pin_name);
    const auto value = effective.find(pin_name);
    if (pin == database.get_pin_map().end() || value == effective.end() || !isConstant(value->second)) {
      continue;
    }
    values[pin->second.get_pin_name()] = value->second == TimingCaseValue::kOne;
  }
  return values;
}

bool expressionInputsAreStatic(Database& database, Instance& instance, LogicExpression& expression)
{
  auto& effective = database.get_timing_constraint().get_effective_case_analysis_map();
  for (LogicExpressionTerm& term : expression.get_term_list()) {
    if (term.get_operation_type() != LogicOperationType::kPort) {
      continue;
    }
    const std::string pin_name = instance.get_instance_name() + ":" + term.get_port_name();
    const auto value = effective.find(pin_name);
    if (value == effective.end() || !isStatic(value->second)) {
      return false;
    }
  }
  return !expression.get_is_empty();
}

void propagateCaseValues(Database& database)
{
  TimingConstraint& constraint = database.get_timing_constraint();
  bool changed = true;
  for (std::size_t iteration = 0; changed && iteration <= database.get_pin_map().size(); ++iteration) {
    changed = false;
    for (auto& [net_name, net] : database.get_net_map()) {
      const std::optional<TimingCaseValue> value = driverValue(constraint, net);
      if (!value.has_value()) {
        continue;
      }
      for (const std::string& load : net.get_load_pin_list()) {
        changed = updateEffectiveValue(constraint, load, *value) || changed;
      }
    }

    for (auto& [instance_name, instance] : database.get_instance_map()) {
      const auto cell = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
      if (cell == database.get_timing_library().get_cell_map().end()) {
        continue;
      }
      const std::map<std::string, bool> values = constantInputs(database, instance);
      for (auto& [port_name, port] : cell->second.get_port_map()) {
        if (!port.get_is_output() || port.get_function_expression().get_is_empty()) {
          continue;
        }
        const std::string output_pin = instance_name + ":" + port_name;
        const std::optional<bool> value = port.get_function_expression().evaluate_constant(values);
        if (value.has_value()) {
          changed = updateEffectiveValue(constraint, output_pin, *value ? TimingCaseValue::kOne : TimingCaseValue::kZero) || changed;
        } else if (expressionInputsAreStatic(database, instance, port.get_function_expression())) {
          changed = updateEffectiveValue(constraint, output_pin, TimingCaseValue::kStatic) || changed;
        }
      }
    }
  }
}

void disableInsensitiveArcs(Database& database)
{
  TimingConstraint& constraint = database.get_timing_constraint();
  auto& effective = constraint.get_effective_case_analysis_map();
  for (Arc& arc : database.get_arc_list()) {
    arc.set_is_case_analysis_disable(false);
    const auto source_value = effective.find(arc.get_source_pin());
    const auto sink_value = effective.find(arc.get_sink_pin());
    if ((source_value != effective.end() && isStatic(source_value->second))
        || (sink_value != effective.end() && isStatic(sink_value->second))) {
      arc.set_is_case_analysis_disable(true);
      continue;
    }
    if (arc.get_type() != ArcType::kCell) {
      continue;
    }
    const auto instance = database.get_instance_map().find(arc.get_owner_name());
    if (instance == database.get_instance_map().end()) {
      continue;
    }
    const auto cell = database.get_timing_library().get_cell_map().find(instance->second.get_cell_name());
    if (cell == database.get_timing_library().get_cell_map().end()) {
      continue;
    }
    const auto output = cell->second.get_port_map().find(arc.get_library_sink_port());
    if (output == cell->second.get_port_map().end() || output->second.get_function_expression().get_is_empty()) {
      continue;
    }
    if (!output->second.get_function_expression().is_sensitive_to(arc.get_library_source_port(), constantInputs(database, instance->second))) {
      arc.set_is_case_analysis_disable(true);
    }
  }
}

}  // namespace

void TimingCaseAnalysis::apply(Database& database)
{
  TimingConstraint& constraint = database.get_timing_constraint();
  if (constraint.get_case_analysis_map().empty()) {
    constraint.get_effective_case_analysis_map().clear();
    for (Arc& arc : database.get_arc_list()) {
      arc.set_is_case_analysis_disable(false);
    }
    return;
  }
  constraint.get_effective_case_analysis_map() = constraint.get_case_analysis_map();
  propagateCaseValues(database);
  disableInsensitiveArcs(database);
}

bool TimingCaseAnalysis::allowsTransition(const Database& database, std::string_view pin_name, TransType trans_type)
{
  const auto& values = const_cast<Database&>(database).get_timing_constraint().get_effective_case_analysis_map();
  const auto value = values.find(std::string(pin_name));
  if (value == values.end()) {
    return true;
  }
  switch (value->second) {
    case TimingCaseValue::kRise:
      return trans_type == TransType::kRise;
    case TimingCaseValue::kFall:
      return trans_type == TransType::kFall;
    case TimingCaseValue::kZero:
    case TimingCaseValue::kOne:
    case TimingCaseValue::kStatic:
      return false;
  }
  return true;
}

}  // namespace ista
