#include "py_clock_tree.h"

#include "ClockTreeSchema.h"

#include <pybind11/stl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace python_interface {
namespace {

using ecc::geometry::ClockEdgeKind;
using ecc::geometry::ClockEdgeRecord;
using ecc::geometry::ClockEdgeTransition;
using ecc::geometry::ClockNodeKind;
using ecc::geometry::ClockNodeRecord;
using ecc::geometry::ClockRecord;
using ecc::geometry::ClockTimingRecord;
using ecc::geometry::ClockTreeAnalysisSource;
using ecc::geometry::ClockTreeSnapshot;
using ecc::geometry::ClockTreeStage;
using ecc::geometry::ClockTreeStatus;

template <typename T>
T value_or(const pybind11::dict& dict, const char* key, T fallback)
{
  if (!dict.contains(key)) {
    return fallback;
  }
  try {
    return pybind11::cast<T>(dict[key]);
  } catch (const pybind11::error_already_set&) {
    return fallback;
  }
}

std::string string_or(const pybind11::dict& dict, const char* key, const std::string& fallback = {})
{
  return value_or<std::string>(dict, key, fallback);
}

std::optional<double> optional_number(const pybind11::dict& dict, const char* key)
{
  if (!dict.contains(key) || dict[key].is_none()) {
    return std::nullopt;
  }
  try {
    const auto value = pybind11::cast<double>(dict[key]);
    return std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
  } catch (const pybind11::error_already_set&) {
    return std::nullopt;
  }
}

ClockEdgeTransition transition_or(const std::string& value, ClockEdgeTransition fallback)
{
  if (value == "fall") {
    return ClockEdgeTransition::kFall;
  }
  if (value == "rise") {
    return ClockEdgeTransition::kRise;
  }
  return fallback;
}

ClockNodeKind node_kind_for(const std::string& kind, const std::string& role)
{
  if (kind == "root_port" || kind == "source") {
    return ClockNodeKind::kRootPort;
  }
  if (kind == "internal_root") {
    return ClockNodeKind::kInternalRoot;
  }
  if (kind == "sink") {
    return ClockNodeKind::kSink;
  }
  if (kind == "buffer_input") {
    return ClockNodeKind::kBufferInput;
  }
  if (kind == "buffer_output") {
    return ClockNodeKind::kBufferOutput;
  }
  if (kind == "inverter_input") {
    return ClockNodeKind::kInverterInput;
  }
  if (kind == "inverter_output") {
    return ClockNodeKind::kInverterOutput;
  }
  if (kind == "icg_input") {
    return ClockNodeKind::kIcgInput;
  }
  if (kind == "icg_output") {
    return ClockNodeKind::kIcgOutput;
  }
  if (kind == "macro_boundary") {
    return ClockNodeKind::kMacroBoundary;
  }
  if (kind == "other_boundary") {
    return ClockNodeKind::kOtherBoundary;
  }
  if (role == "clock_gate") {
    return kind == "buffer_output" ? ClockNodeKind::kIcgOutput : ClockNodeKind::kIcgInput;
  }
  if (role == "inverter") {
    return kind == "buffer_output" ? ClockNodeKind::kInverterOutput : ClockNodeKind::kInverterInput;
  }
  return kind == "buffer_output" ? ClockNodeKind::kBufferOutput : ClockNodeKind::kBufferInput;
}

ClockTreeStage stage_for(const std::string& stage)
{
  if (stage == "postFloorplan") {
    return ClockTreeStage::kPostFloorplan;
  }
  if (stage == "CTS") {
    return ClockTreeStage::kCts;
  }
  if (stage == "filler") {
    return ClockTreeStage::kFiller;
  }
  return ClockTreeStage::kUnknown;
}

ClockTreeAnalysisSource analysis_source_for(const std::string& source)
{
  if (source == "cts_fast_sta") {
    return ClockTreeAnalysisSource::kCtsFastSta;
  }
  if (source == "ista_typical") {
    return ClockTreeAnalysisSource::kIstaTypical;
  }
  return ClockTreeAnalysisSource::kNone;
}

std::vector<std::string> string_list(const pybind11::dict& dict, const char* key)
{
  std::vector<std::string> result;
  if (!dict.contains(key) || dict[key].is_none()) {
    return result;
  }
  try {
    for (const auto& value : pybind11::cast<pybind11::list>(dict[key])) {
      result.push_back(pybind11::cast<std::string>(value));
    }
  } catch (const pybind11::error_already_set&) {
  }
  return result;
}

std::string join_diagnostics(const std::vector<std::string>& diagnostics)
{
  std::string result;
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.empty()) {
      continue;
    }
    if (!result.empty()) {
      result += "\n";
    }
    result += diagnostic;
  }
  return result;
}

bool read_node_id(const pybind11::dict& dict, std::uint64_t& value)
{
  if (!dict.contains("id") || dict["id"].is_none() || pybind11::isinstance<pybind11::bool_>(dict["id"])) {
    return false;
  }
  try {
    value = pybind11::cast<std::uint64_t>(dict["id"]);
    return true;
  } catch (const pybind11::error_already_set&) {
    return false;
  }
}

bool read_id_value(const pybind11::handle& value, std::uint64_t& result)
{
  if (value.is_none() || pybind11::isinstance<pybind11::bool_>(value)) {
    return false;
  }
  try {
    result = pybind11::cast<std::uint64_t>(value);
    return true;
  } catch (const pybind11::error_already_set&) {
    return false;
  }
}

std::uint64_t fingerprint(const ClockTreeSnapshot& snapshot)
{
  std::uint64_t hash = 1469598103934665603ULL;
  const auto append = [&hash](const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
      hash ^= bytes[index];
      hash *= 1099511628211ULL;
    }
  };
  append(snapshot.clocks.data(), snapshot.clocks.size() * sizeof(ClockRecord));
  append(snapshot.nodes.data(), snapshot.nodes.size() * sizeof(ClockNodeRecord));
  append(snapshot.edges.data(), snapshot.edges.size() * sizeof(ClockEdgeRecord));
  append(snapshot.timing.data(), snapshot.timing.size() * sizeof(ClockTimingRecord));
  append(snapshot.roots.data(), snapshot.roots.size() * sizeof(std::uint32_t));
  append(snapshot.strings.data(), snapshot.strings.size());
  return hash;
}

}  // namespace

std::optional<ClockTreeSnapshot> clockTreeSnapshotFromPython(const pybind11::object& graph, std::uint32_t dbu_per_micron)
{
  if (graph.is_none()) {
    return std::nullopt;
  }
  try {
    const auto data = pybind11::cast<pybind11::dict>(graph);
    if (!data.contains("clocks")) {
      return std::nullopt;
    }
    const auto clocks = pybind11::cast<pybind11::list>(data["clocks"]);

    ClockTreeSnapshot snapshot;
    std::vector<std::string> diagnostics = string_list(data, "diagnostics");
    const auto note = [&diagnostics](std::string message) { diagnostics.push_back(std::move(message)); };

    const std::string stage_name = string_or(data, "stage", "CTS");
    const ClockTreeStage stage = stage_for(stage_name);
    if (stage == ClockTreeStage::kUnknown) {
      note("unknown clock-tree stage: " + stage_name);
    }
    const bool filler = stage == ClockTreeStage::kFiller;
    const std::string default_source = filler ? "ista_typical" : (stage == ClockTreeStage::kCts ? "cts_fast_sta" : "none");
    const std::string source_name = string_or(data, "analysis_source", default_source);
    const ClockTreeAnalysisSource analysis_source = analysis_source_for(source_name);
    if (analysis_source == ClockTreeAnalysisSource::kNone && source_name != "none") {
      note("unknown clock-tree analysis source: " + source_name);
    }

    snapshot.meta.stage = static_cast<std::uint16_t>(stage);
    snapshot.meta.analysis_source = static_cast<std::uint16_t>(analysis_source);
    snapshot.meta.dbu_per_micron = dbu_per_micron;
    snapshot.meta.temperature_c = value_or<double>(data, "temperature_c", filler ? 25.0 : 0.0);
    if (!std::isfinite(snapshot.meta.temperature_c)) {
      note("clock-tree temperature is not finite");
      snapshot.meta.temperature_c = filler ? 25.0 : 0.0;
    }
    const std::string rc_corner = string_or(data, "rc_corner", filler ? "TYPICAL" : "cts_faststa_estimated_rc");
    const std::string liberty_corner = string_or(data, "liberty_corner", filler ? "TYP" : "");
    ecc::geometry::set_string_reference(snapshot, snapshot.meta.rc_corner_offset, snapshot.meta.rc_corner_size,
                                       rc_corner);
    ecc::geometry::set_string_reference(snapshot, snapshot.meta.liberty_corner_offset,
                                       snapshot.meta.liberty_corner_size, liberty_corner);

    std::unordered_map<std::string, std::uint32_t> clock_id_by_name;
    for (std::size_t clock_index = 0U; clock_index < clocks.size(); ++clock_index) {
      const auto clock_dict = pybind11::cast<pybind11::dict>(clocks[clock_index]);
      clock_id_by_name.emplace(string_or(clock_dict, "name", "clock" + std::to_string(clock_index)),
                               static_cast<std::uint32_t>(clock_index));
    }

    for (std::size_t clock_index = 0U; clock_index < clocks.size(); ++clock_index) {
      const auto clock_dict = pybind11::cast<pybind11::dict>(clocks[clock_index]);
      ClockRecord clock;
      clock.clock_id = static_cast<std::uint32_t>(clock_index);
      const auto name = string_or(clock_dict, "name", "clock" + std::to_string(clock_index));
      ecc::geometry::set_string_reference(snapshot, clock.name_offset, clock.name_size, name);
      clock.flags = ecc::geometry::kClockNameValid | ecc::geometry::kClockTopologyValid;
      clock.source_edge = static_cast<std::uint8_t>(
          transition_or(string_or(clock_dict, "source_edge", "rise"), ClockEdgeTransition::kRise));
      clock.node_first = static_cast<std::uint32_t>(snapshot.nodes.size());

      std::vector<std::string> clock_diagnostics = string_list(clock_dict, "diagnostics");
      const auto clock_note = [&clock_diagnostics, &diagnostics, &name](std::string message) {
        diagnostics.push_back(name + ": " + message);
        clock_diagnostics.push_back(std::move(message));
      };
      const std::string singular = string_or(clock_dict, "diagnostic");
      if (!singular.empty()) {
        clock_diagnostics.push_back(singular);
      }

      std::unordered_map<std::uint64_t, std::uint32_t> local_to_global;
      if (clock_dict.contains("nodes")) {
        const auto nodes = pybind11::cast<pybind11::list>(clock_dict["nodes"]);
        local_to_global.reserve(nodes.size());
        for (std::size_t node_index = 0U; node_index < nodes.size(); ++node_index) {
          const auto& value = nodes[node_index];
          const auto node_dict = pybind11::cast<pybind11::dict>(value);
          std::uint64_t local_id = 0U;
          if (!read_node_id(node_dict, local_id)) {
            if (!node_dict.contains("id") || node_dict["id"].is_none()) {
              local_id = static_cast<std::uint64_t>(node_index);
              clock_note("node " + std::to_string(node_index) + " has no id; assigned its list position");
            } else {
              clock_note("node " + std::to_string(node_index) + " has an invalid id; node skipped");
              continue;
            }
          }
          if (local_to_global.contains(local_id)) {
            clock_note("duplicate node id " + std::to_string(local_id) + "; node skipped");
            continue;
          }

          const auto instance_name = string_or(node_dict, "instance_name");
          const auto pin_name = string_or(node_dict, "pin_name");
          const auto kind = string_or(node_dict, "kind");
          const auto role = string_or(node_dict, "cell_role");
          ClockNodeRecord node;
          node.node_id = static_cast<std::uint32_t>(snapshot.nodes.size());
          node.clock_id = clock.clock_id;
          node.kind = static_cast<std::uint8_t>(node_kind_for(kind, role));
          node.owner_index = UINT32_MAX;
          node.x = value_or<std::int32_t>(node_dict, "x_dbu", 0);
          node.y = value_or<std::int32_t>(node_dict, "y_dbu", 0);
          ecc::geometry::set_string_reference(snapshot, node.instance_offset, node.instance_size, instance_name);
          ecc::geometry::set_string_reference(snapshot, node.pin_offset, node.pin_size, pin_name);
          ecc::geometry::set_string_reference(snapshot, node.master_offset, node.master_size,
                                             string_or(node_dict, "master"));
          if (value_or<bool>(node_dict, "position_valid", false)) {
            node.flags |= ecc::geometry::kClockNodePositionValid;
          }
          if (value_or<bool>(node_dict, "sink", false) || node.kind == static_cast<std::uint8_t>(ClockNodeKind::kSink)) {
            node.flags |= ecc::geometry::kClockNodeSink;
            ++clock.sink_count;
          }
          local_to_global.emplace(local_id, node.node_id);
          snapshot.nodes.push_back(node);
        }
      }
      clock.node_count = static_cast<std::uint32_t>(snapshot.nodes.size()) - clock.node_first;

      clock.edge_first = static_cast<std::uint32_t>(snapshot.edges.size());
      if (clock_dict.contains("edges")) {
        const auto edges = pybind11::cast<pybind11::list>(clock_dict["edges"]);
        for (std::size_t edge_index = 0U; edge_index < edges.size(); ++edge_index) {
          const auto& value = edges[edge_index];
          const auto edge_dict = pybind11::cast<pybind11::dict>(value);
          std::uint64_t from_local = 0U;
          std::uint64_t to_local = 0U;
          if (!edge_dict.contains("from") || !read_id_value(edge_dict["from"], from_local)
              || !edge_dict.contains("to") || !read_id_value(edge_dict["to"], to_local)) {
            clock_note("edge " + std::to_string(edge_index) + " has invalid endpoints; edge skipped");
            continue;
          }
          const auto from = local_to_global.find(from_local);
          const auto to = local_to_global.find(to_local);
          if (from == local_to_global.end() || to == local_to_global.end()) {
            clock_note("edge " + std::to_string(edge_index) + " references an unknown node; edge skipped");
            continue;
          }
          if (from->second == to->second) {
            clock_note("edge " + std::to_string(edge_index) + " is a self-loop; edge skipped");
            continue;
          }
          ClockEdgeRecord edge;
          edge.edge_id = static_cast<std::uint32_t>(snapshot.edges.size());
          edge.from_node = from->second;
          edge.to_node = to->second;
          edge.kind = static_cast<std::uint8_t>(value_or<bool>(edge_dict, "cell_arc", false) ? ClockEdgeKind::kCell
                                                                                              : ClockEdgeKind::kNet);
          edge.polarity = static_cast<std::uint8_t>(value_or<bool>(edge_dict, "negative_unate", false)
                                                        ? ClockEdgeTransition::kFall
                                                        : ClockEdgeTransition::kRise);
          ecc::geometry::set_string_reference(snapshot, edge.net_offset, edge.net_size,
                                             string_or(edge_dict, "net_name"));
          ecc::geometry::set_string_reference(snapshot, edge.cell_offset, edge.cell_size,
                                             string_or(edge_dict, "cell_master"));
          snapshot.edges.push_back(edge);
          (void) value_or<std::uint64_t>(edge_dict, "object_ref", 0U);
        }
      }
      clock.edge_count = static_cast<std::uint32_t>(snapshot.edges.size()) - clock.edge_first;

      clock.root_node_first = static_cast<std::uint32_t>(snapshot.roots.size());
      if (clock_dict.contains("roots")) {
        const auto roots = pybind11::cast<pybind11::list>(clock_dict["roots"]);
        for (std::size_t root_index = 0U; root_index < roots.size(); ++root_index) {
          const auto& value = roots[root_index];
          std::uint64_t local = 0U;
          if (!read_id_value(value, local)) {
            clock_note("root " + std::to_string(root_index) + " has an invalid node id; root skipped");
            continue;
          }
          const auto found = local_to_global.find(local);
          if (found == local_to_global.end()) {
            clock_note("root " + std::to_string(root_index) + " references an unknown node; root skipped");
            continue;
          }
          if (std::find(snapshot.roots.begin() + clock.root_node_first, snapshot.roots.end(), found->second)
              != snapshot.roots.end()) {
            continue;
          }
          snapshot.roots.push_back(found->second);
          snapshot.nodes.at(found->second).flags |= ecc::geometry::kClockNodeRoot;
        }
      }
      clock.root_node_count = static_cast<std::uint32_t>(snapshot.roots.size()) - clock.root_node_first;
      clock.flags |= ecc::geometry::kClockRootsValid;

      std::unordered_set<std::uint32_t> nodes_with_timing;
      if (clock_dict.contains("nodes")) {
        const auto nodes = pybind11::cast<pybind11::list>(clock_dict["nodes"]);
        for (std::size_t node_index = 0U; node_index < nodes.size(); ++node_index) {
          const auto& value = nodes[node_index];
          const auto node_dict = pybind11::cast<pybind11::dict>(value);
          std::uint64_t local_id = 0U;
          if (!read_node_id(node_dict, local_id)) {
            local_id = static_cast<std::uint64_t>(node_index);
          }
          const auto node = local_to_global.find(local_id);
          if (node == local_to_global.end() || !node_dict.contains("timing_states")) {
            continue;
          }
          const auto states = pybind11::cast<pybind11::list>(node_dict["timing_states"]);
          for (const auto& state_value : states) {
            const auto state = pybind11::cast<pybind11::dict>(state_value);
            ClockTimingRecord timing;
            timing.node_id = node->second;
            timing.source_edge = static_cast<std::uint8_t>(
                transition_or(string_or(state, "source_edge", "rise"), ClockEdgeTransition::kRise));
            timing.local_edge = static_cast<std::uint8_t>(
                transition_or(string_or(state, "local_edge", "rise"), ClockEdgeTransition::kRise));
            if (state.contains("predecessor") && !state["predecessor"].is_none()) {
              std::uint64_t predecessor = 0U;
              if (!read_id_value(state["predecessor"], predecessor)) {
                clock_note("timing predecessor for node " + std::to_string(local_id) + " is invalid");
              } else if (const auto found = local_to_global.find(predecessor); found != local_to_global.end()) {
                if (found->second == node->second) {
                  clock_note("timing predecessor for node " + std::to_string(local_id)
                             + " is itself; predecessor ignored");
                } else {
                  timing.predecessor_node = found->second;
                  timing.flags |= ecc::geometry::kClockTimingPredecessorValid;
                }
              } else {
                clock_note("timing predecessor for node " + std::to_string(local_id)
                           + " references an unknown node");
              }
            }
            if (state.contains("arrival_ns") && !state["arrival_ns"].is_none()) {
              const auto arrival = optional_number(state, "arrival_ns");
              if (!arrival.has_value() || *arrival < 0.0) {
                clock_note("invalid arrival for node " + std::to_string(local_id));
              } else {
                timing.arrival_ns = *arrival;
                timing.flags |= ecc::geometry::kClockTimingArrivalValid;
                nodes_with_timing.insert(node->second);
              }
            }
            if (state.contains("slew_ns") && !state["slew_ns"].is_none()) {
              const auto slew = optional_number(state, "slew_ns");
              if (!slew.has_value() || *slew < 0.0) {
                clock_note("invalid slew for node " + std::to_string(local_id));
              } else {
                timing.slew_ns = *slew;
                timing.flags |= ecc::geometry::kClockTimingSlewValid;
              }
            }
            if ((timing.flags
                 & (ecc::geometry::kClockTimingArrivalValid | ecc::geometry::kClockTimingSlewValid))
                == 0U) {
              clock_note("timing state for node " + std::to_string(local_id)
                         + " has no valid arrival or slew");
              continue;
            }
            const auto scenario = string_or(state, "analysis", "late") + "/" + string_or(state, "source_edge", "rise") + "->"
                                  + string_or(state, "local_edge", "rise");
            ecc::geometry::set_string_reference(snapshot, timing.scenario_offset, timing.scenario_size, scenario);
            snapshot.timing.push_back(timing);
          }
        }
      }
      clock.flags |= nodes_with_timing.empty() ? 0U : ecc::geometry::kClockTimingValid;
      ecc::geometry::set_string_reference(snapshot, clock.diagnostics_offset, clock.diagnostics_size,
                                         join_diagnostics(clock_diagnostics));
      snapshot.clocks.push_back(clock);
    }

    for (std::size_t clock_index = 0U; clock_index < clocks.size(); ++clock_index) {
      const auto clock_dict = pybind11::cast<pybind11::dict>(clocks[clock_index]);
      const auto master = string_or(clock_dict, "master_clock");
      if (const auto found = clock_id_by_name.find(master); found != clock_id_by_name.end()) {
        snapshot.clocks.at(clock_index).master_clock_id = found->second;
      }
    }

    std::unordered_set<std::uint32_t> timed_nodes;
    for (const auto& timing : snapshot.timing) {
      if (timing.flags & ecc::geometry::kClockTimingArrivalValid) {
        timed_nodes.insert(timing.node_id);
      }
    }
    if (snapshot.clocks.empty()) {
      snapshot.meta.status = static_cast<std::uint16_t>(ClockTreeStatus::kNoClocks);
      if (diagnostics.empty()) {
        diagnostics.emplace_back("no clock networks were found in the stage design");
      }
    } else if (timed_nodes.empty()) {
      snapshot.meta.status = static_cast<std::uint16_t>(ClockTreeStatus::kTopologyOnly);
      if (diagnostics.empty()) {
        diagnostics.emplace_back(filler ? "filler clock graph has no valid iSTA insertion timing"
                                        : "clock graph has no valid insertion timing");
      }
    } else {
      snapshot.meta.status = static_cast<std::uint16_t>(timed_nodes.size() == snapshot.nodes.size()
                                                            ? ClockTreeStatus::kCompleteTiming
                                                            : ClockTreeStatus::kPartialTiming);
    }
    if (diagnostics.empty() && analysis_source == ClockTreeAnalysisSource::kCtsFastSta) {
      diagnostics.emplace_back("Committed CTS FastSTA clock graph; not signoff timing");
    }
    ecc::geometry::set_string_reference(snapshot, snapshot.meta.diagnostics_offset,
                                       snapshot.meta.diagnostics_size, join_diagnostics(diagnostics));
    snapshot.meta.node_count = snapshot.nodes.size();
    snapshot.meta.edge_count = snapshot.edges.size();
    snapshot.meta.timing_count = snapshot.timing.size();
    snapshot.meta.clock_count = snapshot.clocks.size();
    snapshot.meta.root_count = snapshot.roots.size();
    snapshot.meta.producer_fingerprint = fingerprint(snapshot);
    snapshot.meta.string_size = snapshot.strings.size();
    return snapshot;
  } catch (const pybind11::error_already_set&) {
    return std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace python_interface
