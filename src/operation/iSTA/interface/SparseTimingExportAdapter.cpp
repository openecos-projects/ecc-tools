// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "liberty/LibertyCell.hpp"
#include "liberty/TimingArc.hpp"
#include "liberty/TimingCheckArc.hpp"
#include "NetThresholdAdjustment.hpp"
#include "SparseTimingSnapshot.hpp"
#include "SparseElectricalExport.hpp"
#include "TimingData.hpp"
#include "graph/TimingStateQuery.hpp"
#include "path/PathEnd.hpp"
#include "sdc/ExceptionMatcher.hpp"
#include "timing_checker/PathChecker.hpp"

namespace ista {
namespace {

// Keep the launch domain in the identity. Exception tags are not guaranteed
// to be globally unique across clock domains, so (source type, tag) alone can
// incorrectly merge two arrivals at a reconvergent pin.
using StateKey = std::tuple<int32_t, std::string, std::string, int32_t>;
using PathPair = std::array<TimingPathState*, 2>;

struct EndpointArcKey
{
  std::string_view sink;
  std::string_view source;
  int32_t check_type;
  int32_t library_arc;
  bool operator==(const EndpointArcKey&) const = default;
};

struct EndpointArcHash
{
  std::size_t operator()(const EndpointArcKey& key) const
  {
    auto hash = std::hash<std::string_view>{}(key.sink);
    for (auto value : {std::hash<std::string_view>{}(key.source), std::hash<int32_t>{}(key.check_type),
                       std::hash<int32_t>{}(key.library_arc)})
      hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    return hash;
  }
};

bool isBidirectionalCellPin(Database& database, const std::string& pin_name)
{
  const auto pin = database.network().pins().find(pin_name);
  if (pin == database.network().pins().end() || pin->second.get_is_port())
    return false;
  const auto instance = database.network().instances().find(pin->second.get_instance_name());
  if (instance == database.network().instances().end())
    return false;
  const auto cell = database.liberty().get_cell_map().find(instance->second.get_cell_name());
  if (cell == database.liberty().get_cell_map().end())
    return false;
  const auto port = cell->second.get_port_map().find(pin->second.get_pin_name());
  return port != cell->second.get_port_map().end() && port->second.get_is_input() && port->second.get_is_output();
}

bool isTopLevelInoutNet(Database& database, const std::unordered_set<std::string>& inout_nets, const std::string& pin_name)
{
  if (inout_nets.empty())
    return false;
  const auto pin = database.network().pins().find(pin_name);
  return pin != database.network().pins().end() && inout_nets.contains(pin->second.get_net_name());
}

bool sameLaunchTag(const std::vector<std::string>& tags, int32_t lhs, int32_t rhs)
{
  if (lhs == rhs)
    return true;
  if (lhs < 0 || rhs < 0 || static_cast<std::size_t>(lhs) >= tags.size() || static_cast<std::size_t>(rhs) >= tags.size())
    return false;
  const auto lhs_separator = tags[lhs].find('\x1f');
  const auto rhs_separator = tags[rhs].find('\x1f');
  if (lhs_separator == std::string::npos || rhs_separator == std::string::npos)
    return false;
  // L/D is the load/driver role prefix. The remainder is the native launch
  // identity and is allowed to cross a physical inout net.
  return tags[lhs].substr(lhs_separator + 1) == tags[rhs].substr(rhs_separator + 1);
}

std::string localPortName(const std::string& pin_name)
{
  const auto separator = pin_name.find(':');
  return separator == std::string::npos ? pin_name : pin_name.substr(separator + 1);
}

int32_t selectedCheckLibraryArcId(Database& database, const std::string& data_pin, const std::string& clock_pin,
                                  TimingCheckType check_type, TransType clock_transition, int32_t selected_arc_idx)
{
  if (selected_arc_idx < 0)
    return -1;
  const auto pin = database.network().pins().find(data_pin);
  if (pin == database.network().pins().end())
    return -1;
  const auto instance = database.network().instances().find(pin->second.get_instance_name());
  if (instance == database.network().instances().end())
    return -1;
  const auto cell = database.liberty().get_cell_map().find(instance->second.get_cell_name());
  if (cell == database.liberty().get_cell_map().end())
    return -1;

  int32_t flat_id = 0;
  for (const auto& arc : cell->second.get_cell_arc_list())
    flat_id += static_cast<int32_t>(arc.get_timing_arc_list().size());
  const std::string local_data = localPortName(data_pin);
  const std::string local_clock = localPortName(clock_pin);
  for (const auto& check : cell->second.get_check_arc_list()) {
    for (const auto& arc : check.get_timing_arc_list()) {
      if (check.get_check_type() == check_type && check.get_clock_port() == local_clock && check.get_data_port() == local_data
          && check.get_clock_trans_type() == clock_transition && arc.get_arc_idx() == selected_arc_idx)
        return flat_id;
      ++flat_id;
    }
  }
  return -1;
}

std::map<StateKey, PathPair> pathStates(TimingPoint& point, int32_t role, AnalysisType analysis_type)
{
  std::map<StateKey, PathPair> result;
  auto analysis = point.get_path_state_map().find(analysis_type);
  if (analysis == point.get_path_state_map().end())
    return result;
  for (auto& [source, transitions] : analysis->second) {
    if (source == PathSourceType::kNone)
      continue;
    for (auto& [transition, paths] : transitions) {
      if (transition != TransType::kRise && transition != TransType::kFall)
        continue;
      const int edge = transition == TransType::kRise ? 0 : 1;
      for (auto& [tag, path] : paths) {
        if (std::isfinite(path.get_arrival())) {
          result[{static_cast<int32_t>(source), path.get_clock_name(), tag, role}][edge] = &path;
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
  auto& constraint = database.sdc();
  if (constraint.get_clock_map().size() > 1)
    return true;
  for (const auto& exception : constraint.get_path_exception_list()) {
    if (!exception.get_through_list().empty() || (exception.get_type() != TimingExceptionType::kMinDelay && exception.get_setup()))
      return true;
  }
  for (const auto& group : constraint.get_clock_group_list()) {
    if (!group.get_allow_paths())
      return true;
  }
  return false;
}

}  // namespace

void compileSparse(TimingData& snapshot, bool force, bool structural_model, AnalysisType analysis)
{
  Database& database = STADM.getDatabase();
  const bool early = analysis == AnalysisType::kMin;
  auto& view = early ? snapshot.sparse_min : snapshot.sparse;
  if (!force && !needsSparse(database))
    return;
  Monitor monitor;
  STALOG.info(Loc::current(), "Compiling ", early ? "min" : "max", " sparse timing snapshot...");
  view.enabled = true;
  view.early = early;
  if (!early)
    snapshot.schema_version = 3;
  auto& exceptions = database.sdc().get_path_exception_list();
  view.domains = snapshot.clock_names;
  view.domains.push_back("");  // unclocked input domain
  // Freeze the exact native loads for fixed-RC validation. Runtime propagation
  // uses its current physical RC vectors; these values are diagnostic only.
  view.reference_pin_cap.resize(snapshot.pins.size(), {0, 0});
  std::unordered_map<std::string, int32_t> pin_ids;
  pin_ids.reserve(snapshot.pins.size());
  // Connectivity is immutable during compilation. Resolve the top-level role
  // once; scanning a net for every sink edge makes a high-fanout clock quadratic.
  std::unordered_set<std::string> inout_nets;
  for (std::size_t index = 0; index < snapshot.pins.size(); ++index) {
    const auto& pin = snapshot.pins[index];
    pin_ids.emplace(pin.name, index);
    if (!pin.is_port || pin.direction != static_cast<int32_t>(PinDirection::kInout) || inout_nets.contains(pin.net_name))
      continue;
    const auto net = database.network().nets().find(pin.net_name);
    if (net != database.network().nets().end()) {
      const auto& connected = net->second.get_pin_name_list();
      if (std::find(connected.begin(), connected.end(), pin.name) != connected.end())
        inout_nets.insert(pin.net_name);
    }
  }
  DelayCalculator calculator(database, STADM.getConfig());
  for (auto& [name, net] : database.network().nets()) {
    if (!pin_ids.contains(net.get_driver_pin()))
      continue;
    auto& cap = view.reference_pin_cap[pin_ids.at(net.get_driver_pin())];
    cap[0] = calculator.pinLoad(net.get_driver_pin(), analysis, TransType::kRise) * snapshot.capacitance_unit_pf;
    cap[1] = calculator.pinLoad(net.get_driver_pin(), analysis, TransType::kFall) * snapshot.capacitance_unit_pf;
  }
  std::map<std::string, int32_t> domains;
  for (std::size_t index = 0; index < view.domains.size(); ++index)
    domains.emplace(view.domains[index], index);
  view.clock_reached_pins.resize(view.domains.size(), 0);
  view.clock_reached_sequential_pins.resize(view.domains.size(), 0);
  std::set<std::string> sequential_clock_pins;
  for (auto& [name, instance] : database.network().instances()) {
    if (instance.get_is_sequential()) {
      ++view.sequential_instances;
      sequential_clock_pins.insert(instance.get_clock_pin_name());
    }
  }

  ExceptionMatcher matcher(database.sdc(), database.network());
  std::map<std::string, int32_t> tags, launch_tags;
  std::map<std::string, std::map<StateKey, int32_t>> pin_states;
  std::vector<PathPair> paths;
  auto collectStates = [&](auto& point_map, int32_t role) {
    for (auto& [name, point] : point_map) {
      for (auto& [clock, clock_state] : point.get_clock_state_map()) {
        if (domains.contains(clock)) {
          ++view.clock_reached_pins[domains.at(clock)];
          if (sequential_clock_pins.contains(name))
            ++view.clock_reached_sequential_pins[domains.at(clock)];
          if (view.clock_pin_sample.size() < 30)
            view.clock_pin_sample.push_back(clock + ":" + name);
        }
      }
      for (auto& [key, pair] : pathStates(point, role, analysis)) {
        auto* representative = pair[0] != nullptr ? pair[0] : pair[1];
        if (!domains.contains(representative->get_clock_name())) {
          view.unsupported.push_back("unknown launch clock at " + name);
          continue;
        }
        const std::string tag_name = (role == 0 ? "L\x1f" : "D\x1f") + std::get<2>(key);
        auto [tag, inserted] = tags.emplace(tag_name, tags.size());
        if (inserted)
          view.tags.push_back(tag_name);
        SparseTimingState state;
        state.pin = name;
        state.domain = domains.at(std::get<1>(key));
        state.tag = tag->second;
        const auto launch_name = matcher.makePathStateTag(representative->get_clock_name(), representative->get_launch_clock_trans_type(), {},
                                                          representative->get_is_clock_source_path(), representative->crprPathTag(),
                                                          representative->get_launch_clock_edge_index());
        const auto [launch, new_launch] = launch_tags.emplace(launch_name, launch_tags.size());
        if (new_launch)
          view.launch_tags.push_back(launch_name);
        state.launch_tag = launch->second;
        state.source_type = std::get<0>(key);
        state.role = role;
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
        const auto pin = database.network().pins().find(name);
        const auto port_constraint = database.sdc().get_port_constraint_map().find(name);
        if (pin != database.network().pins().end() && pin->second.get_is_port()
            && port_constraint != database.sdc().get_port_constraint_map().end()
            && (early ? port_constraint->second.get_has_input_delay_min() : port_constraint->second.get_has_input_delay_max())
            && state.source_type != 2) {
          state.seed = state.valid;
        }
        pin_states[name].emplace(key, view.states.size());
        view.states.push_back(std::move(state));
        paths.push_back(pair);
      }
    }
  };
  collectStates(database.graph().points(), 0);
  collectStates(database.graph().driverPoints(), 1);
  STALOG.info(Loc::current(), "Sparse ", early ? "min" : "max", " states collected: states=", view.states.size(),
              ", tags=", view.tags.size(), ", launch_tags=", view.launch_tags.size(), "; phase ", monitor.getStatsInfo());
  const auto peak = std::max_element(pin_states.begin(), pin_states.end(), [](const auto& a, const auto& b) {
    return a.second.size() < b.second.size();
  });
  if (peak != pin_states.end()) {
    STALOG.info(Loc::current(), "Sparse highest-state pin: ", peak->first, ", states=", peak->second.size());
    int samples = 0;
    for (const auto& [key, id] : peak->second) {
      STALOG.info(Loc::current(), "Sparse high-state sample: pin=", peak->first, ", clock=", std::get<1>(key), ", tag=", std::get<2>(key));
      if (++samples == 4)
        break;
    }
  }

  if (structural_model) {
    // Structural path states carry no numeric slew from a completed STA run.
    // Copy the prepared electrical boundary into the seed state so sparse
    // propagation evaluates PI/CK arcs with the same LUT input as full STA.
    for (auto& state : view.states) {
      if (!state.seed[0] && !state.seed[1])
        continue;
      const auto pin = pin_ids.find(state.pin);
      if (pin == pin_ids.end())
        continue;
      const auto& electrical = snapshot.pins.at(pin->second);
      if (state.valid[0])
        state.reference[2] = early ? electrical.min_rise_slew_ps : electrical.max_rise_slew_ps;
      if (state.valid[1])
        state.reference[3] = early ? electrical.min_fall_slew_ps : electrical.max_fall_slew_ps;
    }
  }

  // The refactored graph already carries separate driver points for inout
  // pins.  There is no old PR302-only control/electrical-disable flag on Arc;
  // keep the physical role separation from GraphBuilder and do not invent a
  // second disable classification here.
  const std::set<std::string> isolated_slew_pins;
  std::map<std::tuple<int32_t, int32_t, int32_t>, int32_t> isolated_groups;
  int32_t next_slew_group = static_cast<int32_t>(pin_ids.size());
  for (auto& state : view.states) {
    const int32_t pin = pin_ids.at(state.pin);
    if (!isolated_slew_pins.contains(state.pin)) {
      state.slew_group = pin + (state.role == 0 ? 0 : static_cast<int32_t>(pin_ids.size()));
      continue;
    }
    const auto key = std::make_tuple(pin, state.source_type, state.role);
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
  for (auto& arc : database.graph().arcs()) {
    // Three-state OE -> PAD arcs are data-path timing arcs for a top-level
    // inout.  Keep them in the sparse graph so an output path can be driven
    // by the enable transition, while retaining the native disable-arc
    // exclusion for ordinary output ports and internal control arcs.
    const bool inout_control = arc.get_is_disable_arc() && isTopLevelInoutNet(database, inout_nets, arc.get_sink_pin());
    if ((!arc.get_is_disable_arc() || inout_control) && !arc.get_is_loop_disable())
      active.emplace(arc.get_source_pin(), arc.get_sink_pin());
    // New Arc does not expose PR302's electrical-only disable bit.  Such
    // edges are intentionally omitted until the model exposes that
    // distinction explicitly.
  }
  const auto has_driver_role = [&](const std::string& pin) { return database.graph().driverPoints().contains(pin); };
  ExceptionThroughIndex through_index(database.sdc(), database.network());
  auto appendEdges = [&](const auto& base_arcs, auto& target, bool cell) {
    for (std::size_t base = 0; base < base_arcs.size(); ++base) {
      const auto& arc = base_arcs[base];
      if constexpr (std::is_same_v<std::decay_t<decltype(arc)>, TimingGraphArcData>) {
        if (arc.arc_type != 1 || arc.library_arc_id < 0)
          continue;
      }
      const auto sink_pin = database.network().pins().find(arc.sink_pin);
      const auto sink_constraint = database.sdc().get_port_constraint_map().find(arc.sink_pin);
      const bool synthetic_output = !cell && sink_pin != database.network().pins().end() && sink_pin->second.get_is_port()
                                    && sink_constraint != database.sdc().get_port_constraint_map().end()
                                    && (early ? sink_constraint->second.get_has_output_delay_min() : sink_constraint->second.get_has_output_delay_max());
      if (!active.contains({arc.source_pin, arc.sink_pin}) && !synthetic_output)
        continue;
      const auto sources = pin_states.find(arc.source_pin);
      const auto sinks = pin_states.find(arc.sink_pin);
      if (sources == pin_states.end() || sinks == pin_states.end())
        continue;
      const int32_t source_role = !cell && has_driver_role(arc.source_pin) ? 1 : 0;
      const int32_t sink_role = cell && has_driver_role(arc.sink_pin) ? 1 : 0;
      for (const auto& [key, source] : sources->second) {
        if (std::get<3>(key) != source_role)
          continue;
        // The output electrical role of an inout PAD must not feed the PAD's
        // input receiver. OpenSTA keeps these vertices separate; retaining
        // this edge would create a false internal loop and a large bogus
        // arrival at GPIO/SD input endpoints.
        if (cell && isolated_slew_pins.contains(arc.source_pin) && view.states[source].source_type == 2) {
          continue;
        }
        const bool inout_net = !cell && (isBidirectionalCellPin(database, arc.source_pin) || isTopLevelInoutNet(database, inout_nets, arc.sink_pin));
        std::map<int32_t, uint8_t> transitions;
        for (int input = 0; input < 2; ++input) {
          const auto* path = paths[source][input];
          if (path == nullptr)
            continue;
          for (int output = 0; output < 2; ++output) {
            bool legal = input == output;
            if constexpr (std::is_same_v<std::decay_t<decltype(arc)>, TimingGraphArcData>) {
              legal = (arc.sense == 1 ? input == output : arc.sense == -1 ? input != output : true)
                      && (arc.timing_type == 0 || arc.timing_type == (input == 0 ? 1 : -1));
            }
            if (!legal)
              continue;
            const auto progress = matcher.advanceState(through_index, path->get_exception_state_list(), arc.sink_pin,
                                                        output == 0 ? TransType::kRise : TransType::kFall,
                                                        cell ? "" : arc.source_pin);
            StateKey sink_key = key;
            std::get<2>(sink_key) = matcher.makePathStateTag(path->get_clock_name(), path->get_launch_clock_trans_type(), progress,
                                                          path->get_is_clock_source_path(), path->crprPathTag(), path->get_launch_clock_edge_index());
            std::get<3>(sink_key) = sink_role;
            auto sink = sinks->second.find(sink_key);
            if (sink == sinks->second.end())
              continue;
            // CK is a launch seed. Never feed incoming data back into it.
            const auto& destination = view.states[sink->second];
            if (!destination.valid[output] || (!cell && destination.seed[output] && !inout_net))
              continue;
            if (!cell && !structural_model && view.states[source].level >= destination.level)
              continue;
            transitions[sink->second] |= uint8_t{1} << (2 * input + output);
          }
        }
        for (const auto& [sink, mask] : transitions)
          target.push_back({static_cast<int32_t>(base), source, sink, mask});
      }
    }
  };
  // The native timing graph can assign an inout receiver and the first
  // sequential input levels from different subgraphs. The physical receiver
  // net is nevertheless a forward data edge. Promote only nets sourced by a
  // Liberty cell with an input/output port; changing all levels globally
  // would alter unrelated net edges and can introduce false ordering.
  //
  // A three-state control arc has the same role transition at an inout PAD as
  // the physical receiver net. Its native level can be higher than the PAD
  // load-role level because the control path is not part of the ordinary data
  // winner. Promote that cell edge as well so the sparse runtime sees a DAG.
  for (int pass = 0; pass < 2; ++pass) {
    for (auto& arc : database.graph().arcs()) {
      const bool promote_net = arc.get_type() == ArcType::kNet && isBidirectionalCellPin(database, arc.get_source_pin());
      const bool promote_inout_cell = arc.get_type() == ArcType::kCell && isTopLevelInoutNet(database, inout_nets, arc.get_sink_pin());
      if (arc.get_is_loop_disable() || (!promote_net && !promote_inout_cell))
        continue;
      const auto sources = pin_states.find(arc.get_source_pin());
      const auto sinks = pin_states.find(arc.get_sink_pin());
      if (sources == pin_states.end() || sinks == pin_states.end())
        continue;
      const int32_t source_role = has_driver_role(arc.get_source_pin()) ? 1 : 0;
      const int32_t sink_role = arc.get_type() == ArcType::kCell && has_driver_role(arc.get_sink_pin()) ? 1 : 0;
      for (const auto& [key, source] : sources->second) {
        if (std::get<3>(key) != source_role)
          continue;
        StateKey sink_key = key;
        std::get<3>(sink_key) = sink_role;
        auto sink = sinks->second.find(sink_key);
        if (sink == sinks->second.end() && promote_net) {
          for (auto candidate = sinks->second.begin(); candidate != sinks->second.end(); ++candidate) {
            const auto& candidate_key = candidate->first;
            if (std::get<0>(candidate_key) == std::get<0>(sink_key) && std::get<1>(candidate_key) == std::get<1>(sink_key)
                && std::get<3>(candidate_key) == std::get<3>(sink_key)
                && sameLaunchTag(view.tags, view.states[source].tag, view.states[candidate->second].tag)) {
              sink = candidate;
              break;
            }
          }
        }
        if (sink != sinks->second.end())
          view.states[sink->second].level = std::max(view.states[sink->second].level, view.states[source].level + 1);
      }
    }
  }
  appendEdges(snapshot.graph_arcs, view.cell_edges, true);
  appendEdges(snapshot.net_arcs, view.net_edges, false);
  for (const auto& edge : view.net_edges) {
    const auto& arc = snapshot.net_arcs.at(edge.base_arc);
    view.net_threshold_adjustments.push_back(netThresholdAdjustment(database, arc.source_pin, arc.sink_pin));
  }
  appendSparseElectricalEdges(snapshot, database, view);
  STALOG.info(Loc::current(), "Sparse ", early ? "min" : "max", " edges collected: cell=", view.cell_edges.size(),
              ", net=", view.net_edges.size(), ", slew_only=", view.slew_only_cell_edges.size(), "; phase ", monitor.getStatsInfo());

  // Some output ports are represented as virtual timing endpoints with no DEF
  // net row. Their native path arrival is already the launch-side electrical
  // result, so use it as a seed when no sparse net edge feeds the port. A
  // bidirectional PAD with a synthetic PAD -> port edge remains non-seed.
  std::set<int32_t> net_destinations;
  for (const auto& edge : view.net_edges)
    net_destinations.insert(edge.sink);
  for (std::size_t state_id = 0; state_id < view.states.size(); ++state_id) {
    auto& state = view.states[state_id];
    const auto pin = database.network().pins().find(state.pin);
    const auto constraint = database.sdc().get_port_constraint_map().find(state.pin);
    if (pin != database.network().pins().end() && pin->second.get_is_port() && state.source_type == 2
        && constraint != database.sdc().get_port_constraint_map().end()
        && (early ? constraint->second.get_has_output_delay_min() : constraint->second.get_has_output_delay_max())
        && !net_destinations.contains(state_id)) {
      state.seed = state.valid;
    }
  }

  // Min required/check contexts are exported by MinTimingDataExporter. They
  // bind to these exact native states; do not repeat capture selection here.
  if (early)
    return;

  // PathChecker is the current-main owner of capture selection, output-delay
  // handling and exception-aware required time.  Reusing it here keeps sparse
  // export tied to the same check semantics as the complete STA run.
  PathChecker checker(database, calculator);
  TimingStateQuery state_query(database);
  // Borrow identities from the immutable snapshot and retain the first exact
  // match, as the previous linear search did when duplicate rows are present.
  std::unordered_map<EndpointArcKey, int32_t, EndpointArcHash> endpoint_arcs;
  endpoint_arcs.reserve(snapshot.timing_check_arcs.size());
  for (std::size_t index = 0; index < snapshot.timing_check_arcs.size(); ++index) {
    const auto& arc = snapshot.timing_check_arcs[index];
    endpoint_arcs.try_emplace({arc.sink_pin, arc.source_pin, arc.check_type, arc.library_arc_id}, static_cast<int32_t>(index));
  }
  std::set<std::string> endpoints(snapshot.end_points.begin(), snapshot.end_points.end());
  for (std::size_t state_id = 0; state_id < view.states.size(); ++state_id) {
    auto& state = view.states[state_id];
    if (!endpoints.contains(state.pin))
      continue;
    for (int edge = 0; edge < 2; ++edge) {
      if (!state.valid[edge])
        continue;
      const TimingPathState& path = *paths[state_id][edge];
      const auto endpoint = checker.checkPath(path, state.pin, AnalysisType::kMax);
      if (!endpoint.has_value())
        continue;

      const auto& check = endpoint->check();
      const std::string capture = endpoint->captureClock();
      if (!domains.contains(capture)) {
        view.unsupported.push_back("unknown capture clock at " + state.pin);
        continue;
      }

      int32_t endpoint_arc = -1;
      if (!check.capture_pin.empty()) {
        const auto check_type = static_cast<int32_t>(check.type);
        const int32_t selected_library_arc
            = selectedCheckLibraryArcId(database, state.pin, check.capture_pin, check.type, check.capture_pin_transition, check.selected_check_arc_idx);
        const auto arc = endpoint_arcs.find({state.pin, check.capture_pin, check_type, selected_library_arc});
        if (arc != endpoint_arcs.end())
          endpoint_arc = arc->second;
        if (endpoint_arc < 0) {
          view.unsupported.push_back(selected_library_arc < 0 ? "missing selected max check LUT at " + state.pin
                                                              : "missing max check LUT at " + state.pin);
          continue;
        }
      }

      const auto resolved = matcher.resolve(path.get_start_point(), path.get_clock_name(), state.pin, capture, AnalysisType::kMax,
                                            path.get_trans_type(), check.capture_transition, path.get_exception_state_list());
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
      if (chosen != nullptr && !exceptions.empty())
        row.constraint = static_cast<int32_t>(chosen - exceptions.data());
      if (row.valid) {
        row.check_coefficient = check.capture_pin.empty() ? 0.0 : -1.0;
        row.clock_slew_ps = check.capture_pin.empty()
                                ? 0.0
                                : state_query.getClockSlew(check.capture_pin, capture, AnalysisType::kMax, check.capture_pin_transition,
                                                            check.capture_transition)
                                      * snapshot.time_unit_ps;
        if (structural_model && !check.capture_pin.empty()) {
          const auto clock_pin = pin_ids.find(check.capture_pin);
          if (clock_pin != pin_ids.end()) {
            const auto& electrical = snapshot.pins.at(clock_pin->second);
            if (check.capture_pin_transition == TransType::kRise)
              row.clock_slew_ps = electrical.max_rise_slew_ps;
            else if (check.capture_pin_transition == TransType::kFall)
              row.clock_slew_ps = electrical.max_fall_slew_ps;
            else
              row.clock_slew_ps = std::max(electrical.max_rise_slew_ps, electrical.max_fall_slew_ps);
          }
        }
        row.reference_required_ps = endpoint->required() * snapshot.time_unit_ps;
        row.intercept_ps = (endpoint->required() - row.check_coefficient * check.check_time) * snapshot.time_unit_ps;
        if (!std::isfinite(row.intercept_ps))
          view.unsupported.push_back("non-finite required rule at " + state.pin);
      }
      view.checks.push_back(row);
    }
  }
  std::sort(view.unsupported.begin(), view.unsupported.end());
  view.unsupported.erase(std::unique(view.unsupported.begin(), view.unsupported.end()), view.unsupported.end());
  STALOG.info(Loc::current(), "Sparse max required rules ready: checks=", view.checks.size(), "; phase ", monitor.getStatsInfo());
}

void SparseTimingExportAdapter::compile(TimingData& snapshot)
{
  compileSparse(snapshot, false, false, AnalysisType::kMax);
  compileSparse(snapshot, true, false, AnalysisType::kMin);
}

void SparseTimingExportAdapter::compileModel(TimingData& snapshot)
{
  compileSparse(snapshot, true, true, AnalysisType::kMax);
  compileSparse(snapshot, true, true, AnalysisType::kMin);
}

}  // namespace ista
