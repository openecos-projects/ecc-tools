// SPDX-License-Identifier: MulanPSL-2.0
#include "ClockGatingChecker.hpp"

#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "Logger.hpp"
#include "graph/TimingStateQuery.hpp"

namespace ista {
void ClockGatingChecker::infer()
{
  auto& graph = _database.graph();
  const auto& network = _database.network();
  graph.clearClockGatingChecks();
  TimingStateQuery query(_database);
  auto has_clock = [&](const std::string& pin) {
    const auto point = graph.points().find(pin);
    if (point != graph.points().end())
      for (const auto& [clock, state] : point->second.get_clock_state_map())
        if (!state.is_source_path_only && (state.is_structural || !state.waveforms.empty()))
          return true;
    return false;
  };
  // A clock can also feed ordinary data logic. Trace clock sinks and exported
  // clock outputs, keeping unrelated primary-clock roots as boundaries.
  std::map<std::string, std::set<std::string>> clock_fanin;
  std::vector<std::pair<std::string, std::string>> pending;
  auto seed = [&](const std::string& pin) {
    const auto point = graph.points().find(pin);
    if (point != graph.points().end())
      for (const auto& [clock, state] : point->second.get_clock_state_map())
        if (!state.is_source_path_only && (state.is_structural || !state.waveforms.empty()))
          pending.emplace_back(pin, clock);
  };
  for (const auto& [name, instance] : network.instances()) {
    if (!instance.get_is_sequential() && !instance.get_is_clock_gating())
      continue;
    seed(instance.get_clock_pin_name());
    for (const auto& check : instance.get_check_arc_list())
      seed(check.get_clock_port());
  }
  for (const auto& [name, definition] : _database.sdc().get_clock_map())
    if (definition.get_is_generated())
      for (const auto& target : definition.get_source_list())
        pending.emplace_back(target, definition.get_master_clock_name());
  for (const auto& [name, pin] : network.pins()) {
    if (!pin.get_is_port() || (pin.get_direction() != PinDirection::kOutput && pin.get_direction() != PinDirection::kInout))
      continue;
    bool primary_root = false;
    for (const auto& [clock, definition] : _database.sdc().get_clock_map())
      if (!definition.get_is_generated())
        for (const auto& source : definition.get_source_objects())
          primary_root |= source == name;
    if (primary_root)
      continue;
    const auto incoming = graph.incomingArcs().find(name);
    if (incoming == graph.incomingArcs().end())
      continue;
    for (auto index : incoming->second) {
      auto& arc = graph.arcs()[index];
      if (arc.get_type() != ArcType::kNet || query.isDisableArc(arc))
        continue;
      const auto& point = graph.sourcePoint(arc);
      for (const auto& [clock, state] : point.get_clock_state_map())
        if (!state.is_source_path_only && (state.is_structural || !state.waveforms.empty())
            && _database.sdc().clockAllowsNetArc(network, clock, arc.get_source_pin(), name))
          pending.emplace_back(arc.get_source_pin(), clock);
    }
  }
  while (!pending.empty()) {
    auto [pin, clock] = std::move(pending.back());
    pending.pop_back();
    if (!clock_fanin[pin].insert(clock).second)
      continue;
    const auto incoming = graph.incomingArcs().find(pin);
    if (incoming == graph.incomingArcs().end())
      continue;
    for (auto index : incoming->second) {
      auto& arc = graph.arcs()[index];
      const auto* point = clockPoint(_database, arc.get_source_pin(), clock);
      const auto* state = point ? point->find_clock_state(clock) : nullptr;
      if (arc.get_is_clock_arc() || query.isDisableArc(arc) || !state || state->is_source_path_only || (!state->is_structural && state->waveforms.empty()))
        continue;
      if (arc.get_type() == ArcType::kNet && !_database.sdc().clockAllowsNetArc(network, clock, arc.get_source_pin(), arc.get_sink_pin()))
        continue;
      pending.emplace_back(arc.get_source_pin(), clock);
    }
  }
  std::set<std::tuple<std::string, std::string, bool, std::string>> inferred;
  for (const auto& [output, output_clocks] : clock_fanin) {
    const auto pin = network.pins().find(output);
    if (pin == network.pins().end() || pin->second.get_is_port())
      continue;
    const auto& instance = network.instances().at(pin->second.get_instance_name());
    if (instance.get_is_sequential() || instance.get_is_clock_gating())
      continue;
    const auto cell = _database.liberty().get_cell_map().find(instance.get_cell_name());
    if (cell == _database.liberty().get_cell_map().end())
      continue;
    const auto port = cell->second.get_port_map().find(pin->second.get_pin_name());
    if (port == cell->second.get_port_map().end() || !port->second.get_is_output())
      continue;
    const auto& function = port->second.get_function_expression();
    if (function.get_is_empty())
      continue;
    std::map<std::string, bool> constants;
    for (const auto& term : function.get_term_list()) {
      if (term.get_operation_type() != LogicOperationType::kPort)
        continue;
      const auto value = graph.simValues().value(instance.get_instance_name() + ":" + term.get_port_name());
      if (value && (*value == TimingCaseValue::kZero || *value == TimingCaseValue::kOne))
        constants[term.get_port_name()] = *value == TimingCaseValue::kOne;
    }
    const auto incoming = graph.incomingArcs().find(output);
    if (incoming == graph.incomingArcs().end())
      continue;
    for (auto clock_index : incoming->second) {
      auto& clock_arc = graph.arcs()[clock_index];
      const auto& clock_pin = clock_arc.get_source_pin();
      if (clock_arc.get_type() != ArcType::kCell || query.isDisableArc(clock_arc) || !clock_fanin.contains(clock_pin))
        continue;
      const auto& clock_port = network.pins().at(clock_pin).get_pin_name();
      auto values = constants;
      values[clock_port] = false;
      const bool low_constant = function.evaluate_constant(values).has_value();
      values[clock_port] = true;
      const bool high_constant = function.evaluate_constant(values).has_value();
      // One clock level must block every data change. A buffer is constant
      // for both cofactors; XOR/mux functions generally have neither.
      if (low_constant == high_constant)
        continue;
      const bool active_high = low_constant;
      values[clock_port] = active_high;
      for (auto enable_index : incoming->second) {
        auto& enable_arc = graph.arcs()[enable_index];
        const auto& enable = enable_arc.get_source_pin();
        if (enable_arc.get_type() != ArcType::kCell || query.isDisableArc(enable_arc) || has_clock(enable))
          continue;
        const auto& enable_port = network.pins().at(enable).get_pin_name();
        if (constants.contains(enable_port) || !function.is_sensitive_to(enable_port, values))
          continue;
        // Explicit Liberty checks take precedence over inferred zero margins.
        bool explicit_check = false;
        for (const auto& check : instance.get_check_arc_list())
          explicit_check |= check.get_clock_port() == clock_pin && check.get_data_port() == enable;
        if (explicit_check)
          continue;
        for (const auto& clock : clock_fanin.at(clock_pin))
          if (output_clocks.contains(clock) && inferred.emplace(enable, clock_pin, active_high, clock).second)
            for (bool setup : {true, false}) {
              TimingCheckArc check;
              check.set_clock_port(clock_pin);
              check.set_data_port(enable);
              check.setCaptureClock(clock);
              check.set_check_type(setup ? TimingCheckType::kClockGatingSetup : TimingCheckType::kClockGatingHold);
              check.set_clock_trans_type(setup == active_high ? TransType::kRise : TransType::kFall);
              graph.addClockGatingCheck(std::move(check));
            }
      }
    }
  }
  if (!graph.clockGatingChecks().empty())
    STALOG.info(Loc::current(), "Inferred combinational clock-gating endpoints: ", graph.clockGatingChecks().size());
}
}  // namespace ista
