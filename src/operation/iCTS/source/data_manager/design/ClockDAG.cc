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
 * @file ClockDAG.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-08-14
 * @brief Typed validation and read-only projection of explicit clock membership.
 */

#include "data_manager/design/ClockDAG.hh"

#include <algorithm>
#include <deque>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "data_manager/design/Clock.hh"
#include "data_manager/design/Design.hh"
#include "data_manager/design/Inst.hh"
#include "data_manager/design/Net.hh"
#include "data_manager/design/Pin.hh"

namespace icts {
namespace {

constexpr std::size_t kMaxClockGraphIssues = 64U;

auto PinName(const Pin* pin) -> std::string
{
  return Design::getPinFullName(pin);
}

auto NetName(const Net* net) -> std::string
{
  return net == nullptr ? std::string{} : net->get_name();
}

auto InstName(const Inst* inst) -> std::string
{
  return inst == nullptr ? std::string{} : inst->get_name();
}

auto CellMaster(const Inst* inst) -> std::string
{
  return inst == nullptr ? std::string{} : inst->get_cell_master();
}

auto IsInputDirection(PinType type) -> bool
{
  return type == PinType::kIn || type == PinType::kClock;
}

auto IsOutputDirection(PinType type) -> bool
{
  return type == PinType::kOut;
}

auto PinDirectionName(PinType type) -> const char*
{
  switch (type) {
    case PinType::kClock:
      return "clock";
    case PinType::kIn:
      return "input";
    case PinType::kOut:
      return "output";
    case PinType::kInOut:
      return "inout";
    case PinType::kOther:
      return "other";
  }
  return "other";
}

auto PropagationKindName(ClockPropagationKind kind) -> const char*
{
  return kind == ClockPropagationKind::kBuffer ? "buffer" : "inverter";
}

auto PropagationOriginName(ClockPropagationOrigin origin) -> const char*
{
  return origin == ClockPropagationOrigin::kTracedInput ? "traced_input" : "synthesized";
}

auto InstTypeName(InstType type) -> const char*
{
  switch (type) {
    case InstType::kBuffer:
      return "buffer";
    case InstType::kFlipFlop:
      return "flip_flop";
    case InstType::kLatch:
      return "latch";
    case InstType::kInverter:
      return "inverter";
    case InstType::kClockGate:
      return "clock_gate";
    case InstType::kMux:
      return "mux";
    case InstType::kClockLogic:
      return "clock_logic";
    case InstType::kBoundaryLoad:
      return "boundary_load";
    case InstType::kMacroBlock:
      return "macro_block";
    case InstType::kUnknown:
      return "unknown";
  }
  return "unknown";
}

auto ClockLess(const Clock* lhs, const Clock* rhs) -> bool
{
  if (lhs == nullptr || rhs == nullptr) {
    return rhs != nullptr;
  }
  if (lhs->get_clock_name() != rhs->get_clock_name()) {
    return lhs->get_clock_name() < rhs->get_clock_name();
  }
  return lhs->get_clock_net_name() < rhs->get_clock_net_name();
}

auto NetLess(const Net* lhs, const Net* rhs) -> bool
{
  if (lhs == nullptr || rhs == nullptr) {
    return rhs != nullptr;
  }
  return lhs->get_name() < rhs->get_name();
}

auto ArcLess(const ClockPropagationArc* lhs, const ClockPropagationArc* rhs) -> bool
{
  if (lhs == nullptr || rhs == nullptr) {
    return rhs != nullptr;
  }
  if (InstName(lhs->inst) != InstName(rhs->inst)) {
    return InstName(lhs->inst) < InstName(rhs->inst);
  }
  if (PinName(lhs->input_pin) != PinName(rhs->input_pin)) {
    return PinName(lhs->input_pin) < PinName(rhs->input_pin);
  }
  return PinName(lhs->output_pin) < PinName(rhs->output_pin);
}

auto AppendPin(ClockDAG::ClockGraph& graph, Pin* pin) -> void
{
  if (pin == nullptr || !graph.pin_set.insert(pin).second) {
    return;
  }
  graph.pins.push_back(pin);
}

auto AppendNet(ClockDAG::ClockGraph& graph, Net* net) -> void
{
  if (net == nullptr || !graph.net_set.insert(net).second) {
    return;
  }
  graph.nets.push_back(net);
}

auto AppendArc(ClockDAG::ClockGraph& graph, Pin* from, Pin* to, Net* net, int32_t path_buffer_weight) -> void
{
  if (from == nullptr || to == nullptr) {
    return;
  }
  AppendPin(graph, from);
  AppendPin(graph, to);
  graph.outgoing_arcs[from].push_back(ClockDAG::Arc{
      .from = from,
      .to = to,
      .net = net,
      .path_buffer_weight = path_buffer_weight,
  });
}

auto MakeIssue(const Clock* clock, ClockGraphIssueCode code, std::string message) -> ClockGraphIssue
{
  return ClockGraphIssue{
      .code = code,
      .clock_name = clock == nullptr ? std::string{} : clock->get_clock_name(),
      .clock_net_name = clock == nullptr ? std::string{} : clock->get_clock_net_name(),
      .object_name = {},
      .inst_name = {},
      .cell_master = {},
      .input_pin_name = {},
      .output_pin_name = {},
      .input_net_name = {},
      .output_net_name = {},
      .propagation_kind = {},
      .propagation_origin = {},
      .expected = {},
      .observed = {},
      .invariant = ClockGraphIssueCodeName(code),
      .message = std::move(message),
  };
}

auto MakePropagationIssueBase(const Clock* clock, const ClockPropagationArc* arc) -> ClockGraphIssue
{
  auto issue = MakeIssue(clock, ClockGraphIssueCode::kIncompletePropagationArc, {});
  if (arc == nullptr) {
    return issue;
  }
  issue.inst_name = InstName(arc->inst);
  issue.cell_master = CellMaster(arc->inst);
  issue.input_pin_name = PinName(arc->input_pin);
  issue.output_pin_name = PinName(arc->output_pin);
  issue.input_net_name = NetName(arc->input_pin == nullptr ? nullptr : arc->input_pin->get_net());
  issue.output_net_name = NetName(arc->output_pin == nullptr ? nullptr : arc->output_pin->get_net());
  issue.propagation_kind = PropagationKindName(arc->kind);
  issue.propagation_origin = PropagationOriginName(arc->origin);
  return issue;
}

auto SetIssueCode(ClockGraphIssue& issue, ClockGraphIssueCode code) -> void
{
  issue.code = code;
  issue.invariant = ClockGraphIssueCodeName(code);
}

auto FinishTopologicalOrder(ClockDAG::ClockGraph& graph) -> bool
{
  std::unordered_map<const Pin*, std::size_t> in_degree;
  in_degree.reserve(graph.pins.size());
  for (auto* pin : graph.pins) {
    in_degree[pin] = 0U;
  }
  for (const auto& [unused_pin, arcs] : graph.outgoing_arcs) {
    (void) unused_pin;
    for (const auto& arc : arcs) {
      ++in_degree[arc.to];
    }
  }

  struct ReadyPin
  {
    std::string name;
    std::size_t ordinal = 0U;
    Pin* pin = nullptr;
  };
  const auto ready_greater = [](const ReadyPin& lhs, const ReadyPin& rhs) -> bool {
    if (lhs.name != rhs.name) {
      return lhs.name > rhs.name;
    }
    return lhs.ordinal > rhs.ordinal;
  };
  std::unordered_map<const Pin*, std::size_t> pin_ordinals;
  pin_ordinals.reserve(graph.pins.size());
  for (std::size_t ordinal = 0U; ordinal < graph.pins.size(); ++ordinal) {
    pin_ordinals.emplace(graph.pins.at(ordinal), ordinal);
  }
  for (auto& [unused_pin, arcs] : graph.outgoing_arcs) {
    (void) unused_pin;
    std::ranges::sort(arcs, [&pin_ordinals](const ClockDAG::Arc& lhs, const ClockDAG::Arc& rhs) -> bool {
      const auto lhs_name = PinName(lhs.to);
      const auto rhs_name = PinName(rhs.to);
      return lhs_name != rhs_name ? lhs_name < rhs_name : pin_ordinals.at(lhs.to) < pin_ordinals.at(rhs.to);
    });
  }

  std::priority_queue<ReadyPin, std::vector<ReadyPin>, decltype(ready_greater)> ready(ready_greater);
  for (auto* pin : graph.pins) {
    if (in_degree[pin] == 0U) {
      ready.push(ReadyPin{.name = PinName(pin), .ordinal = pin_ordinals.at(pin), .pin = pin});
      ++graph.build_work.ready_push_count;
    }
  }

  graph.topological_pins.clear();
  graph.topological_pins.reserve(graph.pins.size());
  while (!ready.empty()) {
    auto* pin = ready.top().pin;
    ready.pop();
    ++graph.build_work.ready_pop_count;
    graph.topological_pins.push_back(pin);
    const auto arc_iter = graph.outgoing_arcs.find(pin);
    if (arc_iter == graph.outgoing_arcs.end()) {
      continue;
    }
    for (const auto& arc : arc_iter->second) {
      ++graph.build_work.arc_relaxation_count;
      auto degree_iter = in_degree.find(arc.to);
      if (degree_iter == in_degree.end() || degree_iter->second == 0U) {
        continue;
      }
      --degree_iter->second;
      if (degree_iter->second == 0U) {
        ready.push(ReadyPin{.name = PinName(arc.to), .ordinal = pin_ordinals.at(arc.to), .pin = arc.to});
        ++graph.build_work.ready_push_count;
      }
    }
  }
  graph.has_cycle = graph.topological_pins.size() != graph.pins.size();
  if (graph.has_cycle) {
    graph.topological_pins.clear();
  }
  return !graph.has_cycle;
}

auto ReachablePins(const ClockDAG::ClockGraph& graph, Pin* start_pin) -> std::unordered_set<const Pin*>
{
  std::unordered_set<const Pin*> reachable;
  if (start_pin == nullptr || !graph.pin_set.contains(start_pin)) {
    return reachable;
  }
  std::deque<Pin*> pending = {start_pin};
  while (!pending.empty()) {
    auto* pin = pending.front();
    pending.pop_front();
    if (pin == nullptr || !reachable.insert(pin).second) {
      continue;
    }
    const auto arc_iter = graph.outgoing_arcs.find(pin);
    if (arc_iter == graph.outgoing_arcs.end()) {
      continue;
    }
    for (const auto& arc : arc_iter->second) {
      pending.push_back(arc.to);
    }
  }
  return reachable;
}

auto IsFlipFlopSinkTerminal(const Pin* pin) -> bool
{
  return pin != nullptr && pin->get_inst() != nullptr && pin->get_inst()->is_sequential_sink();
}

auto CountDeclaredFlipFlopSinkTerminals(const Clock& clock) -> std::size_t
{
  return static_cast<std::size_t>(std::ranges::count_if(clock.get_loads(), [](const Pin* pin) -> bool { return IsFlipFlopSinkTerminal(pin); }));
}

auto StatusForUnavailableClockStats(const Clock& clock, std::size_t reachable_ff_sink_count) -> std::string
{
  if (reachable_ff_sink_count > 0U) {
    return "available";
  }
  return CountDeclaredFlipFlopSinkTerminals(clock) > 0U ? "no_reachable_ff_sink_terminal" : "no_ff_sink_terminal";
}

}  // namespace

auto ClockGraphIssueCodeName(ClockGraphIssueCode code) -> const char*
{
  switch (code) {
    case ClockGraphIssueCode::kNullClock:
      return "null_clock";
    case ClockGraphIssueCode::kMissingClockSource:
      return "missing_clock_source";
    case ClockGraphIssueCode::kMissingNet:
      return "missing_net";
    case ClockGraphIssueCode::kMissingNetDriver:
      return "missing_net_driver";
    case ClockGraphIssueCode::kMissingNetLoad:
      return "missing_net_load";
    case ClockGraphIssueCode::kDuplicateNetMembership:
      return "duplicate_net_membership";
    case ClockGraphIssueCode::kForeignNetMembership:
      return "foreign_net_membership";
    case ClockGraphIssueCode::kDuplicatePinMembership:
      return "duplicate_pin_membership";
    case ClockGraphIssueCode::kForeignPinMembership:
      return "foreign_pin_membership";
    case ClockGraphIssueCode::kIncompletePropagationArc:
      return "incomplete_propagation_arc";
    case ClockGraphIssueCode::kPropagationPinInstMismatch:
      return "propagation_pin_inst_mismatch";
    case ClockGraphIssueCode::kInvalidPinDirection:
      return "invalid_pin_direction";
    case ClockGraphIssueCode::kPhysicalKindMismatch:
      return "physical_kind_mismatch";
    case ClockGraphIssueCode::kAmbiguousCrossClockOwnership:
      return "ambiguous_cross_clock_ownership";
    case ClockGraphIssueCode::kUnreachableDeclaredSink:
      return "unreachable_declared_sink";
    case ClockGraphIssueCode::kGraphCycle:
      return "graph_cycle";
  }
  return "unknown_clock_graph_issue";
}

auto FormatClockGraphIssue(const ClockGraphIssue& issue) -> std::string
{
  std::ostringstream stream;
  stream << ClockGraphIssueCodeName(issue.code) << "{clock=" << (issue.clock_name.empty() ? "n/a" : issue.clock_name)
         << ",clock_net=" << (issue.clock_net_name.empty() ? "n/a" : issue.clock_net_name);
  if (!issue.object_name.empty()) {
    stream << ",object=" << issue.object_name;
  }
  if (!issue.inst_name.empty()) {
    stream << ",inst=" << issue.inst_name;
  }
  if (!issue.cell_master.empty()) {
    stream << ",cell_master=" << issue.cell_master;
  }
  if (!issue.input_pin_name.empty()) {
    stream << ",input_pin=" << issue.input_pin_name;
  }
  if (!issue.output_pin_name.empty()) {
    stream << ",output_pin=" << issue.output_pin_name;
  }
  if (!issue.input_net_name.empty()) {
    stream << ",input_net=" << issue.input_net_name;
  }
  if (!issue.output_net_name.empty()) {
    stream << ",output_net=" << issue.output_net_name;
  }
  if (!issue.propagation_kind.empty()) {
    stream << ",propagation_kind=" << issue.propagation_kind;
  }
  if (!issue.propagation_origin.empty()) {
    stream << ",propagation_origin=" << issue.propagation_origin;
  }
  if (!issue.expected.empty()) {
    stream << ",expected=" << issue.expected;
  }
  if (!issue.observed.empty()) {
    stream << ",observed=" << issue.observed;
  }
  if (!issue.invariant.empty()) {
    stream << ",invariant=" << issue.invariant;
  }
  if (!issue.message.empty()) {
    stream << ",detail=" << issue.message;
  }
  stream << "}";
  return stream.str();
}

auto ClockDAG::rebuild(const std::vector<Clock*>& clocks) -> bool
{
  clear();
  _built = true;
  _valid = true;
  _status = clocks.empty() ? "empty" : "valid";

  auto ordered_clocks = clocks;
  std::ranges::sort(ordered_clocks, ClockLess);
  _clock_order.reserve(ordered_clocks.size());

  std::unordered_map<const Inst*, std::unordered_set<const Clock*>> propagation_owners;
  for (const auto* clock : ordered_clocks) {
    if (clock == nullptr) {
      continue;
    }
    for (const auto& arc : clock->get_propagation_arcs()) {
      if (arc.inst != nullptr) {
        propagation_owners[arc.inst].insert(clock);
      }
    }
  }

  const auto add_issue = [this](ClockGraph& graph, ClockGraphIssue issue) -> void {
    graph.valid = false;
    graph.status = graph.issues.empty() ? FormatClockGraphIssue(issue) : graph.status;
    if (graph.issues.size() < kMaxClockGraphIssues) {
      graph.issues.push_back(issue);
    }
    if (_issues.size() < kMaxClockGraphIssues) {
      _issues.push_back(std::move(issue));
    }
  };

  for (auto* clock : ordered_clocks) {
    if (clock == nullptr) {
      ClockGraph graph;
      add_issue(graph, MakeIssue(nullptr, ClockGraphIssueCode::kNullClock, "clock pointer is null"));
      _valid = false;
      continue;
    }

    ClockGraph graph;
    graph.clock = clock;
    _clock_order.push_back(clock);

    auto* source_pin = clock->get_clock_source();
    auto* source_net = clock->get_clock_source_net();
    if (source_pin == nullptr) {
      add_issue(graph, MakeIssue(clock, ClockGraphIssueCode::kMissingClockSource, "clock source pin is null"));
    } else {
      AppendPin(graph, source_pin);
    }
    if (source_net == nullptr) {
      auto issue = MakeIssue(clock, ClockGraphIssueCode::kMissingNet, "clock source net is null");
      issue.object_name = clock->get_clock_net_name();
      add_issue(graph, std::move(issue));
    }

    std::vector<Net*> nets;
    std::unordered_set<const Net*> net_membership;
    const auto append_net = [&](Net* net, bool report_duplicate) -> void {
      if (net == nullptr) {
        return;
      }
      if (!net_membership.insert(net).second) {
        if (report_duplicate) {
          auto issue = MakeIssue(clock, ClockGraphIssueCode::kDuplicateNetMembership, "net appears more than once in explicit clock membership");
          issue.object_name = net->get_name();
          add_issue(graph, std::move(issue));
        }
        return;
      }
      nets.push_back(net);
    };
    append_net(source_net, false);
    for (auto* net : clock->get_nets()) {
      if (net == nullptr) {
        add_issue(graph, MakeIssue(clock, ClockGraphIssueCode::kMissingNet, "explicit clock net pointer is null"));
        continue;
      }
      append_net(net, net != source_net);
    }
    std::ranges::sort(nets, NetLess);

    std::unordered_map<const Pin*, const Net*> owning_net_by_pin;
    for (auto* net : nets) {
      AppendNet(graph, net);
      auto* driver = net->get_driver();
      if (driver == nullptr) {
        auto issue = MakeIssue(clock, ClockGraphIssueCode::kMissingNetDriver, "explicit clock net has no driver pin");
        issue.object_name = net->get_name();
        add_issue(graph, std::move(issue));
      } else {
        if (driver->get_net() != net) {
          auto issue = MakeIssue(clock, ClockGraphIssueCode::kForeignNetMembership, "net driver points to a different net");
          issue.object_name = net->get_name();
          issue.output_pin_name = PinName(driver);
          issue.expected = net->get_name();
          issue.observed = NetName(driver->get_net());
          add_issue(graph, std::move(issue));
        }
        if (const auto [iter, inserted] = owning_net_by_pin.emplace(driver, net); !inserted && iter->second != net) {
          auto issue = MakeIssue(clock, ClockGraphIssueCode::kDuplicatePinMembership, "pin belongs to multiple explicit clock nets");
          issue.object_name = PinName(driver);
          issue.expected = iter->second->get_name();
          issue.observed = net->get_name();
          add_issue(graph, std::move(issue));
        }
        AppendPin(graph, driver);
      }

      auto loads = net->get_loads();
      std::ranges::sort(loads, [](const Pin* lhs, const Pin* rhs) -> bool { return PinName(lhs) < PinName(rhs); });
      std::unordered_set<const Pin*> local_loads;
      for (auto* load : loads) {
        if (load == nullptr) {
          auto issue = MakeIssue(clock, ClockGraphIssueCode::kMissingNetLoad, "explicit clock net contains a null load pin");
          issue.object_name = net->get_name();
          add_issue(graph, std::move(issue));
          continue;
        }
        if (!local_loads.insert(load).second) {
          auto issue = MakeIssue(clock, ClockGraphIssueCode::kDuplicatePinMembership, "load pin appears more than once on an explicit clock net");
          issue.object_name = PinName(load);
          issue.observed = net->get_name();
          add_issue(graph, std::move(issue));
          continue;
        }
        if (load->get_net() != net) {
          auto issue = MakeIssue(clock, ClockGraphIssueCode::kForeignNetMembership, "net load points to a different net");
          issue.object_name = net->get_name();
          issue.input_pin_name = PinName(load);
          issue.expected = net->get_name();
          issue.observed = NetName(load->get_net());
          add_issue(graph, std::move(issue));
        }
        if (const auto [iter, inserted] = owning_net_by_pin.emplace(load, net); !inserted && iter->second != net) {
          auto issue = MakeIssue(clock, ClockGraphIssueCode::kDuplicatePinMembership, "pin belongs to multiple explicit clock nets");
          issue.object_name = PinName(load);
          issue.expected = iter->second->get_name();
          issue.observed = net->get_name();
          add_issue(graph, std::move(issue));
        }
        AppendArc(graph, driver, load, net, 0);
      }
    }

    std::unordered_set<const Inst*> explicit_inst_membership(clock->get_insts().begin(), clock->get_insts().end());
    std::vector<const ClockPropagationArc*> arcs;
    arcs.reserve(clock->get_propagation_arcs().size());
    for (const auto& arc : clock->get_propagation_arcs()) {
      arcs.push_back(&arc);
    }
    std::ranges::sort(arcs, ArcLess);
    for (const auto* arc : arcs) {
      auto issue_base = MakePropagationIssueBase(clock, arc);
      if (arc == nullptr || arc->inst == nullptr || arc->input_pin == nullptr || arc->output_pin == nullptr || arc->input_pin == arc->output_pin) {
        issue_base.expected = "non-null inst and distinct non-null input/output pins";
        issue_base.observed = arc == nullptr ? "null arc" : "one or more required propagation objects are null or aliased";
        issue_base.message = "clock propagation arc is incomplete";
        add_issue(graph, std::move(issue_base));
        continue;
      }

      if (!explicit_inst_membership.contains(arc->inst)) {
        auto issue = issue_base;
        SetIssueCode(issue, ClockGraphIssueCode::kForeignPinMembership);
        issue.message = "propagation inst is absent from explicit clock inst membership";
        add_issue(graph, std::move(issue));
      }
      if (arc->input_pin->get_inst() != arc->inst || arc->output_pin->get_inst() != arc->inst) {
        auto issue = issue_base;
        SetIssueCode(issue, ClockGraphIssueCode::kPropagationPinInstMismatch);
        issue.message = "propagation pins do not both belong to the recorded inst";
        add_issue(graph, std::move(issue));
      }
      if (!IsInputDirection(arc->input_pin->get_type()) || !IsOutputDirection(arc->output_pin->get_type())) {
        auto issue = issue_base;
        SetIssueCode(issue, ClockGraphIssueCode::kInvalidPinDirection);
        issue.expected = "input->output";
        issue.observed = std::string(PinDirectionName(arc->input_pin->get_type())) + "->" + PinDirectionName(arc->output_pin->get_type());
        issue.message = "propagation pin directions are incompatible";
        add_issue(graph, std::move(issue));
      }
      const bool physical_kind_matches = (arc->kind == ClockPropagationKind::kBuffer && arc->inst->is_buffer())
                                         || (arc->kind == ClockPropagationKind::kInverter && arc->inst->is_inverter());
      if (!physical_kind_matches) {
        auto issue = issue_base;
        SetIssueCode(issue, ClockGraphIssueCode::kPhysicalKindMismatch);
        issue.expected = issue.propagation_kind;
        issue.observed = InstTypeName(arc->inst->get_type());
        issue.message = "propagation kind does not match physical inst kind";
        add_issue(graph, std::move(issue));
      }
      if (!net_membership.contains(arc->input_pin->get_net()) || !net_membership.contains(arc->output_pin->get_net())) {
        auto issue = issue_base;
        SetIssueCode(issue, ClockGraphIssueCode::kForeignNetMembership);
        issue.message = "propagation input/output net is absent from explicit clock net membership";
        add_issue(graph, std::move(issue));
      }
      if (propagation_owners[arc->inst].size() > 1U) {
        auto issue = issue_base;
        SetIssueCode(issue, ClockGraphIssueCode::kAmbiguousCrossClockOwnership);
        issue.expected = "exactly one clock owner";
        std::vector<std::string> owner_names;
        owner_names.reserve(propagation_owners[arc->inst].size());
        for (const auto* owner : propagation_owners[arc->inst]) {
          owner_names.push_back(owner == nullptr ? "n/a" : owner->get_clock_name());
        }
        std::ranges::sort(owner_names);
        for (const auto& owner_name : owner_names) {
          if (!issue.observed.empty()) {
            issue.observed += ",";
          }
          issue.observed += owner_name;
        }
        issue.message = "propagation inst is owned by multiple clocks";
        add_issue(graph, std::move(issue));
      }
      AppendArc(graph, arc->input_pin, arc->output_pin, nullptr, arc->path_buffer_weight);
    }

    if (!FinishTopologicalOrder(graph)) {
      add_issue(graph, MakeIssue(clock, ClockGraphIssueCode::kGraphCycle, "clock graph contains a cycle"));
    } else {
      const auto reachable = ReachablePins(graph, source_pin);
      auto declared_loads = clock->get_loads();
      std::ranges::sort(declared_loads, [](const Pin* lhs, const Pin* rhs) -> bool { return PinName(lhs) < PinName(rhs); });
      for (auto* load : declared_loads) {
        if (load == nullptr || !graph.pin_set.contains(load)) {
          auto issue = MakeIssue(clock, ClockGraphIssueCode::kForeignPinMembership, "declared clock sink is absent from explicit net membership");
          issue.object_name = PinName(load);
          add_issue(graph, std::move(issue));
        } else if (!reachable.contains(load)) {
          auto issue = MakeIssue(clock, ClockGraphIssueCode::kUnreachableDeclaredSink, "declared clock sink is unreachable from the clock source");
          issue.object_name = PinName(load);
          issue.input_net_name = NetName(load->get_net());
          add_issue(graph, std::move(issue));
        }
      }
    }

    _valid = _valid && graph.valid;
    _graphs_by_clock.emplace(clock, std::move(graph));
  }

  if (!_issues.empty()) {
    _status = FormatClockGraphIssue(_issues.front());
  }
  return is_valid();
}

auto ClockDAG::clear() -> void
{
  _built = false;
  _valid = false;
  _status = "not_built";
  _issues.clear();
  _clock_order.clear();
  _graphs_by_clock.clear();
}

auto ClockDAG::invalidate(const std::string& reason) -> void
{
  clear();
  _status = reason.empty() ? "invalidated" : reason;
}

auto ClockDAG::findGraph(const Clock* clock) const -> const ClockGraph*
{
  if (clock == nullptr) {
    return nullptr;
  }
  const auto iter = _graphs_by_clock.find(clock);
  return iter == _graphs_by_clock.end() ? nullptr : &iter->second;
}

auto ClockDAG::hasCycle(const Clock* clock) const -> bool
{
  const auto* graph = findGraph(clock);
  return graph != nullptr && graph->has_cycle;
}

auto ClockDAG::topologicalPins(const Clock* clock) const -> std::vector<Pin*>
{
  const auto* graph = findGraph(clock);
  if (graph == nullptr || !graph->valid) {
    return {};
  }
  return graph->topological_pins;
}

auto ClockDAG::reachablePins(const Clock* clock) const -> std::vector<Pin*>
{
  return reachablePinsFrom(clock, clock != nullptr ? clock->get_clock_source() : nullptr);
}

auto ClockDAG::reachablePinsFrom(const Clock* clock, Pin* start_pin) const -> std::vector<Pin*>
{
  const auto* graph = findGraph(clock);
  if (graph == nullptr || !graph->valid || start_pin == nullptr || !graph->pin_set.contains(start_pin)) {
    return {};
  }
  std::vector<Pin*> reachable;
  std::deque<Pin*> pending = {start_pin};
  std::unordered_set<const Pin*> visited;
  while (!pending.empty()) {
    auto* pin = pending.front();
    pending.pop_front();
    if (pin == nullptr || !visited.insert(pin).second) {
      continue;
    }
    reachable.push_back(pin);
    const auto arc_iter = graph->outgoing_arcs.find(pin);
    if (arc_iter == graph->outgoing_arcs.end()) {
      continue;
    }
    for (const auto& arc : arc_iter->second) {
      pending.push_back(arc.to);
    }
  }
  return reachable;
}

auto ClockDAG::reachableNets(const Clock* clock) const -> std::vector<Net*>
{
  const auto* graph = findGraph(clock);
  if (graph == nullptr || !graph->valid) {
    return {};
  }
  const auto reachable_pins = reachablePins(clock);
  std::unordered_set<const Pin*> reachable_pin_set(reachable_pins.begin(), reachable_pins.end());
  std::vector<Net*> nets;
  nets.reserve(graph->nets.size());
  for (auto* net : graph->nets) {
    if (net != nullptr && net->get_driver() != nullptr && reachable_pin_set.contains(net->get_driver())) {
      nets.push_back(net);
    }
  }
  return nets;
}

auto ClockDAG::graphForClock(const Clock* clock) const -> const ClockGraph*
{
  const auto* graph = findGraph(clock);
  return graph != nullptr && graph->valid ? graph : nullptr;
}

auto ClockDAG::pathBufferStats(const Clock* clock) const -> PathBufferStats
{
  PathBufferStats stats;
  if (!_built) {
    stats.status = "not_built";
    return stats;
  }
  if (!_valid) {
    stats.status = "invalid_topology";
    return stats;
  }

  const auto* graph = findGraph(clock);
  if (graph == nullptr || clock == nullptr) {
    stats.status = "clock_not_found";
    return stats;
  }
  if (!graph->valid) {
    stats.status = "invalid_topology";
    return stats;
  }
  stats.topology_valid = true;
  if (clock->get_clock_source() == nullptr) {
    stats.status = "clock_source_pin_is_null";
    return stats;
  }

  constexpr int32_t unreachable_count = std::numeric_limits<int32_t>::max() / 4;
  std::unordered_map<const Pin*, int32_t> min_count;
  std::unordered_map<const Pin*, int32_t> max_count;
  for (auto* pin : graph->pins) {
    min_count[pin] = unreachable_count;
    max_count[pin] = -1;
  }
  min_count[clock->get_clock_source()] = 0;
  max_count[clock->get_clock_source()] = 0;

  for (auto* pin : graph->topological_pins) {
    if (min_count[pin] == unreachable_count) {
      continue;
    }
    const auto arc_iter = graph->outgoing_arcs.find(pin);
    if (arc_iter == graph->outgoing_arcs.end()) {
      continue;
    }
    for (const auto& arc : arc_iter->second) {
      min_count[arc.to] = std::min(min_count[arc.to], min_count[pin] + arc.path_buffer_weight);
      max_count[arc.to] = std::max(max_count[arc.to], max_count[pin] + arc.path_buffer_weight);
    }
  }

  int32_t min_path_count = unreachable_count;
  int32_t max_path_count = 0;
  for (auto* pin : clock->get_loads()) {
    ++stats.terminal_probe_count;
    const auto min_iter = min_count.find(pin);
    const auto max_iter = max_count.find(pin);
    if (min_iter == min_count.end() || max_iter == max_count.end() || min_iter->second == unreachable_count || !IsFlipFlopSinkTerminal(pin)) {
      continue;
    }
    min_path_count = std::min(min_path_count, min_iter->second);
    max_path_count = std::max(max_path_count, max_iter->second);
    ++stats.ff_sink_terminal_count;
  }

  stats.status = StatusForUnavailableClockStats(*clock, stats.ff_sink_terminal_count);
  stats.has_ff_sink_terminal = stats.ff_sink_terminal_count > 0U;
  stats.available = stats.has_ff_sink_terminal;
  if (stats.available) {
    stats.min_buffer_count = min_path_count;
    stats.max_buffer_count = max_path_count;
  }
  return stats;
}

auto ClockDAG::pathBufferStats() const -> PathBufferStats
{
  PathBufferStats aggregate;
  if (!_built) {
    aggregate.status = "not_built";
    return aggregate;
  }
  if (!_valid) {
    aggregate.status = "invalid_topology";
    return aggregate;
  }

  int32_t min_path_count = std::numeric_limits<int32_t>::max();
  int32_t max_path_count = 0;
  std::string first_unavailable_status = "no_ff_sink_terminal";
  bool has_unavailable_status = false;
  for (const auto* clock : _clock_order) {
    const auto stats = pathBufferStats(clock);
    if (stats.available) {
      aggregate.available = true;
      aggregate.has_ff_sink_terminal = true;
      aggregate.ff_sink_terminal_count += stats.ff_sink_terminal_count;
      aggregate.terminal_probe_count += stats.terminal_probe_count;
      min_path_count = std::min(min_path_count, stats.min_buffer_count);
      max_path_count = std::max(max_path_count, stats.max_buffer_count);
    } else {
      aggregate.terminal_probe_count += stats.terminal_probe_count;
      if (has_unavailable_status) {
        continue;
      }
      first_unavailable_status = stats.status;
      has_unavailable_status = true;
    }
  }

  aggregate.topology_valid = true;
  if (aggregate.available) {
    aggregate.min_buffer_count = min_path_count;
    aggregate.max_buffer_count = max_path_count;
    aggregate.status = "available";
  } else {
    aggregate.status = first_unavailable_status;
  }
  return aggregate;
}

}  // namespace icts
