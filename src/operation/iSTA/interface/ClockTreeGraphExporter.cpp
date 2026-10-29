// SPDX-License-Identifier: MulanPSL-2.0
#include "ClockTreeGraphExporter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ClockNetworkBuilder.hpp"
#include "Database.hpp"
#include "common/AnalysisType.hpp"
#include "common/TransType.hpp"
#include "graph/Arc.hpp"
#include "graph/ArcType.hpp"
#include "graph/ClockState.hpp"
#include "graph/Graph.hpp"
#include "graph/TimingPoint.hpp"
#include "liberty/LibertyCell.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "liberty/LibertyUnits.hpp"
#include "liberty/TimingArc.hpp"
#include "liberty/TimingArcSense.hpp"
#include "liberty/TimingCellArc.hpp"
#include "network/Instance.hpp"
#include "network/Network.hpp"
#include "network/Pin.hpp"

namespace ista {
namespace {

constexpr AnalysisType kAnalysis = AnalysisType::kMax;
constexpr TransType kSourceEdge = TransType::kRise;

std::string lower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string edgeName(TransType edge)
{
  return edge == TransType::kFall ? "fall" : "rise";
}

double toNs(double value, TimingTimeUnit unit)
{
  switch (unit) {
    case TimingTimeUnit::kPS:
      return value * 1.0e-3;
    case TimingTimeUnit::kFS:
      return value * 1.0e-6;
    case TimingTimeUnit::kNS:
    case TimingTimeUnit::kNone:
    default:
      return value;
  }
}

std::string nodeName(const Pin& pin)
{
  return pin.get_is_port() ? pin.get_pin_name() : pin.get_instance_name() + "/" + pin.get_pin_name();
}

const TimingPoint* timingPointFor(const Graph& graph, const Pin& pin)
{
  if (pin.get_direction() == PinDirection::kOutput || pin.get_direction() == PinDirection::kInout) {
    const auto driver = graph.driverPoints().find(pin.get_full_name());
    if (driver != graph.driverPoints().end()) {
      return &driver->second;
    }
  }
  const auto point = graph.points().find(pin.get_full_name());
  if (point != graph.points().end()) {
    return &point->second;
  }
  const auto driver = graph.driverPoints().find(pin.get_full_name());
  return driver == graph.driverPoints().end() ? nullptr : &driver->second;
}

const ClockState* clockStateFor(const Graph& graph, const Pin& pin, const std::string& clock)
{
  const TimingPoint* point = timingPointFor(graph, pin);
  return point == nullptr ? nullptr : point->find_clock_state(clock);
}

bool isInverter(const std::string& master)
{
  const std::string name = lower(master);
  return name.find("inv") != std::string::npos || name.find("inverter") != std::string::npos;
}

bool isBuffer(const std::string& master)
{
  const std::string name = lower(master);
  return name.find("buf") != std::string::npos || name.find("buffer") != std::string::npos;
}

bool isClockGate(const Database& database, const Instance* instance)
{
  if (instance != nullptr && instance->get_is_clock_gating()) {
    return true;
  }
  const auto cell = database.liberty().get_cell_map().find(instance == nullptr ? std::string{} : instance->get_cell_name());
  return cell != database.liberty().get_cell_map().end() && cell->second.get_is_clock_gating();
}

std::string nodeKind(const Database& database, const Pin& pin, bool root, bool sink)
{
  if (root) {
    return pin.get_is_port() ? "root_port" : "internal_root";
  }
  if (sink) {
    return "sink";
  }
  if (pin.get_is_port()) {
    return "other_boundary";
  }

  const auto instance = database.network().instances().find(pin.get_instance_name());
  if (instance == database.network().instances().end()) {
    return "other_boundary";
  }
  const std::string& master = instance->second.get_cell_name();
  const bool output = pin.get_direction() == PinDirection::kOutput || pin.get_direction() == PinDirection::kInout;
  if (isClockGate(database, &instance->second)) {
    return output ? "icg_output" : "icg_input";
  }
  if (isInverter(master)) {
    return output ? "inverter_output" : "inverter_input";
  }
  if (isBuffer(master)) {
    return output ? "buffer_output" : "buffer_input";
  }
  const auto cell = database.liberty().get_cell_map().find(master);
  if (cell != database.liberty().get_cell_map().end() && cell->second.get_is_macro()) {
    return "macro_boundary";
  }
  return "other_boundary";
}

std::string cellRole(const Database& database, const Pin& pin)
{
  const auto instance = database.network().instances().find(pin.get_instance_name());
  if (instance == database.network().instances().end()) {
    return {};
  }
  const std::string& master = instance->second.get_cell_name();
  if (isClockGate(database, &instance->second)) {
    return "clock_gate";
  }
  if (isInverter(master)) {
    return "inverter";
  }
  if (isBuffer(master)) {
    return "buffer";
  }
  const auto cell = database.liberty().get_cell_map().find(master);
  return cell != database.liberty().get_cell_map().end() && cell->second.get_is_macro() ? "macro" : "other";
}

bool isNegativeUnate(const Database& database, const Arc& arc)
{
  const auto* cell_arc = arc.libertyArcId() ? database.liberty().findCellArc(*arc.libertyArcId()) : nullptr;
  if (cell_arc == nullptr) {
    return false;
  }
  for (const TimingArc& timing_arc : cell_arc->get_timing_arc_list()) {
    if (timing_arc.get_sense() == TimingArcSense::kNegative) {
      return true;
    }
  }
  return false;
}

std::string netNameForArc(const Network& network, const Arc& arc)
{
  if (arc.get_type() == ArcType::kCell) {
    return {};
  }
  const auto sink = network.pins().find(arc.get_sink_pin());
  if (sink != network.pins().end() && !sink->second.get_net_name().empty()) {
    return sink->second.get_net_name();
  }
  const auto source = network.pins().find(arc.get_source_pin());
  return source == network.pins().end() ? std::string{} : source->second.get_net_name();
}

std::string cellMasterForArc(const Database& database, const Arc& arc)
{
  const auto source = database.network().pins().find(arc.get_source_pin());
  if (source == database.network().pins().end() || source->second.get_instance_name().empty()) {
    return {};
  }
  const auto instance = database.network().instances().find(source->second.get_instance_name());
  return instance == database.network().instances().end() ? std::string{} : instance->second.get_cell_name();
}

std::optional<double> finiteNs(const std::optional<double>& value, TimingTimeUnit unit)
{
  if (!value.has_value() || !std::isfinite(*value)) {
    return std::nullopt;
  }
  const double converted = toNs(*value, unit);
  return std::isfinite(converted) ? std::optional<double>{converted} : std::nullopt;
}

bool containsName(const std::vector<std::string>& sorted_names, const std::string& name)
{
  return std::binary_search(sorted_names.begin(), sorted_names.end(), name);
}

}  // namespace

ClockTreeGraphData exportClockTreeGraph(const Database& database)
{
  ClockTreeGraphData result;
  if (database.network().pins().empty() || database.graph().points().empty()) {
    result.diagnostics.emplace_back("iSTA clock graph is unavailable because no timing graph was built");
    return result;
  }

  Database& mutable_database = const_cast<Database&>(database);
  const std::vector<ClockSource> sources = ClockNetworkBuilder(mutable_database).build();
  const TimingTimeUnit time_unit = database.liberty().get_time_unit();

  for (const ClockSource& source : sources) {
    ClockTreeClockData exported;
    exported.name = std::string(source.get_clock_name());
    const auto definition = database.sdc().get_clock_map().find(exported.name);
    if (definition != database.sdc().get_clock_map().end() && definition->second.get_is_generated()) {
      exported.master_clock_name = definition->second.get_master_clock_name();
    }

    std::vector<std::string> point_names;
    point_names.reserve(source.get_clock_point_list().size());
    for (const std::string& name : source.get_clock_point_list()) {
      if (database.network().pins().contains(name)) {
        point_names.push_back(name);
      }
    }
    std::sort(point_names.begin(), point_names.end());
    point_names.erase(std::unique(point_names.begin(), point_names.end()), point_names.end());
    if (point_names.empty()) {
      exported.diagnostics.emplace_back("clock has no annotated graph points");
      result.diagnostics.emplace_back(exported.name + ": clock has no annotated graph points");
      result.clocks.push_back(std::move(exported));
      continue;
    }

    std::unordered_map<std::string, std::size_t> node_by_pin;
    node_by_pin.reserve(point_names.size());
    std::set<std::string> root_pins;
    for (const std::string& root : source.get_source_list()) {
      root_pins.insert(root);
    }

    std::set<std::string> sink_pins;
    for (const std::string& name : point_names) {
      if (ClockNetworkBuilder(mutable_database).stopsAt(name)) {
        sink_pins.insert(name);
      }
    }

    std::vector<ClockTreeEdgeData> edges;
    for (const Arc& arc : database.graph().arcs()) {
      if (!containsName(point_names, arc.get_source_pin()) || !containsName(point_names, arc.get_sink_pin())) {
        continue;
      }
      if (arc.get_source_pin() == arc.get_sink_pin()) {
        continue;
      }
      ClockTreeEdgeData edge;
      edge.from = nodeName(database.network().pins().at(arc.get_source_pin()));
      edge.to = nodeName(database.network().pins().at(arc.get_sink_pin()));
      edge.cell_arc = arc.get_type() == ArcType::kCell;
      edge.negative_unate = edge.cell_arc && isNegativeUnate(database, arc);
      edge.net_name = netNameForArc(database.network(), arc);
      edge.cell_master = edge.cell_arc ? cellMasterForArc(database, arc) : std::string{};
      edges.push_back(std::move(edge));
    }
    std::sort(edges.begin(), edges.end(), [](const ClockTreeEdgeData& lhs, const ClockTreeEdgeData& rhs) {
      return std::tie(lhs.from, lhs.to, lhs.cell_arc, lhs.net_name, lhs.cell_master)
             < std::tie(rhs.from, rhs.to, rhs.cell_arc, rhs.net_name, rhs.cell_master);
    });
    edges.erase(std::unique(edges.begin(), edges.end(), [](const ClockTreeEdgeData& lhs, const ClockTreeEdgeData& rhs) {
                  return lhs.from == rhs.from && lhs.to == rhs.to && lhs.cell_arc == rhs.cell_arc && lhs.net_name == rhs.net_name;
                }),
                edges.end());

    std::map<std::string, std::size_t> edge_indegree;
    for (const ClockTreeEdgeData& edge : edges) {
      ++edge_indegree[edge.to];
    }
    if (root_pins.empty()) {
      for (const std::string& name : point_names) {
        if (!edge_indegree.contains(nodeName(database.network().pins().at(name)))) {
          root_pins.insert(name);
        }
      }
    }

    double root_arrival = std::numeric_limits<double>::infinity();
    bool any_timing = false;
    std::vector<const ClockState*> states;
    states.reserve(point_names.size());
    for (const std::string& name : point_names) {
      const Pin& pin = database.network().pins().at(name);
      const ClockState* state = clockStateFor(database.graph(), pin, exported.name);
      states.push_back(state);
      const auto arrival = state == nullptr ? std::nullopt : finiteNs(state->waveform(kSourceEdge) != nullptr
                                                                         ? state->waveform(kSourceEdge)->physicalArrival(kAnalysis, kSourceEdge)
                                                                         : std::nullopt,
                                                                     time_unit);
      if (arrival.has_value()) {
        any_timing = true;
        if (root_pins.contains(name)) {
          root_arrival = std::min(root_arrival, *arrival);
        }
      }
    }
    if (!std::isfinite(root_arrival)) {
      root_arrival = 0.0;
    }

    for (std::size_t index = 0; index < point_names.size(); ++index) {
      const std::string& pin_key = point_names[index];
      const Pin& pin = database.network().pins().at(pin_key);
      const auto instance = database.network().instances().find(pin.get_instance_name());
      ClockTreeNodeData node;
      node.name = nodeName(pin);
      node.instance_name = pin.get_instance_name();
      node.pin_name = pin.get_pin_name();
      node.master = instance == database.network().instances().end() ? std::string{} : instance->second.get_cell_name();
      const bool root = root_pins.contains(pin_key);
      const bool sink = sink_pins.contains(pin_key);
      node.kind = nodeKind(database, pin, root, sink);
      node.cell_role = cellRole(database, pin);
      node.position_valid = pin.get_is_port() || pin.get_x() != 0.0 || pin.get_y() != 0.0;
      node.x_dbu = static_cast<std::int32_t>(std::llround(pin.get_x()));
      node.y_dbu = static_cast<std::int32_t>(std::llround(pin.get_y()));
      node.sink = sink;

      const ClockState* state = states[index];
      if (state != nullptr) {
        const ClockWaveform* waveform = state->waveform(kSourceEdge);
        if (waveform != nullptr) {
          for (TransType local : {TransType::kRise, TransType::kFall}) {
            const auto arrival = finiteNs(waveform->physicalArrival(kAnalysis, local), time_unit);
            const auto slew = finiteNs(waveform->physicalSlew(kAnalysis, local), time_unit);
            if (!arrival.has_value() && !slew.has_value()) {
              continue;
            }
            ClockTreeTimingStateData timing;
            timing.analysis = "late";
            timing.source_edge = edgeName(kSourceEdge);
            timing.local_edge = edgeName(local);
            const auto predecessor = waveform->physical_predecessor_map.find(kAnalysis);
            if (predecessor != waveform->physical_predecessor_map.end()) {
              const auto name = predecessor->second.find(local);
              if (name != predecessor->second.end()) {
                const auto pin = database.network().pins().find(name->second);
                timing.predecessor = pin == database.network().pins().end() ? name->second : nodeName(pin->second);
              }
            }
            if (arrival.has_value()) {
              timing.arrival_ns = std::max(0.0, *arrival - root_arrival);
            }
            if (slew.has_value()) {
              timing.slew_ns = std::max(0.0, *slew);
            }
            node.timing_states.push_back(std::move(timing));
          }
        }
      }
      exported.timing_available = exported.timing_available || !node.timing_states.empty();
      node_by_pin.emplace(pin_key, index);
      exported.nodes.push_back(std::move(node));
    }
    exported.edges = std::move(edges);
    for (const std::string& root : root_pins) {
      if (node_by_pin.contains(root)) {
        exported.roots.push_back(nodeName(database.network().pins().at(root)));
      }
    }
    std::sort(exported.roots.begin(), exported.roots.end());
    if (exported.roots.empty()) {
      for (std::size_t index = 0; index < exported.nodes.size(); ++index) {
        if (exported.nodes[index].kind == "root_port" || exported.nodes[index].kind == "internal_root") {
          exported.roots.push_back(exported.nodes[index].name);
        }
      }
    }
    if (!any_timing || !exported.timing_available) {
      exported.diagnostics.emplace_back("clock source-rise late physical timing is unavailable");
      result.diagnostics.emplace_back(exported.name + ": source-rise late physical timing is unavailable");
    }
    result.clocks.push_back(std::move(exported));
  }
  return result;
}

}  // namespace ista
