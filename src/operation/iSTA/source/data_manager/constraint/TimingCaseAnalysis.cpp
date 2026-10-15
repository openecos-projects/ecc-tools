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

bool updateDriverValue(Database& database, const std::string& pin_name, TimingCaseValue value)
{
  if (database.get_pin_map().at(pin_name).get_direction() != PinDirection::kInout) {
    return updateEffectiveValue(database.get_timing_constraint(), pin_name, value);
  }
  auto& values = database.get_timing_constraint().get_driver_case_analysis_map();
  auto current = values.find(pin_name);
  if (current != values.end() && current->second == value) {
    return false;
  }
  values[pin_name] = value;
  return true;
}

std::optional<TimingCaseValue> driverValue(Database& database, Net& net, const std::string& load)
{
  auto& effective = database.get_timing_constraint().get_effective_case_analysis_map();
  std::optional<TimingCaseValue> result;
  for (const std::string& driver : net.get_driver_pin_list()) {
    Pin& pin = database.get_pin_map().at(driver);
    if (driver == load || pin.get_is_driver_disabled()) {
      continue;
    }
    // Unknown candidate drivers must not be replaced by the first constant found.
    auto& values = pin.get_direction() == PinDirection::kInout ? database.get_timing_constraint().get_driver_case_analysis_map() : effective;
    const auto value = values.find(driver);
    if (value == values.end() || (result.has_value() && *result != value->second)) {
      return std::nullopt;
    }
    result = value->second;
  }
  return result;
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
      for (const std::string& load : net.get_load_pin_list()) {
        const std::optional<TimingCaseValue> value = driverValue(database, net, load);
        if (value.has_value()) {
          changed = updateEffectiveValue(constraint, load, *value) || changed;
        }
      }
    }

    for (auto& [instance_name, instance] : database.get_instance_map()) {
      const auto cell = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
      if (cell == database.get_timing_library().get_cell_map().end()) {
        continue;
      }
      const std::map<std::string, bool> values = constantInputs(database, instance);
      for (auto& [port_name, port] : cell->second.get_port_map()) {
        if (!port.get_is_output()) {
          continue;
        }
        const std::string output_pin = instance_name + ":" + port_name;
        auto pin = database.get_pin_map().find(output_pin);
        if (pin == database.get_pin_map().end()) {
          continue;
        }
        // An inout PAD may expose an output function in Liberty while it is
        // resolved as an externally driven input in the design.
        if (pin->second.get_direction() != PinDirection::kOutput) {
          continue;
        }
        LogicExpression& three_state = port.get_three_state_expression();
        if (!three_state.get_is_empty()) {
          std::optional<bool> disabled = three_state.evaluate_constant(values);
          if (pin->second.get_is_driver_disabled() != disabled.value_or(false)) {
            pin->second.set_is_driver_disabled(disabled.value_or(false));
            changed = true;
          }
          if (!disabled.has_value() || *disabled) {
            continue;
          }
        }
        if (port.get_function_expression().get_is_empty()) {
          continue;
        }
        const std::optional<bool> value = port.get_function_expression().evaluate_constant(values);
        if (value.has_value()) {
          changed = updateDriverValue(database, output_pin, *value ? TimingCaseValue::kOne : TimingCaseValue::kZero) || changed;
        } else if (expressionInputsAreStatic(database, instance, port.get_function_expression())) {
          changed = updateDriverValue(database, output_pin, TimingCaseValue::kStatic) || changed;
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
    arc.set_is_function_disable(false);
    arc.get_disabled_timing_arcs().clear();
    Pin& source_pin = database.get_pin_map().at(arc.get_source_pin());
    Pin& sink_pin = database.get_pin_map().at(arc.get_sink_pin());
    if ((arc.get_type() == ArcType::kNet && source_pin.get_is_driver_disabled()) || (arc.get_type() == ArcType::kCell && sink_pin.get_is_driver_disabled())) {
      arc.set_is_case_analysis_disable(true);
      continue;
    }
    auto& source_values
        = arc.get_type() == ArcType::kNet && source_pin.get_direction() == PinDirection::kInout ? constraint.get_driver_case_analysis_map() : effective;
    auto& sink_values
        = arc.get_type() == ArcType::kCell && sink_pin.get_direction() == PinDirection::kInout ? constraint.get_driver_case_analysis_map() : effective;
    const auto source_value = source_values.find(arc.get_source_pin());
    const auto sink_value = sink_values.find(arc.get_sink_pin());
    if ((source_value != source_values.end() && isStatic(source_value->second)) || (sink_value != sink_values.end() && isStatic(sink_value->second))) {
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
    std::map<std::string, bool> values = constantInputs(database, instance->second);
    TimingCellArc* cell_arc = arc.get_timing_cell_arc();
    bool enabled = cell_arc == nullptr || cell_arc->get_timing_arc_list().empty();
    if (cell_arc != nullptr) {
      for (TimingArc& timing_arc : cell_arc->get_timing_arc_list()) {
        LogicExpression& condition = timing_arc.get_when_expression();
        if (condition.evaluate_constant(values) == std::optional<bool>(false)) {
          arc.get_disabled_timing_arcs().insert(timing_arc.get_arc_idx());
        }
        if (condition.get_is_empty() || condition.evaluate_constant(values) != std::optional<bool>(false)) {
          enabled = true;
        }
      }
    }
    if (!enabled) {
      arc.set_is_case_analysis_disable(true);
      continue;
    }
    // A sequential output function names its state, not the triggering clock.
    // Keep clock-to-Q arcs unless their case value or explicit condition disables them.
    if (arc.get_is_clock_arc() || arc.get_is_control_arc()) {
      continue;
    }
    const auto output = cell->second.get_port_map().find(arc.get_library_sink_port());
    if (output == cell->second.get_port_map().end() || output->second.get_function_expression().get_is_empty()) {
      continue;
    }
    if (!output->second.get_function_expression().is_sensitive_to(arc.get_library_source_port(), constantInputs(database, instance->second))) {
      // OpenSTA excludes this arc from arrival search, but still evaluates
      // its electrical slew when the source itself is not constant.
      arc.set_is_function_disable(true);
    }
  }
}

}  // namespace

void TimingCaseAnalysis::apply(Database& database)
{
  TimingConstraint& constraint = database.get_timing_constraint();
  constraint.get_effective_case_analysis_map() = constraint.get_case_analysis_map();
  constraint.get_driver_case_analysis_map().clear();
  for (auto& [pin_name, pin] : database.get_pin_map()) {
    pin.set_is_driver_disabled(false);
    if (pin.get_constant_value() != TimingCaseValue::kNone) {
      constraint.get_effective_case_analysis_map().try_emplace(pin_name, pin.get_constant_value());
      if (pin.get_direction() == PinDirection::kInout) {
        constraint.get_driver_case_analysis_map()[pin_name] = pin.get_constant_value();
      }
    }
    auto value = constraint.get_case_analysis_map().find(pin_name);
    if (pin.get_direction() == PinDirection::kInout && value != constraint.get_case_analysis_map().end()) {
      constraint.get_driver_case_analysis_map()[pin_name] = value->second;
    }
  }
  propagateCaseValues(database);
  disableInsensitiveArcs(database);
}

bool TimingCaseAnalysis::allowsTransition(const Database& database, std::string_view pin_name, TransType trans_type, bool is_driver)
{
  Database& mutable_database = const_cast<Database&>(database);
  auto& constraint = mutable_database.get_timing_constraint();
  auto pin = mutable_database.get_pin_map().find(std::string(pin_name));
  const auto& values = is_driver && pin != mutable_database.get_pin_map().end() && pin->second.get_direction() == PinDirection::kInout
                           ? constraint.get_driver_case_analysis_map()
                           : constraint.get_effective_case_analysis_map();
  const auto value = values.find(std::string(pin_name));
  if (value == values.end()) {
    return true;
  }
  switch (value->second) {
    case TimingCaseValue::kNone:
      return true;
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
