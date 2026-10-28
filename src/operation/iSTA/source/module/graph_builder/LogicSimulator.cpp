// SPDX-License-Identifier: MulanPSL-2.0
#include "LogicSimulator.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>

#include "graph/Graph.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "network/Network.hpp"
#include "sdc/Sdc.hpp"

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
}  // namespace

LogicSimulator::LogicSimulator(const Network& network, const LibertyLibrary& liberty, const Sdc& sdc, Graph& graph)
    : _network(network), _liberty(liberty), _sdc(sdc), _graph(graph), _values(graph.simValues())
{
}

bool LogicSimulator::updateValue(const std::string& pin, TimingCaseValue value)
{
  if (_sdc.get_case_analysis_map().contains(pin))
    return false;
  const auto current = _values.value(pin);
  if (current == value || current == TimingCaseValue::kZero)
    return false;
  _values.setValue(pin, value);
  return true;
}

bool LogicSimulator::updateDriverValue(const std::string& pin, TimingCaseValue value)
{
  if (_network.pins().at(pin).get_direction() != PinDirection::kInout)
    return updateValue(pin, value);
  if (_values.value(pin, true) == value)
    return false;
  _values.setValue(pin, value, true);
  return true;
}

std::optional<TimingCaseValue> LogicSimulator::driverValue(const Net& net, const std::string& load) const
{
  std::optional<TimingCaseValue> result;
  for (const std::string& driver : net.get_driver_pin_list()) {
    const Pin& pin = _network.pins().at(driver);
    if (driver == load || _values.isDriverDisabled(driver))
      continue;
    // Unknown candidate drivers must not be replaced by the first constant found.
    const auto value = _values.value(driver, pin.get_direction() == PinDirection::kInout);
    if (!value || (result && *result != *value))
      return std::nullopt;
    result = value;
  }
  return result;
}

std::map<std::string, bool> LogicSimulator::constantInputs(const Instance& instance) const
{
  std::map<std::string, bool> values;
  for (const std::string& pin_name : instance.get_pin_name_list()) {
    const auto pin = _network.pins().find(pin_name);
    const auto value = _values.value(pin_name);
    if (pin == _network.pins().end() || !value || !isConstant(*value))
      continue;
    values[pin->second.get_pin_name()] = *value == TimingCaseValue::kOne;
  }
  return values;
}

bool LogicSimulator::expressionInputsAreStatic(const Instance& instance, const LogicExpression& expression) const
{
  for (const LogicExpressionTerm& term : expression.get_term_list()) {
    if (term.get_operation_type() != LogicOperationType::kPort)
      continue;
    const std::string pin = instance.get_instance_name() + ":" + term.get_port_name();
    const auto value = _values.value(pin);
    if (!value || !isStatic(*value))
      return false;
  }
  return !expression.get_is_empty();
}

void LogicSimulator::propagateCaseValues()
{
  bool changed = true;
  for (std::size_t iteration = 0; changed && iteration <= _network.pins().size(); ++iteration) {
    changed = false;
    for (const auto& [net_name, net] : _network.nets()) {
      for (const std::string& load : net.get_load_pin_list()) {
        const auto value = driverValue(net, load);
        if (value)
          changed = updateValue(load, *value) || changed;
      }
    }

    for (const auto& [instance_name, instance] : _network.instances()) {
      const auto cell = _liberty.get_cell_map().find(instance.get_cell_name());
      if (cell == _liberty.get_cell_map().end())
        continue;
      const auto values = constantInputs(instance);
      for (const auto& [port_name, port] : cell->second.get_port_map()) {
        if (!port.get_is_output())
          continue;
        const std::string output_pin = instance_name + ":" + port_name;
        if (!_network.pins().contains(output_pin))
          continue;
        const auto& three_state = port.get_three_state_expression();
        const auto disabled = three_state.get_is_empty() ? std::optional<bool>(false) : three_state.evaluate_constant(values);
        if (_values.driverDisabled(output_pin) != disabled) {
          _values.setDriverDisabled(output_pin, disabled);
          changed = true;
        }
        if (!disabled || *disabled)
          continue;
        const auto& function = port.get_function_expression();
        if (function.get_is_empty())
          continue;
        const auto value = function.evaluate_constant(values);
        if (value) {
          changed = updateDriverValue(output_pin, *value ? TimingCaseValue::kOne : TimingCaseValue::kZero) || changed;
        } else if (expressionInputsAreStatic(instance, function)) {
          changed = updateDriverValue(output_pin, TimingCaseValue::kStatic) || changed;
        }
      }
    }
  }
}

void LogicSimulator::disableInsensitiveArcs()
{
  for (Arc& arc : _graph.arcs()) {
    arc.set_is_case_analysis_disable(false);
    const Pin& source_pin = _network.pins().at(arc.get_source_pin());
    const Pin& sink_pin = _network.pins().at(arc.get_sink_pin());
    if ((arc.get_type() == ArcType::kNet && _values.isDriverDisabled(arc.get_source_pin()))
        || (arc.get_type() == ArcType::kCell && _values.isDriverDisabled(arc.get_sink_pin()))) {
      arc.set_is_case_analysis_disable(true);
      continue;
    }
    const auto source_value = _values.value(arc.get_source_pin(), arc.get_type() == ArcType::kNet && source_pin.get_direction() == PinDirection::kInout);
    const auto sink_value = _values.value(arc.get_sink_pin(), arc.get_type() == ArcType::kCell && sink_pin.get_direction() == PinDirection::kInout);
    if ((source_value && isStatic(*source_value)) || (sink_value && isStatic(*sink_value))) {
      arc.set_is_case_analysis_disable(true);
      continue;
    }
    if (arc.get_type() != ArcType::kCell)
      continue;
    const auto instance = _network.instances().find(arc.get_owner_name());
    if (instance == _network.instances().end())
      continue;
    const auto cell = _liberty.get_cell_map().find(instance->second.get_cell_name());
    if (cell == _liberty.get_cell_map().end())
      continue;
    const auto values = constantInputs(instance->second);
    const auto* cell_arc = arc.libertyArcId() ? _liberty.findCellArc(*arc.libertyArcId()) : nullptr;
    bool enabled = cell_arc == nullptr || cell_arc->get_timing_arc_list().empty();
    if (cell_arc != nullptr) {
      for (const TimingArc& timing_arc : cell_arc->get_timing_arc_list()) {
        const auto& condition = timing_arc.get_when_expression();
        if (condition.get_is_empty() || condition.evaluate_constant(values) != std::optional<bool>(false))
          enabled = true;
      }
    }
    if (!enabled) {
      arc.set_is_case_analysis_disable(true);
      continue;
    }
    // Sequential output functions name internal state. Liberty timing arcs
    // define both clock-to-Q and transparent latch data-to-Q sensitivity.
    if (arc.get_is_clock_arc() || (cell_arc != nullptr && cell_arc->get_is_latch_data_arc()))
      continue;
    const auto output = cell->second.get_port_map().find(arc.get_library_sink_port());
    if (output == cell->second.get_port_map().end())
      continue;
    const auto& expression
        = cell_arc != nullptr && cell_arc->isThreeStateArc() ? output->second.get_three_state_expression() : output->second.get_function_expression();
    if (!expression.get_is_empty() && !expression.is_sensitive_to(arc.get_library_source_port(), values)) {
      arc.set_is_case_analysis_disable(true);
    }
  }
}

void LogicSimulator::propagate()
{
  _values.clear();
  for (const auto& [pin, value] : _sdc.get_case_analysis_map())
    _values.setValue(pin, value);
  for (const auto& [pin_name, pin] : _network.pins()) {
    if (pin.get_constant_value() != TimingCaseValue::kNone) {
      if (!_values.value(pin_name))
        _values.setValue(pin_name, pin.get_constant_value());
      if (pin.get_direction() == PinDirection::kInout)
        _values.setValue(pin_name, pin.get_constant_value(), true);
    }
    const auto value = _sdc.get_case_analysis_map().find(pin_name);
    if (pin.get_direction() == PinDirection::kInout && value != _sdc.get_case_analysis_map().end()) {
      _values.setValue(pin_name, value->second, true);
    }
  }
  propagateCaseValues();
  disableInsensitiveArcs();
}
}  // namespace ista
