// SPDX-License-Identifier: MulanPSL-2.0
#include "PyPlaceDB.h"

#include <unordered_map>
#include <unordered_set>

#include "TimingData.hpp"

namespace python_interface {
namespace {

constexpr int kHoldSchemaVersion = 2;

void appendQualification(pybind11::list& valid, pybind11::list& reasons, const ista::TimingMinQualification& value)
{
  valid.append(pybind11::make_tuple(value.valid[0], value.valid[1]));
  reasons.append(pybind11::make_tuple(static_cast<int>(value.reason[0]), static_cast<int>(value.reason[1])));
}

}  // namespace

void PyPlaceDB::init_min_timing(const ista::TimingData& snapshot)
{
  // Hold has an independent payload contract.  Sparse setup export may use
  // TimingData schema v3, but that must not silently change the min consumer
  // schema version.
  hold_schema_version = kHoldSchemaVersion;
  std::unordered_map<std::string, int32_t> pin_ids;
  std::unordered_map<std::string, const ista::TimingPinData*> timing_pins;
  std::unordered_set<std::string> nets_with_instance_pins;
  for (std::size_t i = 0; i < static_cast<std::size_t>(pin_names.size()); ++i)
    pin_ids.emplace(pin_names[i].cast<std::string>(), static_cast<int32_t>(i));
  for (const auto& pin : snapshot.pins) {
    timing_pins.emplace(pin.name, &pin);
    if (!pin.is_port && !pin.net_name.empty())
      nets_with_instance_pins.insert(pin.net_name);
  }
  for (const auto& name : snapshot.end_points) {
    if (!pin_ids.contains(name) || !timing_pins.contains(name))
      continue;
    const auto& pin = *timing_pins.at(name);
    appendQualification(endpoints_min_valid, endpoints_min_reason, pin.min_qualification);
    backend_endpoint_min_rSlew.append(pin.min_rise_slew_ps);
    backend_endpoint_min_fSlew.append(pin.min_fall_slew_ps);
    min_outcaps_rise.append(pin.min_output_rise_load_pf);
    min_outcaps_fall.append(pin.min_output_fall_load_pf);
  }
  for (const auto& arc : snapshot.timing_check_arcs)
    appendQualification(endpoints_timing_check_min_valid, endpoints_timing_check_min_reason, arc.min_qualification);
  for (const auto& id : start_points) {
    const auto name = pin_names[id.cast<std::size_t>()].cast<std::string>();
    const auto pin = timing_pins.find(name);
    if (pin == timing_pins.end())
      continue;
    min_inrdelays.append(pin->second->min_rise_aat_ps);
    min_infdelays.append(pin->second->min_fall_aat_ps);
    min_inrtrans.append(pin->second->min_rise_slew_ps);
    min_inftrans.append(pin->second->min_fall_slew_ps);
    startpoints_min_valid.append(pybind11::make_tuple(pin->second->min_path_valid[0], pin->second->min_path_valid[1]));
  }

  std::unordered_map<std::string, std::string> clock_outputs;
  for (const auto& arc : snapshot.graph_arcs) {
    if (!arc.is_clock_arc || arc.library_arc_id < 0 || !pin_ids.contains(arc.sink_pin))
      continue;
    const auto sink = timing_pins.find(arc.sink_pin);
    if (sink == timing_pins.end())
      continue;
    const auto current = clock_outputs.find(arc.source_pin);
    if (current == clock_outputs.end()) {
      clock_outputs.emplace(arc.source_pin, arc.sink_pin);
      continue;
    }
    const auto current_pin = timing_pins.find(current->second);
    if ((current_pin == timing_pins.end() || current_pin->second->net_name.empty()) && !sink->second->net_name.empty())
      current->second = arc.sink_pin;
  }
  auto pinId = [&](const std::string& name) {
    const auto it = pin_ids.find(name);
    return it == pin_ids.end() ? -1 : it->second;
  };
  for (const auto& seed : snapshot.min_launches) {
    const auto start = timing_pins.find(seed.start_point);
    if (pinId(seed.start_point) < 0 && start != timing_pins.end()
        && (start->second->net_name.empty()
            || (start->second->is_port && !nets_with_instance_pins.contains(start->second->net_name)))) {
      timing_diagnostics.append("unused min launch outside placement graph: " + seed.start_point);
      continue;
    }
    pybind11::dict row;
    row["start_pin"] = pinId(seed.start_point);
    row["launch_pin"] = pinId(seed.launch_pin);
    row["launch_clock"] = seed.launch_clock;
    row["path_tag"] = seed.path_tag;
    row["source_type"] = seed.source_type;
    row["data_transition"] = seed.data_transition;
    row["launch_transition"] = seed.launch_transition;
    row["arrival_ps"] = seed.arrival_ps;
    row["slew_ps"] = seed.slew_ps;
    row["launch_time_ps"] = seed.launch_time_ps;
    row["launch_slew_ps"] = seed.launch_slew_ps;
    hold_launches.append(row);
  }
  std::size_t omitted_contexts = 0;
  for (const auto& state : snapshot.min_contexts) {
    std::string start = state.start_point;
    if (clock_outputs.contains(start))
      start = clock_outputs.at(start);
    const int32_t start_id = pinId(start);
    const int32_t endpoint_id = pinId(state.end_point);
    if (endpoint_id < 0 || start_id < 0) {
      if (omitted_contexts < 100) {
        timing_diagnostics.append("min timing context omitted: start=" + start + " (id=" + std::to_string(start_id)
                                  + "), endpoint=" + state.end_point + " (id=" + std::to_string(endpoint_id)
                                  + "), source_type=" + std::to_string(state.source_type)
                                  + ", check_type=" + std::to_string(state.check_type));
      }
      ++omitted_contexts;
      continue;
    }
    pybind11::dict row;
    row["start_pin"] = start_id;
    row["native_start_pin"] = pinId(state.start_point);
    row["launch_pin"] = pinId(state.launch_pin);
    row["endpoint_pin"] = endpoint_id;
    row["capture_pin"] = pinId(state.capture_pin);
    row["launch_clock"] = state.launch_clock;
    row["capture_clock"] = state.capture_clock;
    row["path_tag"] = state.path_tag;
    row["source_type"] = state.source_type;
    row["check_type"] = state.check_type;
    row["data_transition"] = state.data_transition;
    row["launch_transition"] = state.launch_transition;
    row["capture_transition"] = state.capture_transition;
    row["capture_pin_transition"] = state.capture_pin_transition;
    row["capture_edge_index"] = state.capture_edge_index;
    row["selected_check_arc_idx"] = state.selected_check_arc_idx;
    row["arrival_ps"] = state.arrival_ps;
    row["required_ps"] = state.required_ps;
    row["normal_required_ps"] = state.normal_required_ps;
    row["data_slew_ps"] = state.data_slew_ps;
    row["launch_time_ps"] = state.launch_time_ps;
    row["launch_clock_arrival_ps"] = state.launch_clock_arrival_ps;
    row["launch_clock_slew_ps"] = state.launch_clock_slew_ps;
    row["capture_edge_ps"] = state.capture_edge_ps;
    row["capture_arrival_ps"] = state.capture_arrival_ps;
    row["capture_slew_ps"] = state.capture_slew_ps;
    row["check_time_ps"] = state.check_time_ps;
    row["uncertainty_ps"] = state.uncertainty_ps;
    row["cppr_ps"] = state.cppr_ps;
    hold_contexts.append(row);
  }
  if (omitted_contexts != 0)
    timing_diagnostics.append("min timing contexts omitted total: " + std::to_string(omitted_contexts));
}

}  // namespace python_interface
