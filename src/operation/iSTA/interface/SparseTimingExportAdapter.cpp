// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "EndpointQualification.hpp"
#include "NetThresholdAdjustment.hpp"
#include "SparseTimingSnapshot.hpp"
#include "TimingAnalyzer.hpp"
#include "TimingExceptionMatcher.hpp"
#include "TimingExportAdapter.hpp"

namespace ista {
namespace {

// Keep the launch domain in the identity. Exception tags are not guaranteed
// to be globally unique across clock domains, so (source type, tag) alone can
// incorrectly merge two arrivals at a reconvergent pin.
using StateKey = std::tuple<int32_t, std::string, std::string>;
using PathPair = std::array<TimingPathState*, 2>;

std::map<StateKey, PathPair> maxStates(TimingPoint& point)
{
  std::map<StateKey, PathPair> result;
  auto analysis = point.get_path_state_map().find(AnalysisType::kMax);
  if (analysis == point.get_path_state_map().end())
    return result;
  for (auto& [source, transitions] : analysis->second) {
    for (auto& [transition, paths] : transitions) {
      if (transition != TransType::kRise && transition != TransType::kFall)
        continue;
      const int edge = transition == TransType::kRise ? 0 : 1;
      for (auto& [tag, path] : paths) {
        if (std::isfinite(path.get_arrival())) {
          result[{static_cast<int32_t>(source), path.get_clock_name(), tag}][edge] = &path;
        }
      }
    }
  }
  return result;
}

bool needsSparse(Database& database)
{
  // Regression control: exercise the sparse backend with unchanged single-clock SDCs.
  const char* force_sparse = std::getenv("ECC_TIMING_FORCE_SPARSE");
  if (force_sparse != nullptr && std::string(force_sparse) == "1")
    return true;
  auto& constraint = database.get_timing_constraint();
  if (constraint.get_clock_map().size() > 1)
    return true;
  for (const auto& exception : constraint.get_path_exception_list()) {
    if (exception.get_type() != TimingExceptionType::kMinDelay && exception.get_setup())
      return true;
  }
  for (const auto& group : constraint.get_clock_group_list()) {
    if (!group.get_allow_paths())
      return true;
  }
  return false;
}

}  // namespace

void SparseTimingExportAdapter::compile(TimingSnapshot& snapshot)
{
  Database& database = STADM.getDatabase();
  auto& view = snapshot.sparse;
  if (!needsSparse(database))
    return;
  view.enabled = true;
  snapshot.schema_version = 3;
  auto& exceptions = database.get_timing_constraint().get_path_exception_list();
  for (std::size_t index = 0; index < exceptions.size(); ++index) {
    if (!exceptions[index].get_through_list().empty()) {
      view.unsupported.push_back("path exception " + std::to_string(index) + " requires through-state transitions");
    }
  }
  view.domains = snapshot.clock_names;
  view.domains.push_back("");  // unclocked input domain
  // Freeze the exact native loads for fixed-RC validation. Runtime propagation
  // uses its current physical RC vectors; these values are diagnostic only.
  view.reference_pin_cap.resize(snapshot.pins.size(), {0, 0});
  std::unordered_map<std::string, int32_t> pin_ids;
  for (std::size_t index = 0; index < snapshot.pins.size(); ++index)
    pin_ids.emplace(snapshot.pins[index].name, index);
  DelayCalculator calculator;
  for (auto& [name, net] : database.get_net_map()) {
    if (!pin_ids.contains(net.get_driver_pin()))
      continue;
    auto& cap = view.reference_pin_cap[pin_ids.at(net.get_driver_pin())];
    cap[0] = calculator.getNetOutputLoad(net, AnalysisType::kMax, TransType::kRise) * snapshot.capacitance_unit_pf;
    cap[1] = calculator.getNetOutputLoad(net, AnalysisType::kMax, TransType::kFall) * snapshot.capacitance_unit_pf;
  }
  std::map<std::string, int32_t> domains;
  for (std::size_t index = 0; index < view.domains.size(); ++index)
    domains.emplace(view.domains[index], index);
  view.clock_reached_pins.resize(view.domains.size(), 0);
  view.clock_reached_sequential_pins.resize(view.domains.size(), 0);
  std::set<std::string> sequential_clock_pins;
  for (auto& [name, instance] : database.get_instance_map()) {
    if (instance.get_is_sequential()) {
      ++view.sequential_instances;
      sequential_clock_pins.insert(instance.get_clock_pin_name());
    }
  }

  std::map<std::string, int32_t> tags;
  std::map<std::string, std::map<StateKey, int32_t>> pin_states;
  std::vector<PathPair> paths;
  for (auto& [name, point] : database.get_timing_point_map()) {
    for (auto& [clock, clock_state] : point.get_clock_state_map()) {
      if (domains.contains(clock)) {
        ++view.clock_reached_pins[domains.at(clock)];
        if (sequential_clock_pins.contains(name))
          ++view.clock_reached_sequential_pins[domains.at(clock)];
        if (view.clock_pin_sample.size() < 30)
          view.clock_pin_sample.push_back(clock + ":" + name);
      }
    }
    for (auto& [key, pair] : maxStates(point)) {
      auto* representative = pair[0] != nullptr ? pair[0] : pair[1];
      if (!domains.contains(representative->get_clock_name())) {
        view.unsupported.push_back("unknown launch clock at " + name);
        continue;
      }
      auto [tag, inserted] = tags.emplace(std::get<2>(key), tags.size());
      if (inserted)
        view.tags.push_back(std::get<2>(key));
      SparseTimingState state;
      state.pin = name;
      state.domain = domains.at(std::get<1>(key));
      state.tag = tag->second;
      state.source_type = std::get<0>(key);
      state.level = point.get_level();
      for (int edge = 0; edge < 2; ++edge) {
        if (pair[edge] == nullptr)
          continue;
        state.valid[edge] = true;
        state.seed[edge] = pair[edge]->get_predecessor().empty();
        state.reference[edge] = pair[edge]->get_arrival() * snapshot.time_unit_ps;
        state.reference[edge + 2] = pair[edge]->get_slew() * snapshot.time_unit_ps;
      }
      // A top-level input-delay path is a real electrical seed. iSTA's path
      // object can still carry a virtual predecessor for the input constraint,
      // so predecessor emptiness alone is not sufficient here. Keep clocked
      // launch states (source type 2) non-seed; they must arrive through the
      // tagged graph from a register clock-to-Q arc.
      const auto pin = database.get_pin_map().find(name);
      const auto port_constraint = database.get_timing_constraint().get_port_constraint_map().find(name);
      if (pin != database.get_pin_map().end() && pin->second.get_is_port()
          && port_constraint != database.get_timing_constraint().get_port_constraint_map().end() && port_constraint->second.get_has_input_delay_max()
          && state.source_type != 2) {
        state.seed = state.valid;
      }
      pin_states[name].emplace(key, view.states.size());
      view.states.push_back(std::move(state));
      paths.push_back(pair);
    }
  }

  // Promote the output role of a bidirectional pad into the sparse view. The
  // native graph has one physical PAD pin, while the input role may already
  // have a state from the top-level port. An output-delay check needs the
  // state produced by OE/I -> PAD, then a PAD -> port electrical relation.
  std::unordered_map<std::string, std::vector<std::string>> control_sinks_by_net;
  for (Arc& arc : database.get_arc_list()) {
    if (!arc.get_is_control_arc())
      continue;
    const auto sink = database.get_pin_map().find(arc.get_sink_pin());
    if (sink != database.get_pin_map().end() && !sink->second.get_net_name().empty()) {
      control_sinks_by_net[sink->second.get_net_name()].push_back(arc.get_sink_pin());
    }
  }
  // Apply the bidirectional net load to the output PAD role as well. The
  // native driver may be the top-level port (input role), while the sparse
  // output relation is driven by the PAD itself.
  for (const auto& [net_name, sinks] : control_sinks_by_net) {
    auto net = database.get_net_map().find(net_name);
    if (net == database.get_net_map().end())
      continue;
    bool has_output_port = false;
    for (const std::string& pin_name : net->second.get_pin_name_list()) {
      const auto constraint = database.get_timing_constraint().get_port_constraint_map().find(pin_name);
      if (constraint != database.get_timing_constraint().get_port_constraint_map().end() && constraint->second.get_has_output_delay_max()) {
        has_output_port = true;
        break;
      }
    }
    if (!has_output_port)
      continue;
    for (const std::string& sink : sinks) {
      const auto pin = pin_ids.find(sink);
      if (pin == pin_ids.end())
        continue;
      view.reference_pin_cap[pin->second][0] = calculator.getNetOutputLoad(net->second, AnalysisType::kMax, TransType::kRise) * snapshot.capacitance_unit_pf;
      view.reference_pin_cap[pin->second][1] = calculator.getNetOutputLoad(net->second, AnalysisType::kMax, TransType::kFall) * snapshot.capacitance_unit_pf;
    }
  }
  for (auto& [net_name, net] : database.get_net_map()) {
    const auto control_sinks = control_sinks_by_net.find(net_name);
    if (control_sinks == control_sinks_by_net.end())
      continue;
    for (const std::string& port_name : net.get_pin_name_list()) {
      const auto port = database.get_pin_map().find(port_name);
      const auto constraint = database.get_timing_constraint().get_port_constraint_map().find(port_name);
      if (port == database.get_pin_map().end() || !port->second.get_is_port() || constraint == database.get_timing_constraint().get_port_constraint_map().end()
          || !constraint->second.get_has_output_delay_max()) {
        continue;
      }
      auto& output_states = pin_states[port_name];
      for (const std::string& sink_name : control_sinks->second) {
        const auto source_states = pin_states.find(sink_name);
        if (source_states == pin_states.end())
          continue;
        for (const auto& [key, source_id] : source_states->second) {
          if (output_states.contains(key))
            continue;
          const auto source_path = paths[source_id];
          SparseTimingState state = view.states[source_id];
          state.pin = port_name;
          state.level = view.states[source_id].level + 1;
          state.seed = {false, false};
          output_states.emplace(key, static_cast<int32_t>(view.states.size()));
          view.states.push_back(std::move(state));
          paths.push_back(source_path);
        }
      }
    }
  }
  std::set<std::string> isolated_slew_pins;
  for (const auto& [net_name, sinks] : control_sinks_by_net) {
    isolated_slew_pins.insert(sinks.begin(), sinks.end());
    const auto net = database.get_net_map().find(net_name);
    if (net == database.get_net_map().end())
      continue;
    for (const std::string& pin_name : net->second.get_pin_name_list()) {
      const auto port = database.get_pin_map().find(pin_name);
      const auto constraint = database.get_timing_constraint().get_port_constraint_map().find(pin_name);
      if (port != database.get_pin_map().end() && port->second.get_is_port() && constraint != database.get_timing_constraint().get_port_constraint_map().end()
          && constraint->second.get_has_output_delay_max()) {
        isolated_slew_pins.insert(pin_name);
      }
    }
  }
  std::map<std::pair<int32_t, int32_t>, int32_t> isolated_groups;
  int32_t next_slew_group = static_cast<int32_t>(pin_ids.size());
  for (auto& state : view.states) {
    const int32_t pin = pin_ids.at(state.pin);
    if (!isolated_slew_pins.contains(state.pin)) {
      state.slew_group = pin;
      continue;
    }
    const auto key = std::make_pair(pin, state.source_type);
    const auto [group, inserted] = isolated_groups.emplace(key, next_slew_group);
    if (inserted)
      ++next_slew_group;
    state.slew_group = group->second;
  }
  if (view.sequential_instances > 0 && std::none_of(view.states.begin(), view.states.end(), [](const auto& state) { return state.source_type == 2; })) {
    view.unsupported.push_back("no register launch states despite sequential instances; clock coverage is incomplete");
  }

  // Native path maps supply reachability; use every active base edge between
  // equivalent states, never just the winning predecessor from the last STA.
  std::set<std::pair<std::string, std::string>> active;
  std::set<std::pair<std::string, std::string>> slew_only;
  for (auto& arc : database.get_arc_list()) {
    if (!arc.get_is_disable_arc() && !arc.get_is_loop_disable())
      active.emplace(arc.get_source_pin(), arc.get_sink_pin());
    if (arc.get_is_disable_arc() && !arc.get_is_electrical_disable_arc() && !arc.get_is_loop_disable()) {
      slew_only.emplace(arc.get_source_pin(), arc.get_sink_pin());
    }
  }
  auto appendEdges = [&](const auto& base_arcs, auto& target, bool cell) {
    for (std::size_t base = 0; base < base_arcs.size(); ++base) {
      const auto& arc = base_arcs[base];
      if constexpr (std::is_same_v<std::decay_t<decltype(arc)>, TimingGraphArcSnapshot>) {
        if (arc.arc_type != 1 || arc.library_arc_id < 0)
          continue;
      }
      const auto sink_pin = database.get_pin_map().find(arc.sink_pin);
      const auto sink_constraint = database.get_timing_constraint().get_port_constraint_map().find(arc.sink_pin);
      const bool synthetic_output = !cell && sink_pin != database.get_pin_map().end() && sink_pin->second.get_is_port()
                                    && sink_constraint != database.get_timing_constraint().get_port_constraint_map().end()
                                    && sink_constraint->second.get_has_output_delay_max();
      if (!active.contains({arc.source_pin, arc.sink_pin}) && !synthetic_output)
        continue;
      const auto sources = pin_states.find(arc.source_pin);
      const auto sinks = pin_states.find(arc.sink_pin);
      if (sources == pin_states.end() || sinks == pin_states.end())
        continue;
      for (const auto& [key, source] : sources->second) {
        // The output electrical role of an inout PAD must not feed the PAD's
        // input receiver. OpenSTA keeps these vertices separate; retaining
        // this edge would create a false internal loop and a large bogus
        // arrival at GPIO/SD input endpoints.
        if (cell && isolated_slew_pins.contains(arc.source_pin) && view.states[source].source_type == 2) {
          continue;
        }
        const auto sink = sinks->second.find(key);
        if (sink == sinks->second.end())
          continue;
        // CK is a launch seed. Never feed incoming data back into it.
        const auto& destination = view.states[sink->second];
        if (!cell && (destination.seed[0] || destination.seed[1]))
          continue;
        if (!cell && view.states[source].level >= destination.level)
          continue;
        target.push_back({static_cast<int32_t>(base), source, sink->second});
      }
    }
  };
  appendEdges(snapshot.graph_arcs, view.cell_edges, true);
  appendEdges(snapshot.net_arcs, view.net_edges, false);
  // A function-insensitive arc can still dominate GBA slew. Its arrival is
  // excluded, so keep one electrical edge per physical slew-group pair.
  for (std::size_t base = 0; base < snapshot.graph_arcs.size(); ++base) {
    const auto& arc = snapshot.graph_arcs[base];
    if (arc.arc_type != 1 || arc.library_arc_id < 0 || !slew_only.contains({arc.source_pin, arc.sink_pin}))
      continue;
    const auto sources = pin_states.find(arc.source_pin);
    const auto sinks = pin_states.find(arc.sink_pin);
    if (sinks == pin_states.end())
      continue;
    if (sources == pin_states.end()) {
      view.unsupported.push_back("missing electrical source state at " + arc.source_pin);
      continue;
    }
    std::set<std::pair<int32_t, int32_t>> groups;
    for (const auto& [source_key, source] : sources->second) {
      for (const auto& [sink_key, sink] : sinks->second) {
        const auto& src = view.states[source];
        const auto& dst = view.states[sink];
        if (!groups.emplace(src.slew_group, dst.slew_group).second)
          continue;
        if (src.level >= dst.level) {
          view.unsupported.push_back("non-forward electrical edge at " + arc.sink_pin);
          continue;
        }
        view.slew_only_cell_edges.push_back({static_cast<int32_t>(base), source, sink});
      }
    }
  }
  for (const auto& edge : view.net_edges) {
    const auto& arc = snapshot.net_arcs.at(edge.base_arc);
    view.net_threshold_adjustments.push_back(netThresholdAdjustment(database, arc.source_pin, arc.sink_pin));
  }

  // Some output ports are represented as virtual timing endpoints with no DEF
  // net row. Their native path arrival is already the launch-side electrical
  // result, so use it as a seed when no sparse net edge feeds the port. A
  // bidirectional PAD with a synthetic PAD -> port edge remains non-seed.
  std::set<int32_t> net_destinations;
  for (const auto& edge : view.net_edges)
    net_destinations.insert(edge.sink);
  for (std::size_t state_id = 0; state_id < view.states.size(); ++state_id) {
    auto& state = view.states[state_id];
    const auto pin = database.get_pin_map().find(state.pin);
    const auto constraint = database.get_timing_constraint().get_port_constraint_map().find(state.pin);
    if (pin != database.get_pin_map().end() && pin->second.get_is_port() && state.source_type == 2
        && constraint != database.get_timing_constraint().get_port_constraint_map().end() && constraint->second.get_has_output_delay_max()
        && !net_destinations.contains(state_id)) {
      state.seed = state.valid;
    }
  }

  TimingAnalyzer analyzer;
  using CheckKey = std::tuple<std::string, std::string, int32_t>;
  using CheckSignature = std::tuple<std::string, int32_t, int32_t, int32_t>;
  std::map<CheckKey, std::map<CheckSignature, int32_t>> check_ids;
  for (std::size_t index = 0; index < snapshot.timing_check_arcs.size(); ++index) {
    const auto& arc = snapshot.timing_check_arcs[index];
    // Schema v2 emits one qualification row for each data transition, while
    // Liberty export merges its rise/fall tables into one check LUT. Those
    // identical electrical rows are a single rule in the sparse relation.
    check_ids[{arc.sink_pin, arc.source_pin, arc.check_type}].emplace(CheckSignature{arc.library_cell, arc.library_arc_id, arc.sense, arc.timing_type}, index);
  }
  std::set<std::string> endpoints(snapshot.end_points.begin(), snapshot.end_points.end());
  for (std::size_t state_id = 0; state_id < view.states.size(); ++state_id) {
    auto& state = view.states[state_id];
    if (!endpoints.contains(state.pin))
      continue;
    auto& pin = database.get_pin_map().at(state.pin);
    auto* check = analyzer.getEndPointCheckArc(state.pin, AnalysisType::kMax);
    if (check == nullptr && !pin.get_is_port())
      continue;
    if (pin.get_is_port()) {
      auto port = database.get_timing_constraint().get_port_constraint_map().find(state.pin);
      if (port == database.get_timing_constraint().get_port_constraint_map().end() || !port->second.get_has_output_delay_max())
        continue;
    }
    int32_t endpoint_arc = -1;
    if (check != nullptr) {
      const auto matches = check_ids.find({state.pin, check->get_clock_port(), static_cast<int32_t>(check->get_check_type())});
      if (matches != check_ids.end() && matches->second.size() == 1)
        endpoint_arc = matches->second.begin()->second;
      else if (matches != check_ids.end())
        view.unsupported.push_back("multiple max check arcs at " + state.pin);
      if (endpoint_arc < 0 || snapshot.timing_check_arcs[endpoint_arc].library_arc_id < 0) {
        view.unsupported.push_back("missing max check LUT at " + state.pin);
        continue;
      }
    }
    for (int edge = 0; edge < 2; ++edge) {
      if (!state.valid[edge])
        continue;
      TimingPathState path = *paths[state_id][edge];
      if (check != nullptr && !analyzer.isMatchCheckTransType(*check, path.get_trans_type()))
        continue;
      const auto capture_edge = check == nullptr ? TransType::kRise : analyzer.getClockTransType(*check);
      for (const auto& capture : analyzer.getEndPointClockNames(state.pin)) {
        if (check != nullptr && !hasCaptureClockState(database, check->get_clock_port(), capture, capture_edge))
          continue;
        if (!domains.contains(capture)) {
          view.unsupported.push_back("unknown capture clock at " + state.pin);
          continue;
        }
        path.set_capture_clock_name(capture);
        const auto resolved = TimingExceptionMatcher::resolve(database, path.get_start_point(), path.get_clock_name(), state.pin, capture, AnalysisType::kMax,
                                                              path.get_trans_type(), capture_edge, path.get_exception_state_list());
        SparseTimingCheck row;
        row.state = state_id;
        row.capture_domain = domains.at(capture);
        row.endpoint_arc = endpoint_arc;
        row.transition = edge;
        row.valid = !resolved.false_path;
        if (resolved.false_path)
          row.kind = SparseTimingRuleKind::kFalsePath;
        else if (resolved.path_delay != nullptr)
          row.kind = SparseTimingRuleKind::kMaxDelay;
        else if (resolved.setup_multicycle != nullptr)
          row.kind = SparseTimingRuleKind::kMulticycle;
        const auto* chosen = resolved.path_delay != nullptr ? resolved.path_delay : resolved.setup_multicycle;
        if (chosen != nullptr)
          row.constraint = chosen - exceptions.data();
        if (row.valid) {
          double check_time = 0;
          if (check != nullptr) {
            row.check_coefficient = -1;
            check_time = analyzer.getEndPointCheckTime(state.pin, *check, capture, AnalysisType::kMax, path.get_trans_type(), path.get_slew());
            row.clock_slew_ps = analyzer.getClockSlew(check->get_clock_port(), capture, AnalysisType::kMin, capture_edge) * snapshot.time_unit_ps;
          }
          const double required = analyzer.calcPathRequiredTime(state.pin, path, AnalysisType::kMax);
          row.reference_required_ps = required * snapshot.time_unit_ps;
          row.intercept_ps = (required - row.check_coefficient * check_time) * snapshot.time_unit_ps;
          if (!std::isfinite(row.intercept_ps))
            view.unsupported.push_back("non-finite required rule at " + state.pin);
        }
        view.checks.push_back(row);
      }
    }
  }
  std::sort(view.unsupported.begin(), view.unsupported.end());
  view.unsupported.erase(std::unique(view.unsupported.begin(), view.unsupported.end()), view.unsupported.end());
}

}  // namespace ista
