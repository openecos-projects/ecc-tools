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
 * @file FastSTAClockPropagation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Propagate four-state clock timing through real nets and buffer arcs.
 */

#include "FastSTAClockPropagation.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

#include "FastSTAClockState.hh"
#include "FastSTAConstraints.hh"
#include "FastSTADmpCeff.hh"
#include "FastSTAEvents.hh"
#include "FastSTALibertyModel.hh"
#include "FastSTATiming.hh"
#include "FastSTATimingLookup.hh"
#include "clock_sizing/FastSTAClockSizingEdit.hh"
#include "timing/FastSTAClockTiming.hh"

namespace icts {
namespace fast_sta {

namespace {

auto findBufferInputNode(const FastStaContext& context, const FastStaNode& output_node) -> FastStaNodeId
{
  if (output_node.inst_name.empty()) {
    return kInvalidFastStaNodeId;
  }
  if (const auto indexed = context.buffer_input_node_id_by_inst.find(output_node.inst_name); indexed != context.buffer_input_node_id_by_inst.end()) {
    if (indexed->second < context.nodes.size()) {
      const auto& input_node = context.nodes.at(indexed->second);
      if (input_node.kind == FastStaNodeKind::kBufferInput && input_node.inst_name == output_node.inst_name) {
        return indexed->second;
      }
    }
  }
  return kInvalidFastStaNodeId;
}

auto propagateBufferOutput(FastStaContext& context, FastStaNodeId output_node_id, FastStaNet& net) -> void
{
  if (output_node_id >= context.nodes.size()) {
    return;
  }
  auto& output_node = context.nodes.at(output_node_id);
  if (output_node.kind != FastStaNodeKind::kBufferOutput) {
    return;
  }

  const auto input_node_id = findBufferInputNode(context, output_node);
  if (input_node_id == kInvalidFastStaNodeId || input_node_id >= context.nodes.size()) {
    return;
  }
  const auto& input_node = context.nodes.at(input_node_id);

  const auto liberty_iter = context.liberty_cell_by_master.find(output_node.cell_master);
  if (liberty_iter == context.liberty_cell_by_master.end()) {
    output_node.timing = {};
    return;
  }

  const auto& liberty_cell = liberty_iter->second;
  if (input_node.clock_inactive
      || (liberty_cell.clock_gate.has_value()
          && FastStaConstraints::gateActive(context, output_node.inst_name, *liberty_cell.clock_gate) == FastStaLogicValue::kZero)) {
    output_node.clock_inactive = true;
    output_node.timing = {};
    output_node.early_timing = {};
    output_node.late_timing = {};
    return;
  }
  if (liberty_cell.timing_arc.delay_tables.empty() || liberty_cell.timing_arc.slew_tables.empty()) {
    output_node.timing = {};
    return;
  }
  std::vector<std::pair<std::size_t, const FastStaLibertyArc*>> clock_arcs;
  if (liberty_cell.timing_arcs.empty()) {
    clock_arcs.emplace_back(0U, &liberty_cell.timing_arc);
  } else {
    const auto selection = SelectArcIndexes(liberty_cell, [&](const auto& arc) -> bool {
      const auto& from = output_node.clock_from_port.empty() ? liberty_cell.input_port : output_node.clock_from_port;
      const auto& to = output_node.clock_to_port.empty() ? liberty_cell.output_port : output_node.clock_to_port;
      return arc.edge_triggered == output_node.clock_edge_triggered && arc.from_port == from && arc.to_port == to
             && (!arc.edge_triggered || arc.trigger_transition == output_node.clock_trigger_transition)
             && FastStaCondition::evaluate(arc.when, FastStaConstraints::caseLookup(context, output_node.inst_name)) != FastStaLogicValue::kZero;
    });
    for (const auto arc_index : selection.indexes) {
      clock_arcs.emplace_back(arc_index, &liberty_cell.timing_arcs.at(arc_index));
    }
  }
  if (clock_arcs.empty()) {
    output_node.timing = {};
    return;
  }
  output_node.early_timing = {};
  output_node.late_timing = {};
  net.driver_timing_by_state.assign(2U, {});
  for (const auto& [arc_index, arc_ptr] : clock_arcs) {
    const auto& arc = *arc_ptr;
    for (const auto input_transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      if (arc.edge_triggered && input_transition != arc.trigger_transition) {
        continue;
      }
      const auto input_index = input_transition == FastStaTransition::kRise ? 0U : 1U;
      std::array<FastStaTransition, 2U> output_transitions{input_transition, input_transition};
      auto output_count = 1U;
      if (arc.edge_triggered || (!arc.negative_unate && !arc.positive_unate)) {
        output_transitions = {FastStaTransition::kRise, FastStaTransition::kFall};
        output_count = 2U;
      } else if (arc.negative_unate) {
        output_transitions.front() = input_transition == FastStaTransition::kRise ? FastStaTransition::kFall : FastStaTransition::kRise;
      }
      for (std::size_t output_index = 0U; output_index < output_count; ++output_index) {
        const auto output_transition = output_transitions.at(output_index);
        const auto transition_index = output_transition == FastStaTransition::kRise ? 0U : 1U;
        for (const auto early : {true, false}) {
          const auto analysis_index = early ? 0U : 1U;
          const auto& input_timing = early ? input_node.early_timing.at(input_index) : input_node.late_timing.at(input_index);
          if (!input_timing.valid) {
            continue;
          }
          const auto& pi = net.parasitic.driver_pi_by_timing.at(analysis_index).at(transition_index);
          auto driver = FastStaDmpCeff::calcDriverTiming(liberty_cell, arc, pi, output_transition, input_timing.slew_ns);
          if (!driver.valid) {
            continue;
          }
          auto candidate = input_timing;
          if (!output_node.clock_name.empty() && output_node.clock_name != input_timing.clock_name) {
            candidate.clock_name = output_node.clock_name;
            candidate.launch_clock_transition = output_transition;
            candidate.launch_node_id = output_node_id;
            candidate.launch_clock_node_id = output_node_id;
            FastStaEvents::startPath(context, candidate, output_transition);
          }
          candidate.arrival_ns += driver.gate_delay_ns;
          candidate.slew_ns = driver.driver_slew_ns;
          candidate.driver_arc_variant_index = arc_index;
          candidate.driver_input_slew_ns = input_timing.slew_ns;
          candidate.stage_input_node_id = input_node_id;
          candidate.stage_input_transition = input_transition;
          candidate.stage_input_arrival_ns = input_timing.arrival_ns;
          candidate.stage_input_slew_ns = input_timing.slew_ns;
          candidate.stage_delay_ns = driver.gate_delay_ns;
          auto& target = early ? output_node.early_timing.at(transition_index) : output_node.late_timing.at(transition_index);
          if (!target.valid || (early ? candidate.arrival_ns < target.arrival_ns : candidate.arrival_ns > target.arrival_ns)) {
            target = candidate;
            net.driver_timing_by_state.at(analysis_index).at(transition_index) = driver;
          }
        }
      }
    }
  }
  output_node.timing = output_node.late_timing.at(0U).valid ? output_node.late_timing.at(0U) : output_node.early_timing.at(0U);
}

auto propagateNetLoads(FastStaContext& context, FastStaNet& net, std::vector<FastStaNodeId>& ready_nodes, bool clock_only) -> void
{
  if (net.driver_node_id >= context.nodes.size()) {
    return;
  }

  const auto& driver_node = context.nodes.at(net.driver_node_id);
  for (std::size_t load_index = 0U; load_index < net.load_node_ids.size(); ++load_index) {
    const auto load_node_id = net.load_node_ids.at(load_index);
    if (load_node_id >= context.nodes.size()) {
      continue;
    }
    if (std::ranges::find(context.clock_source_node_ids, load_node_id) != context.clock_source_node_ids.end()) {
      continue;
    }
    if (clock_only && context.nodes.at(load_node_id).domain != FastStaNodeDomain::kClock) {
      continue;
    }
    const auto* load_cell = FindLoadLibertyCell(context, load_node_id);
    auto& load_node = context.nodes.at(load_node_id);
    load_node.clock_inactive = driver_node.clock_inactive;
    if (load_node.clock_inactive) {
      ready_nodes.push_back(load_node_id);
      continue;
    }
    load_node.early_timing = {};
    load_node.late_timing = {};
    for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      const auto transition_index = transition == FastStaTransition::kRise ? 0U : 1U;
      for (const auto early : {true, false}) {
        const auto analysis_index = early ? 0U : 1U;
        const auto& driver_timing = early ? driver_node.early_timing.at(transition_index) : driver_node.late_timing.at(transition_index);
        if (!driver_timing.valid) {
          continue;
        }
        auto load_timing = FastStaDmpLoadResult{.valid = true, .wire_delay_ns = 0.0, .load_slew_ns = driver_timing.slew_ns};
        if (!net.parasitic.rc_nodes.empty()) {
          const auto rc_node_id = load_index < net.load_rc_node_ids.size() ? net.load_rc_node_ids.at(load_index) : kInvalidFastStaRcNodeId;
          const auto elmore_delay_ns = FindRcTerminalElmore(net.parasitic, rc_node_id, analysis_index, transition_index);
          if (driver_node.kind == FastStaNodeKind::kSource) {
            load_timing = FastStaDmpCeff::calcInputPortDelaySlew(driver_timing.slew_ns, elmore_delay_ns, transition, load_cell);
          } else if (net.driver_timing_by_state.size() == 2U) {
            const auto& driver = net.driver_timing_by_state.at(analysis_index).at(transition_index);
            load_timing = driver.valid ? FastStaDmpCeff::calcLoadDelaySlew(driver, elmore_delay_ns, load_cell) : FastStaDmpLoadResult{};
          } else {
            load_timing = {};
          }
        }
        if (!load_timing.valid) {
          continue;
        }
        auto timing = driver_timing;
        timing.arrival_ns += load_timing.wire_delay_ns;
        timing.slew_ns = std::max(0.0, load_timing.load_slew_ns);
        timing.stage_input_node_id = net.driver_node_id;
        timing.stage_input_transition = transition;
        timing.stage_input_arrival_ns = driver_timing.arrival_ns;
        timing.stage_input_slew_ns = driver_timing.slew_ns;
        timing.stage_delay_ns = load_timing.wire_delay_ns;
        auto& target = early ? load_node.early_timing.at(transition_index) : load_node.late_timing.at(transition_index);
        target = timing;
      }
    }
    load_node.timing = load_node.late_timing.at(0U).valid ? load_node.late_timing.at(0U) : load_node.early_timing.at(0U);
    if (load_node.timing.valid) {
      ready_nodes.push_back(load_node_id);
    }
  }
}

}  // namespace

auto ResetTiming(FastStaContext& context) -> void
{
  context.logic_tags_valid = false;
  for (auto& node : context.nodes) {
    node.timing = FastStaTimingPoint{};
    node.early_timing = {};
    node.late_timing = {};
    node.tagged_early_timing = {};
    node.tagged_late_timing = {};
  }
  for (auto& net : context.nets) {
    net.driver_timing_by_state.clear();
  }
}

auto ResetTiming(FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> void
{
  for (const auto node_id : dirty_region.node_ids) {
    if (node_id < context.nodes.size()) {
      context.nodes.at(node_id).timing = FastStaTimingPoint{};
      context.nodes.at(node_id).early_timing = {};
      context.nodes.at(node_id).late_timing = {};
    }
  }
  for (const auto net_id : dirty_region.net_ids) {
    if (net_id < context.nets.size()) {
      context.nets.at(net_id).driver_timing_by_state.clear();
    }
  }
}

auto PrepareRegionalClockTiming(FastStaContext& context, const FastStaDirtyRegion& dirty_region, std::queue<FastStaNodeId>& ready) -> bool
{
  struct StartTiming
  {
    FastStaNodeId id;
    FastStaTimingPoint timing;
    std::array<FastStaTimingPoint, 2U> early;
    std::array<FastStaTimingPoint, 2U> late;
  };
  std::vector<StartTiming> starts;
  const auto save = [&](FastStaNodeId id) -> bool {
    if (id >= context.nodes.size() || (!context.nodes.at(id).timing.valid && !context.nodes.at(id).clock_inactive)) {
      return false;
    }
    const auto& node = context.nodes.at(id);
    starts.push_back({id, node.timing, node.early_timing, node.late_timing});
    return true;
  };
  if (dirty_region.start_node_ids.empty()) {
    if (!save(dirty_region.start_node_id)) {
      return false;
    }
  } else {
    for (const auto id : dirty_region.start_node_ids) {
      if (!save(id)) {
        return false;
      }
    }
  }
  // A dirty physical region may cross an independent primary source. Its
  // incoming edge is cut, so retain that source's seed and schedule its own
  // downstream propagation after resetting the region.
  for (const auto source_id : context.clock_source_node_ids) {
    if (std::ranges::any_of(starts, [&](const auto& start) -> bool { return start.id == source_id; })) {
      continue;
    }
    if (std::ranges::find(dirty_region.node_ids, source_id) != dirty_region.node_ids.end() && !save(source_id)) {
      return false;
    }
  }
  ResetTiming(context, dirty_region);
  for (auto& start : starts) {
    auto& node = context.nodes.at(start.id);
    node.timing = std::move(start.timing);
    node.early_timing = std::move(start.early);
    node.late_timing = std::move(start.late);
    ready.push(start.id);
  }
  return true;
}

auto HasCompleteSinkTiming(const FastStaContext& context) -> bool
{
  const auto check_sink = [](const FastStaNode& node) -> bool {
    return node.domain != FastStaNodeDomain::kClock || node.kind != FastStaNodeKind::kSink || node.timing.valid || node.clock_inactive;
  };
  if (context.owner_clock_scope_available) {
    for (const auto node_id : context.owned_clock_sink_node_ids) {
      if (node_id >= context.nodes.size() || !check_sink(context.nodes.at(node_id))) {
        return false;
      }
    }
  } else if (!std::ranges::all_of(context.nodes, check_sink)) {
    return false;
  }
  // No eligible endpoint is a complete empty analysis, not a missing model.
  // Any active physical clock node must nevertheless have propagated timing.
  const auto check_clock_node = [](const FastStaNode& node) -> bool {
    const auto valid
        = [](const auto& point) -> bool { return point.valid && std::isfinite(point.arrival_ns) && std::isfinite(point.slew_ns) && point.slew_ns >= 0.0; };
    return node.domain != FastStaNodeDomain::kClock || node.clock_inactive
           || (valid(node.timing) && std::ranges::all_of(node.early_timing, valid) && std::ranges::all_of(node.late_timing, valid));
  };
  return std::ranges::all_of(context.nodes, check_clock_node);
}

auto HasTimingPropagationState(const FastStaContext& context) -> bool
{
  if (!context.timing_arcs.empty() || !context.timing_launches.empty() || !context.timing_checks.empty()) {
    return true;
  }
  for (const auto& node : context.nodes) {
    if (node.domain == FastStaNodeDomain::kLogic) {
      return true;
    }
  }
  for (const auto& net : context.nets) {
    if (net.domain == FastStaNetDomain::kLogic) {
      return true;
    }
  }
  return false;
}

namespace {

auto applyClockView(const FastStaContext& context, FastStaNodeId node_id, FastStaNode& node) -> void
{
  if (node.domain != FastStaNodeDomain::kClock || node.clock_inactive || FastStaConstraints::isPropagated(context, node.clock_name)) {
    return;
  }
  for (const auto early : {true, false}) {
    for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      const auto index = transition == FastStaTransition::kRise ? 0U : 1U;
      auto& point = early ? node.early_timing.at(index) : node.late_timing.at(index);
      if (!point.valid) {
        continue;
      }
      const auto source_id = point.launch_clock_node_id;
      point.arrival_ns = FastStaConstraints::latency(context, source_id, point.clock_name, point.launch_clock_transition, early, true)
                         + FastStaConstraints::latency(context, node_id, point.clock_name, point.launch_clock_transition, early, false);
      point.slew_ns = context.root_input_slew_ns;
      for (const auto& constraint : context.constraints.clock_transitions) {
        if ((early ? constraint.min : constraint.max) && FastStaConstraints::transitionMatches(constraint.transition, point.launch_clock_transition)
            && std::ranges::any_of(constraint.clocks,
                                   [&](const auto& object) -> bool { return FastStaConstraints::matches(context, object, node_id, point.clock_name); })) {
          point.slew_ns = constraint.value_ns;
        }
      }
    }
  }
  node.timing = node.late_timing.front();
}

}  // namespace

auto PropagateReadyQueue(FastStaContext& context, std::queue<FastStaNodeId>& ready_nodes, bool clock_only) -> void
{
  const auto find_buffer_output = [&](const FastStaNode& input_node) -> FastStaNodeId {
    if (const auto indexed = context.buffer_output_node_id_by_inst.find(input_node.inst_name); indexed != context.buffer_output_node_id_by_inst.end()) {
      if (std::ranges::find(context.clock_source_node_ids, indexed->second) != context.clock_source_node_ids.end()) {
        return kInvalidFastStaNodeId;
      }
      if (indexed->second < context.nodes.size()) {
        const auto& output_node = context.nodes.at(indexed->second);
        if (output_node.kind == FastStaNodeKind::kBufferOutput && output_node.inst_name == input_node.inst_name) {
          return indexed->second;
        }
      }
    }
    return kInvalidFastStaNodeId;
  };
  if (clock_only) {
    std::vector<FastStaNodeId> pending;
    while (!ready_nodes.empty()) {
      pending.push_back(ready_nodes.front());
      ready_nodes.pop();
    }
    for (std::size_t index = 0U; index < pending.size(); ++index) {
      const auto node_id = pending.at(index);
      if (node_id >= context.nodes.size()) {
        continue;
      }
      auto& node = context.nodes.at(node_id);
      if (node.domain != FastStaNodeDomain::kClock) {
        continue;
      }
      applyClockView(context, node_id, node);
      if (node.kind == FastStaNodeKind::kBufferInput) {
        const auto output_node_id = find_buffer_output(node);
        if (output_node_id < context.nodes.size()) {
          auto& output_node = context.nodes.at(output_node_id);
          if (!output_node.output_net_ids.empty() && output_node.output_net_ids.front() < context.nets.size()) {
            propagateBufferOutput(context, output_node_id, context.nets.at(output_node.output_net_ids.front()));
          } else {
            output_node.timing = node.timing;
            output_node.early_timing = node.early_timing;
            output_node.late_timing = node.late_timing;
          }
          if (output_node.timing.valid || output_node.clock_inactive) {
            pending.push_back(output_node_id);
          }
        }
        continue;
      }
      for (const auto net_id : node.output_net_ids) {
        if (net_id < context.nets.size() && context.nets.at(net_id).domain == FastStaNetDomain::kClock) {
          propagateNetLoads(context, context.nets.at(net_id), pending, true);
        }
      }
    }
    return;
  }
  std::vector<FastStaNodeId> current_nodes;
  while (!ready_nodes.empty()) {
    current_nodes.push_back(ready_nodes.front());
    ready_nodes.pop();
  }
  // prepare() has proved the active clock graph acyclic; no empirical cutoff.
  while (!current_nodes.empty()) {
    std::vector<std::vector<FastStaNodeId>> next_nodes(current_nodes.size());
#pragma omp parallel for schedule(static) num_threads(static_cast<int>(context.worker_count)) if (current_nodes.size() > 1U && context.worker_count > 1U)
    for (auto index = std::ptrdiff_t{0}; index < std::ssize(current_nodes); ++index) {
      const auto node_id = current_nodes.at(static_cast<std::size_t>(index));
      auto& node_ready = next_nodes.at(static_cast<std::size_t>(index));
      if (node_id >= context.nodes.size()) {
        continue;
      }
      auto& node = context.nodes.at(node_id);
      if (clock_only && node.domain != FastStaNodeDomain::kClock) {
        continue;
      }
      applyClockView(context, node_id, node);
      if (node.kind == FastStaNodeKind::kBufferInput) {
        const auto output_node_id = find_buffer_output(node);
        if (output_node_id < context.nodes.size()) {
          auto& output_node = context.nodes.at(output_node_id);
          if (!output_node.output_net_ids.empty() && output_node.output_net_ids.front() < context.nets.size()) {
            propagateBufferOutput(context, output_node_id, context.nets.at(output_node.output_net_ids.front()));
          } else {
            output_node.timing = node.timing;
            output_node.early_timing = node.early_timing;
            output_node.late_timing = node.late_timing;
          }
          if (output_node.timing.valid || output_node.clock_inactive) {
            node_ready.push_back(output_node_id);
          }
        }
        continue;
      }
      for (auto net_id : node.output_net_ids) {
        if (net_id < context.nets.size() && (!clock_only || context.nets.at(net_id).domain == FastStaNetDomain::kClock)) {
          propagateNetLoads(context, context.nets.at(net_id), node_ready, clock_only);
        }
      }
    }
    current_nodes.clear();
    for (auto& node_ready : next_nodes) {
      current_nodes.insert(current_nodes.end(), std::make_move_iterator(node_ready.begin()), std::make_move_iterator(node_ready.end()));
    }
  }
}

auto SeedClockSources(FastStaContext& context) -> void
{
  for (const auto source_id : context.clock_source_node_ids) {
    auto& source = context.nodes.at(source_id);
    source.timing = FastStaTimingPoint{.arrival_ns = 0.0, .slew_ns = std::max(0.0, context.root_input_slew_ns), .valid = true, .exception_progress = {}};
    for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      for (const auto early : {true, false}) {
        auto& point = early ? source.early_timing.at(TransitionIndex(transition)) : source.late_timing.at(TransitionIndex(transition));
        point = source.timing;
        point.clock_name = source.clock_name;
        point.launch_node_id = source_id;
        point.launch_clock_node_id = source_id;
        point.launch_clock_transition = transition;
        point.arrival_ns = FastStaConstraints::latency(context, source_id, source.clock_name, transition, early, true)
                           + FastStaConstraints::latency(context, source_id, source.clock_name, transition, early, false);
        for (const auto& slew : context.constraints.clock_transitions) {
          if ((early ? slew.min : slew.max) && FastStaConstraints::transitionMatches(slew.transition, transition)
              && std::ranges::any_of(slew.clocks,
                                     [&](const auto& clock) -> bool { return FastStaConstraints::matches(context, clock, source_id, source.clock_name); })) {
            point.slew_ns = slew.value_ns;
          }
        }
        for (const auto& slew : context.constraints.input_transitions) {
          if ((early ? slew.min : slew.max) && FastStaConstraints::transitionMatches(slew.transition, transition)
              && std::ranges::any_of(slew.objects, [&](const auto& object) -> bool { return FastStaConstraints::matches(context, object, source_id); })) {
            point.slew_ns = slew.value_ns;
          }
        }
        FastStaEvents::startPath(context, point, transition);
      }
    }
    source.timing = source.late_timing.front();
  }
}

}  // namespace fast_sta

auto FastStaTiming::calcSkew(const FastStaContext& context) -> FastStaSkewSummary
{
  FastStaSkewSummary summary;
  summary.min_arrival_ns = std::numeric_limits<double>::infinity();
  summary.max_arrival_ns = -std::numeric_limits<double>::infinity();
  const auto consider = [&](FastStaNodeId node_id) -> void {
    if (node_id >= context.nodes.size()) {
      return;
    }
    const auto& node = context.nodes.at(node_id);
    if (node.domain != FastStaNodeDomain::kClock || node.kind != FastStaNodeKind::kSink || !node.timing.valid) {
      return;
    }
    if (node.timing.arrival_ns < summary.min_arrival_ns) {
      summary.min_arrival_ns = node.timing.arrival_ns;
      summary.min_sink_node_id = node_id;
      summary.min_sink_name = node.name;
    }
    if (node.timing.arrival_ns > summary.max_arrival_ns) {
      summary.max_arrival_ns = node.timing.arrival_ns;
      summary.max_sink_node_id = node_id;
      summary.max_sink_name = node.name;
    }
  };
  if (context.owner_clock_scope_available) {
    for (const auto node_id : context.owned_clock_sink_node_ids) {
      consider(node_id);
    }
  } else {
    for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
      consider(node_id);
    }
  }
  summary.valid = summary.min_sink_node_id != kInvalidFastStaNodeId && summary.max_sink_node_id != kInvalidFastStaNodeId;
  if (summary.valid) {
    summary.skew_ns = std::max(0.0, summary.max_arrival_ns - summary.min_arrival_ns);
  } else {
    summary.min_arrival_ns = 0.0;
    summary.max_arrival_ns = 0.0;
  }
  return summary;
}

}  // namespace icts
