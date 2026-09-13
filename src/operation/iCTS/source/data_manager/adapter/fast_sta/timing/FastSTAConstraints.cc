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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file FastSTAConstraints.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief Case propagation, clock-domain resolution, and event properties.
 */

#include "FastSTAConstraints.hh"

#include <fnmatch.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <queue>
#include <string_view>
#include <utility>
#include <vector>

#include "SDCClockReader.hh"
#include "clock_state/FastSTAClockState.hh"
#include "liberty/FastSTALibertyModel.hh"

namespace icts {
namespace {

auto pathExceptionCommand(SdcExceptionKind kind) -> const char*
{
  switch (kind) {
    case SdcExceptionKind::kFalsePath:
      return "set_false_path";
    case SdcExceptionKind::kMulticyclePath:
      return "set_multicycle_path";
    case SdcExceptionKind::kMinDelay:
      return "set_min_delay";
    case SdcExceptionKind::kMaxDelay:
      return "set_max_delay";
  }
  return "path_exception";
}

auto validateConstraintObjects(const FastStaContext& context) -> std::optional<std::string>
{
  std::optional<std::string> failure;
  const auto validate = [&](const std::vector<SdcObjectRef>& objects, std::string_view command, std::string_view field) -> void {
    if (failure.has_value()) {
      return;
    }
    for (const auto& object : objects) {
      bool found = false;
      if (object.kind == SdcObjectKind::kClock) {
        found = std::ranges::any_of(context.constraints.clocks, [&](const auto& clock) -> bool {
          return FastStaConstraints::matches(context, object, kInvalidFastStaNodeId, clock.clock_name);
        });
        if (context.constraints.clocks.empty()) {
          found = FastStaConstraints::matches(context, object, kInvalidFastStaNodeId, context.clock_name);
        }
      } else if (object.kind == SdcObjectKind::kNet) {
        found = std::ranges::any_of(
            context.nets, [&](const auto& net) -> bool { return object.pattern == net.name || fnmatch(object.pattern.c_str(), net.name.c_str(), 0) == 0; });
      } else {
        for (FastStaNodeId node_id = 0U; node_id < context.nodes.size() && !found; ++node_id) {
          found = FastStaConstraints::matches(context, object, node_id);
        }
      }
      if (!found) {
        failure = "sdc_object_unresolved:" + std::string(command) + ":" + std::string(field) + ":" + object.pattern;
        return;
      }
    }
  };
  for (const auto& clock : context.constraints.clocks) {
    validate(clock.generated_sources, "create_generated_clock", "source");
  }
  for (const auto& exception : context.constraints.path_exceptions) {
    const auto* command = pathExceptionCommand(exception.kind);
    validate(exception.path.from.objects, command, "from");
    validate(exception.path.to.objects, command, "to");
    for (const auto& through : exception.path.through) {
      validate(through.objects, command, "through");
    }
  }
  for (const auto& group : context.constraints.clock_groups) {
    for (const auto& clocks : group.groups) {
      validate(clocks, "set_clock_groups", "group");
    }
  }
  for (const auto& latency : context.constraints.clock_latencies) {
    validate(latency.objects, "set_clock_latency", "objects");
    validate(latency.clocks, "set_clock_latency", "clock");
  }
  for (const auto& uncertainty : context.constraints.clock_uncertainties) {
    validate(uncertainty.objects, "set_clock_uncertainty", "objects");
    validate(uncertainty.from.objects, "set_clock_uncertainty", "from");
    validate(uncertainty.to.objects, "set_clock_uncertainty", "to");
  }
  for (const auto& transition : context.constraints.clock_transitions) {
    validate(transition.clocks, "set_clock_transition", "objects");
  }
  for (const auto input : {true, false}) {
    const auto* command = input ? "set_input_delay" : "set_output_delay";
    for (const auto& delay : input ? context.constraints.input_delays : context.constraints.output_delays) {
      validate(delay.objects, command, "objects");
      validate(delay.clocks, command, "clock");
    }
  }
  for (const auto& transition : context.constraints.input_transitions) {
    validate(transition.objects, "set_input_transition", "objects");
  }
  for (const auto& load : context.constraints.loads) {
    validate(load.objects, "set_load", "objects");
  }
  validate(context.constraints.propagated_clocks, "set_propagated_clock", "objects");
  return failure;
}

auto expandedGateFunction(const FastStaClockGateModel& gate) -> std::string
{
  std::string result;
  for (std::size_t position = 0U; position < gate.output_expression.size();) {
    const auto token = gate.output_expression[position];
    if (std::isspace(static_cast<unsigned char>(token)) != 0 || std::string_view("!~'&*|+^()").find(token) != std::string_view::npos) {
      result += token;
      ++position;
      continue;
    }
    const auto begin = position;
    while (position < gate.output_expression.size() && std::isspace(static_cast<unsigned char>(gate.output_expression[position])) == 0
           && std::string_view("!~'&*|+^()").find(gate.output_expression[position]) == std::string_view::npos) {
      ++position;
    }
    const auto name = gate.output_expression.substr(begin, position - begin);
    if (!gate.state.empty() && name == gate.state) {
      result += "(" + gate.data_expression + ")";
    } else if (!gate.inverted_state.empty() && name == gate.inverted_state) {
      result += "!(" + gate.data_expression + ")";
    } else {
      result += name;
    }
  }
  return result;
}

auto resolveCaseValues(FastStaContext& context) -> std::optional<std::string>
{
  context.case_values.clear();
  for (auto& node : context.nodes) {
    node.case_value.reset();
    node.clock_inactive = false;
  }
  for (const auto& assignment : context.constraints.case_analyses) {
    if (assignment.value != 0 && assignment.value != 1) {
      return "case_analysis_value_not_boolean";
    }
    for (const auto& object : assignment.objects) {
      bool found = false;
      for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
        if (FastStaConstraints::matches(context, object, node_id)) {
          auto& node = context.nodes.at(node_id);
          const auto value = assignment.value != 0;
          if (node.case_value.has_value() && *node.case_value != value) {
            return "conflicting_case_analysis:" + node.name;
          }
          node.case_value = value;
          context.case_values[node.name] = value;
          found = true;
        }
      }
      if (!found) {
        return "case_analysis_object_unresolved:" + object.pattern;
      }
    }
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& net : context.nets) {
      if (net.driver_node_id >= context.nodes.size()) {
        return "case_net_driver_unavailable:" + net.name;
      }
      const auto source = context.nodes.at(net.driver_node_id).case_value;
      if (!source.has_value()) {
        continue;
      }
      for (const auto node_id : net.load_node_ids) {
        auto& load = context.nodes.at(node_id);
        if (load.case_value.has_value() && load.case_value != source) {
          return "conflicting_case_connectivity:" + load.name;
        }
        if (!load.case_value.has_value()) {
          load.case_value = source;
          context.case_values[load.name] = *source;
          changed = true;
        }
      }
    }
    for (auto& node : context.nodes) {
      if (node.case_value.has_value() || node.logic_function.empty()) {
        continue;
      }
      auto expression = node.logic_function;
      const auto cell = context.liberty_cell_by_master.find(node.cell_master);
      if (cell != context.liberty_cell_by_master.end() && cell->second.clock_gate.has_value()) {
        const auto& gate = *cell->second.clock_gate;
        if (gate.output_port == node.port_name && (!gate.latch_based || !gate.data_expression.empty())) {
          expression = expandedGateFunction(gate);
        }
      }
      const auto value = FastStaCondition::evaluate(expression, FastStaConstraints::caseLookup(context, node.inst_name));
      if (value == FastStaLogicValue::kInvalid) {
        return "invalid_liberty_function:" + node.name + ":" + expression;
      }
      if (value == FastStaLogicValue::kZero || value == FastStaLogicValue::kOne) {
        node.case_value = value == FastStaLogicValue::kOne;
        context.case_values[node.name] = *node.case_value;
        changed = true;
      }
    }
  }
  return std::nullopt;
}

auto resolveClockDomains(FastStaContext& context) -> std::optional<std::string>
{
  if (context.initial_node_kinds.empty()) {
    for (const auto& node : context.nodes) {
      context.initial_node_kinds.push_back(node.kind);
      context.initial_node_domains.push_back(node.domain);
    }
    for (const auto& net : context.nets) {
      context.initial_net_domains.push_back(net.domain);
    }
    context.initial_buffer_inputs = context.buffer_input_node_id_by_inst;
    context.initial_buffer_outputs = context.buffer_output_node_id_by_inst;
  }
  for (std::size_t index = 0U; index < context.nodes.size(); ++index) {
    auto& node = context.nodes.at(index);
    node.kind = context.initial_node_kinds.at(index);
    node.domain = context.initial_node_domains.at(index);
    node.clock_name.clear();
    node.clock_inactive = false;
    node.clock_from_port.clear();
    node.clock_to_port.clear();
    node.clock_edge_triggered = false;
  }
  for (std::size_t index = 0U; index < context.nets.size(); ++index) {
    context.nets.at(index).domain = context.initial_net_domains.at(index);
  }
  context.buffer_input_node_id_by_inst = context.initial_buffer_inputs;
  context.buffer_output_node_id_by_inst = context.initial_buffer_outputs;
  context.clock_source_node_ids.clear();
  std::vector<std::string> declared_clock(context.nodes.size());
  for (const auto& declaration : context.constraints.clocks) {
    for (const auto& target : declaration.targets) {
      bool found = false;
      for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
        const auto net_driver_target
            = target.kind != SdcObjectKind::kNet || std::ranges::any_of(context.nodes.at(node_id).output_net_ids, [&](const auto net_id) -> bool {
                return target.pattern == context.nets.at(net_id).name || fnmatch(target.pattern.c_str(), context.nets.at(net_id).name.c_str(), 0) == 0;
              });
        if (!net_driver_target || !FastStaConstraints::matches(context, target, node_id)) {
          continue;
        }
        if (!declared_clock.at(node_id).empty() && declared_clock.at(node_id) != declaration.clock_name) {
          return "multiple_clocks_on_one_source:" + context.nodes.at(node_id).name;
        }
        declared_clock.at(node_id) = declaration.clock_name;
        if (declaration.kind == SdcClockDecl::Kind::kPrimary) {
          context.clock_source_node_ids.push_back(node_id);
        }
        found = true;
      }
      if (!found && !declaration.is_virtual && declaration.clock_name == context.clock_name && target.pattern == context.clock_net_name
          && context.source_node_id < context.nodes.size()) {
        // A clock sizing context intentionally omits the data-path timing
        // graph. Preserve the already identified physical source when the
        // source net is the declared clock target, so the clock-only context
        // uses the same SDC authority as its complete parent context.
        declared_clock.at(context.source_node_id) = declaration.clock_name;
        context.clock_source_node_ids.push_back(context.source_node_id);
        found = true;
      }
      if (!found && !declaration.is_virtual) {
        return "clock_target_unresolved:" + declaration.clock_name + ":" + target.pattern;
      }
    }
  }
  if (context.clock_source_node_ids.empty() && context.source_node_id < context.nodes.size()) {
    context.clock_source_node_ids.push_back(context.source_node_id);
  }
  std::ranges::sort(context.clock_source_node_ids);
  context.clock_source_node_ids.erase(std::ranges::unique(context.clock_source_node_ids).begin(), context.clock_source_node_ids.end());
  if (context.source_node_id == kInvalidFastStaNodeId && !context.clock_source_node_ids.empty()) {
    context.source_node_id = context.clock_source_node_ids.front();
    context.clock_name = declared_clock.at(context.source_node_id);
    const auto* declaration = FastStaConstraints::clock(context, context.clock_name);
    if (declaration != nullptr) {
      context.clock_period_ns = declaration->period_ns;
    }
  }
  std::vector<std::vector<std::size_t>> cell_arcs(context.nodes.size());
  std::vector<std::vector<std::size_t>> launch_arcs(context.nodes.size());
  for (std::size_t index = 0U; index < context.timing_launches.size(); ++index) {
    const auto& launch = context.timing_launches.at(index);
    if (launch.clock_node_id >= context.nodes.size() || launch.output_node_id >= context.nodes.size()) {
      return "clock_launch_node_out_of_range";
    }
    launch_arcs.at(launch.clock_node_id).push_back(index);
  }
  for (std::size_t arc_index = 0U; arc_index < context.timing_arcs.size(); ++arc_index) {
    const auto& arc = context.timing_arcs.at(arc_index);
    if (arc.from_node_id >= context.nodes.size() || arc.to_node_id >= context.nodes.size()) {
      return "clock_arc_node_out_of_range";
    }
    cell_arcs.at(arc.from_node_id).push_back(arc_index);
  }
  std::queue<FastStaNodeId> ready;
  std::vector<bool> clock_sources(context.nodes.size(), false);
  std::vector<bool> reached(context.nodes.size(), false);
  for (const auto node_id : context.clock_source_node_ids) {
    clock_sources.at(node_id) = true;
    auto& node = context.nodes.at(node_id);
    node.clock_name = declared_clock.at(node_id).empty() ? context.clock_name : declared_clock.at(node_id);
    node.domain = FastStaNodeDomain::kClock;
    // A characterization topology deliberately starts at a buffer's input.
    // Only an explicit create_clock target cuts the upstream propagation arc.
    if (!declared_clock.at(node_id).empty()) {
      node.kind = FastStaNodeKind::kSource;
    }
    ready.push(node_id);
    reached.at(node_id) = true;
  }
  const auto reach = [&](FastStaNodeId node_id, const std::string& parent_clock) -> void {
    if (clock_sources.at(node_id)) {
      return;
    }
    auto& node = context.nodes.at(node_id);
    node.domain = FastStaNodeDomain::kClock;
    node.clock_name = declared_clock.at(node_id).empty() ? parent_clock : declared_clock.at(node_id);
    if (!reached.at(node_id)) {
      reached.at(node_id) = true;
      ready.push(node_id);
    }
  };
  while (!ready.empty()) {
    const auto node_id = ready.front();
    ready.pop();
    auto& node = context.nodes.at(node_id);
    for (const auto net_id : node.output_net_ids) {
      auto& net = context.nets.at(net_id);
      net.domain = FastStaNetDomain::kClock;
      for (const auto load_id : net.load_node_ids) {
        reach(load_id, node.clock_name);
      }
    }
    for (const auto arc_index : cell_arcs.at(node_id)) {
      const auto& arc = context.timing_arcs.at(arc_index);
      if (clock_sources.at(arc.to_node_id)) {
        continue;
      }
      auto& output = context.nodes.at(arc.to_node_id);
      const auto& model = context.liberty_cell_by_master.at(arc.cell_master);
      const auto active = std::ranges::any_of(model.timing_arcs, [&](const auto& variant) -> bool {
        return variant.from_port == arc.from_port && variant.to_port == arc.to_port
               && FastStaCondition::evaluate(variant.when, FastStaConstraints::caseLookup(context, output.inst_name)) != FastStaLogicValue::kZero;
      });
      if (!active && !model.clock_gate.has_value()) {
        continue;
      }
      node.kind = FastStaNodeKind::kBufferInput;
      output.kind = FastStaNodeKind::kBufferOutput;
      const auto existing = context.buffer_input_node_id_by_inst.find(output.inst_name);
      if (existing != context.buffer_input_node_id_by_inst.end() && existing->second != node_id) {
        return "multiple_active_clock_inputs:" + output.inst_name;
      }
      context.buffer_input_node_id_by_inst[output.inst_name] = node_id;
      context.buffer_output_node_id_by_inst[output.inst_name] = arc.to_node_id;
      output.clock_from_port = arc.from_port;
      output.clock_to_port = arc.to_port;
      reach(arc.to_node_id, node.clock_name);
    }
    for (const auto launch_index : launch_arcs.at(node_id)) {
      const auto& launch = context.timing_launches.at(launch_index);
      if (declared_clock.at(launch.output_node_id).empty() || clock_sources.at(launch.output_node_id)) {
        continue;
      }
      const auto* declaration = FastStaConstraints::clock(context, declared_clock.at(launch.output_node_id));
      if (declaration != nullptr && declaration->combinational) {
        return "generated_combinational_clock_crosses_sequential_arc:" + declaration->clock_name;
      }
      auto& output = context.nodes.at(launch.output_node_id);
      node.kind = FastStaNodeKind::kBufferInput;
      output.kind = FastStaNodeKind::kBufferOutput;
      output.clock_from_port = launch.clock_port;
      output.clock_to_port = launch.output_port;
      output.clock_edge_triggered = true;
      output.clock_trigger_transition = launch.clock_transition;
      context.buffer_input_node_id_by_inst[output.inst_name] = node_id;
      context.buffer_output_node_id_by_inst[output.inst_name] = launch.output_node_id;
      reach(launch.output_node_id, node.clock_name);
    }
    if (node.kind == FastStaNodeKind::kBufferInput) {
      const auto output = context.buffer_output_node_id_by_inst.find(node.inst_name);
      if (output != context.buffer_output_node_id_by_inst.end() && !clock_sources.at(output->second)) {
        reach(output->second, node.clock_name);
      }
    }
  }
  std::vector<std::vector<FastStaNodeId>> clock_edges(context.nodes.size());
  std::vector<std::size_t> indegree(context.nodes.size(), 0U);
  std::size_t clock_node_count = 0U;
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (node.domain != FastStaNodeDomain::kClock) {
      continue;
    }
    ++clock_node_count;
    if (node.kind == FastStaNodeKind::kBufferInput) {
      const auto output = context.buffer_output_node_id_by_inst.find(node.inst_name);
      if (output != context.buffer_output_node_id_by_inst.end() && !clock_sources.at(output->second)) {
        clock_edges.at(node_id).push_back(output->second);
        ++indegree.at(output->second);
      }
    }
    for (const auto net_id : node.output_net_ids) {
      for (const auto load_id : context.nets.at(net_id).load_node_ids) {
        if (clock_sources.at(load_id)) {
          continue;
        }
        clock_edges.at(node_id).push_back(load_id);
        ++indegree.at(load_id);
      }
    }
  }
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    if (context.nodes.at(node_id).domain == FastStaNodeDomain::kClock && indegree.at(node_id) == 0U) {
      ready.push(node_id);
    }
  }
  std::size_t visited = 0U;
  while (!ready.empty()) {
    const auto node_id = ready.front();
    ready.pop();
    ++visited;
    for (const auto child : clock_edges.at(node_id)) {
      if (--indegree.at(child) == 0U) {
        ready.push(child);
      }
    }
  }
  if (visited != clock_node_count) {
    return "clock_timing_graph_contains_cycle";
  }
  for (auto& declaration : context.constraints.clocks) {
    if (declaration.kind != SdcClockDecl::Kind::kGenerated) {
      continue;
    }
    std::string master;
    std::vector<FastStaNodeId> source_nodes;
    for (const auto& source : declaration.generated_sources) {
      for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
        const auto& node = context.nodes.at(node_id);
        if (node.clock_name.empty() || node.clock_name == declaration.clock_name || !FastStaConstraints::matches(context, source, node_id, node.clock_name)) {
          continue;
        }
        if (!master.empty() && master != node.clock_name) {
          return "generated_clock_master_ambiguous:" + declaration.clock_name;
        }
        master = node.clock_name;
        source_nodes.push_back(node_id);
      }
    }
    if (master.empty()) {
      return "generated_clock_source_unavailable:" + declaration.clock_name;
    }
    if (!declaration.master_clock_name.empty() && declaration.master_clock_name != master) {
      return "generated_clock_source_master_mismatch:" + declaration.clock_name;
    }
    std::vector<bool> source_reachable(context.nodes.size(), false);
    for (const auto source_node : source_nodes) {
      if (!source_reachable.at(source_node)) {
        source_reachable.at(source_node) = true;
        ready.push(source_node);
      }
    }
    while (!ready.empty()) {
      const auto node_id = ready.front();
      ready.pop();
      for (const auto child : clock_edges.at(node_id)) {
        if (!source_reachable.at(child)) {
          source_reachable.at(child) = true;
          ready.push(child);
        }
      }
    }
    for (FastStaNodeId node_id = 0U; node_id < declared_clock.size(); ++node_id) {
      if (declared_clock.at(node_id) == declaration.clock_name && !source_reachable.at(node_id)) {
        return "generated_clock_target_not_reached_from_source:" + declaration.clock_name + ":" + context.nodes.at(node_id).name;
      }
    }
    declaration.master_clock_name = master;
  }
  SdcClockReader::resolveGeneratedClocks(context.constraints);
  for (const auto& declaration : context.constraints.clocks) {
    if (!declaration.period_resolved || !std::isfinite(declaration.period_ns) || declaration.period_ns <= 0.0 || !declaration.waveform_resolved
        || declaration.waveform_ns.size() != 2U) {
      return "clock_waveform_unresolved:" + declaration.clock_name;
    }
  }
  if (const auto* declaration = FastStaConstraints::clock(context, context.clock_name); declaration != nullptr) {
    context.clock_period_ns = declaration->period_ns;
  }
  return std::nullopt;
}

}  // namespace

auto FastStaConstraints::caseLookup(const FastStaContext& context, const std::string& inst_name) -> FastStaCondition::PinValueLookup
{
  return [&context, inst_name](const std::string& port) -> std::optional<bool> {
    const auto name = inst_name.empty() ? port : inst_name + "/" + port;
    const auto found = context.case_values.find(name);
    return found == context.case_values.end() ? std::nullopt : std::optional<bool>{found->second};
  };
}

auto FastStaConstraints::gateActive(const FastStaContext& context, const std::string& inst_name, const FastStaClockGateModel& gate) -> FastStaLogicValue
{
  if (gate.latch_based && (gate.state.empty() || gate.data_expression.empty() || gate.latch_enable_expression.empty())) {
    return FastStaLogicValue::kInvalid;
  }
  return FastStaCondition::isSensitized(expandedGateFunction(gate), gate.clock_port, caseLookup(context, inst_name));
}

auto FastStaConstraints::transitionMatches(SdcTransition selection, FastStaTransition transition) -> bool
{
  return selection == SdcTransition::kBoth || (selection == SdcTransition::kRise) == (transition == FastStaTransition::kRise);
}

auto FastStaConstraints::matches(const FastStaContext& context, const SdcObjectRef& object, FastStaNodeId node_id, const std::string& clock_name) -> bool
{
  if (object.kind == SdcObjectKind::kClock) {
    return !clock_name.empty() && (object.pattern == clock_name || fnmatch(object.pattern.c_str(), clock_name.c_str(), 0) == 0);
  }
  if (node_id >= context.nodes.size()) {
    return false;
  }
  const auto& node = context.nodes.at(node_id);
  if (object.kind == SdcObjectKind::kPort && !node.top_level) {
    return false;
  }
  if (object.kind == SdcObjectKind::kPin && node.top_level) {
    return false;
  }
  if (object.kind == SdcObjectKind::kNet) {
    if (node.incoming_net_id < context.nets.size()
        && (object.pattern == context.nets.at(node.incoming_net_id).name
            || fnmatch(object.pattern.c_str(), context.nets.at(node.incoming_net_id).name.c_str(), 0) == 0)) {
      return true;
    }
    return std::ranges::any_of(node.output_net_ids, [&](auto net_id) -> bool {
      return net_id < context.nets.size()
             && (object.pattern == context.nets.at(net_id).name || fnmatch(object.pattern.c_str(), context.nets.at(net_id).name.c_str(), 0) == 0);
    });
  }
  return node.name == object.pattern || fnmatch(object.pattern.c_str(), node.name.c_str(), 0) == 0;
}

auto FastStaConstraints::matches(const FastStaContext& context, const SdcPathSelector& selector, FastStaNodeId node_id, FastStaTransition transition,
                                 const std::string& clock_name) -> bool
{
  return transitionMatches(selector.transition, transition)
         && (selector.objects.empty()
             || std::ranges::any_of(selector.objects, [&](const auto& object) -> bool { return matches(context, object, node_id, clock_name); }));
}

auto FastStaConstraints::clock(const FastStaContext& context, const std::string& name) -> const SdcClockDecl*
{
  const auto found = std::ranges::find(context.constraints.clocks, name, &SdcClockDecl::clock_name);
  return found == context.constraints.clocks.end() ? nullptr : &*found;
}

auto FastStaConstraints::period(const FastStaContext& context, const std::string& name) -> double
{
  const auto* declaration = clock(context, name);
  return declaration == nullptr ? context.clock_period_ns : declaration->period_ns;
}

auto FastStaConstraints::isPropagated(const FastStaContext& context, const std::string& name) -> bool
{
  return context.propagate_all_clocks || context.constraints.clocks.empty()
         || std::ranges::any_of(context.constraints.propagated_clocks,
                                [&](const auto& object) -> bool { return matches(context, object, kInvalidFastStaNodeId, name); });
}

auto FastStaConstraints::phase(const FastStaContext& context, const std::string& name, FastStaTransition transition) -> double
{
  const auto* declaration = clock(context, name);
  if (declaration != nullptr && declaration->waveform_ns.size() >= 2U) {
    return declaration->waveform_ns.at(transition == FastStaTransition::kRise ? 0U : 1U);
  }
  return transition == FastStaTransition::kRise ? 0.0 : 0.5 * context.clock_period_ns;
}

auto FastStaConstraints::latency(const FastStaContext& context, FastStaNodeId node_id, const std::string& clock_name, FastStaTransition transition, bool early,
                                 bool source) -> double
{
  double result = 0.0;
  for (const auto& latency : context.constraints.clock_latencies) {
    if (latency.source != source || !(early ? latency.min && latency.early : latency.max && latency.late)
        || !transitionMatches(latency.transition, transition)) {
      continue;
    }
    const auto matches_objects
        = std::ranges::any_of(latency.objects, [&](const auto& object) -> bool { return matches(context, object, node_id, clock_name); });
    const auto matches_clocks = latency.clocks.empty() || std::ranges::any_of(latency.clocks, [&](const auto& object) -> bool {
                                  return matches(context, object, node_id, clock_name);
                                });
    if (matches_objects && matches_clocks) {
      result = latency.value_ns;
    }
  }
  return result;
}

auto FastStaConstraints::prepare(FastStaContext& context) -> std::optional<std::string>
{
  if (!context.constraints.ok()) {
    return "invalid_sdc_constraints:" + (context.constraints.diagnostics.empty() ? std::string{"unspecified"} : context.constraints.diagnostics.front());
  }
  for (const auto& clock : context.constraints.clocks) {
    if (clock.waveform_ns.size() > 2U) {
      return "unsupported_sdc_field:clock.waveform_ns:multiple_rise_fall_pairs";
    }
  }
  for (const auto& exception : context.constraints.path_exceptions) {
    if (exception.reset_path || exception.match_start_end) {
      return exception.reset_path ? "unsupported_sdc_field:path_exception.reset_path" : "unsupported_sdc_field:path_exception.match_start_end";
    }
  }
  for (const auto& load : context.constraints.loads) {
    if (load.wire_load) {
      return "unsupported_sdc_field:load.wire_load";
    }
    if (load.subtract_pin_load) {
      return "unsupported_sdc_field:load.subtract_pin_load";
    }
    if (std::ranges::any_of(load.objects, [](const auto& object) -> bool { return object.kind == SdcObjectKind::kNet; })) {
      return "unsupported_sdc_field:load.net_object";
    }
    for (const auto& object : load.objects) {
      if (object.kind != SdcObjectKind::kPort && object.kind != SdcObjectKind::kUnknown) {
        return "unsupported_sdc_field:load.object_kind:" + object.pattern;
      }
      for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
        const auto& node = context.nodes.at(node_id);
        if (matches(context, object, node_id) && (!node.top_level || !node.output)) {
          return "unsupported_sdc_field:load.target:" + node.name;
        }
      }
    }
  }
  // Collection syntax is normalized before the timing graph exists. Resolve
  // identity here without requiring an eligible timing path through the object.
  if (auto error = validateConstraintObjects(context); error.has_value()) {
    return error;
  }
  for (const auto input : {true, false}) {
    const auto* command = input ? "set_input_delay" : "set_output_delay";
    for (const auto& delay : input ? context.constraints.input_delays : context.constraints.output_delays) {
      if (delay.clocks.empty()) {
        return "unsupported_sdc_field:" + std::string(command) + ":reference_clock_required";
      }
      if (std::ranges::any_of(delay.objects, [](const auto& object) -> bool { return object.kind == SdcObjectKind::kPin; })) {
        return "unsupported_sdc_field:" + std::string(command) + ":internal_pin_target";
      }
    }
  }
  if (auto error = resolveCaseValues(context); error.has_value()) {
    return error;
  }
  for (const auto& node : context.nodes) {
    const auto model = context.liberty_cell_by_master.find(node.cell_master);
    if (model == context.liberty_cell_by_master.end()) {
      continue;
    }
    for (const auto& arc : model->second.timing_arcs) {
      if (FastStaCondition::evaluate(arc.when, caseLookup(context, node.inst_name)) == FastStaLogicValue::kInvalid) {
        return "invalid_liberty_when:" + node.name;
      }
    }
    if (model->second.clock_gate.has_value() && gateActive(context, node.inst_name, *model->second.clock_gate) == FastStaLogicValue::kInvalid) {
      return "invalid_liberty_clock_gate:" + node.name;
    }
  }
  for (const auto& [node_id, caps] : context.unconstrained_io_caps) {
    auto& node = context.nodes.at(node_id);
    node.input_cap_pf_by_timing = caps;
    node.input_cap_pf = std::max({caps.at(0).at(0), caps.at(0).at(1), caps.at(1).at(0), caps.at(1).at(1)});
  }
  for (const auto& load : context.constraints.loads) {
    for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
      auto& node = context.nodes.at(node_id);
      if (!node.top_level || !node.output
          || !std::ranges::any_of(load.objects, [&](const auto& object) -> bool { return matches(context, object, node_id); })) {
        continue;
      }
      context.unconstrained_io_caps.try_emplace(node_id, node.input_cap_pf_by_timing);
      node.input_cap_profile_available = true;
      for (const auto early : {true, false}) {
        for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
          if ((early ? load.min : load.max) && transitionMatches(load.transition, transition)) {
            node.input_cap_pf_by_timing.at(early ? 0U : 1U).at(transition == FastStaTransition::kRise ? 0U : 1U) = load.value_pf;
          }
        }
      }
      node.input_cap_pf = std::max({node.input_cap_pf_by_timing.at(0).at(0), node.input_cap_pf_by_timing.at(0).at(1), node.input_cap_pf_by_timing.at(1).at(0),
                                    node.input_cap_pf_by_timing.at(1).at(1)});
    }
  }
  if (auto error = resolveClockDomains(context); error.has_value()) {
    return error;
  }
  for (const auto input : {true, false}) {
    const auto& delays = input ? context.constraints.input_delays : context.constraints.output_delays;
    for (const auto& delay : delays) {
      if (delay.reference_pins.empty()) {
        continue;
      }
      for (const auto& declaration : context.constraints.clocks) {
        if (!std::ranges::any_of(delay.clocks,
                                 [&](const auto& object) -> bool { return matches(context, object, kInvalidFastStaNodeId, declaration.clock_name); })) {
          continue;
        }
        std::size_t reference_count = 0U;
        for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
          const auto& node = context.nodes.at(node_id);
          reference_count += node.domain == FastStaNodeDomain::kClock && node.clock_name == declaration.clock_name
                                     && std::ranges::any_of(delay.reference_pins, [&](const auto& object) -> bool { return matches(context, object, node_id); })
                                 ? 1U
                                 : 0U;
        }
        if (reference_count != 1U) {
          return std::string("sdc_io_reference_pin_") + (reference_count == 0U ? "unresolved:" : "ambiguous:")
                 + (input ? "set_input_delay:" : "set_output_delay:") + declaration.clock_name;
        }
      }
    }
  }
  return std::nullopt;
}

}  // namespace icts
