// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <cmath>
#include <numeric>
#include <tuple>
#include <unordered_map>

#include "PyPlaceDB.h"
#include "TimingData.hpp"

namespace python_interface {
namespace {
namespace py = pybind11;

template <typename T>
py::array_t<T> array(const std::vector<T>& values, int columns = 0)
{
  std::vector<py::ssize_t> shape{static_cast<py::ssize_t>(values.size())};
  if (columns != 0)
    shape = {static_cast<py::ssize_t>(values.size() / columns), columns};
  py::array_t<T> output(shape);
  std::copy(values.begin(), values.end(), output.mutable_data());
  return output;
}

std::string digest(py::dict arrays, py::dict metadata)
{
  auto hash = py::module_::import("hashlib").attr("sha256")();
  auto json = py::module_::import("json");
  hash.attr("update")(json.attr("dumps")(metadata, py::arg("sort_keys") = true).attr("encode")("utf-8"));
  auto names = py::module_::import("builtins").attr("sorted")(arrays);
  for (auto name : names) {
    py::array value = arrays[name].cast<py::array>();
    hash.attr("update")(py::str(name).attr("encode")("utf-8"));
    hash.attr("update")(py::str(value.attr("dtype")).attr("encode")("utf-8"));
    hash.attr("update")(py::str(value.attr("shape")).attr("encode")("utf-8"));
    // Hash the NumPy buffer through Python's buffer protocol. pybind11 2.10's
    // descriptor layout cannot report itemsize reliably with NumPy 2.
    hash.attr("update")(value);
  }
  return hash.attr("hexdigest")().cast<std::string>();
}

}  // namespace

void PyPlaceDB::init_sparse_timing(const ista::TimingData& snapshot)
{
  if (!snapshot.sparse.enabled && !snapshot.sparse_min.enabled)
    return;
  std::unordered_map<std::string, int64_t> pins;
  pins.reserve(pin_names.size());
  for (std::size_t index = 0; index < pin_names.size(); ++index)
    pins.emplace(pin_names[index].cast<std::string>(), index);
  // Snapshot graph IDs and runtime level-order cell IDs are different domains.
  using ArcKey = std::tuple<int64_t, int64_t, int64_t, int64_t, int64_t>;
  std::map<ArcKey, int64_t> cells;
  const std::size_t clock_arc_count = flat_inst_arcs_by_level_start.size() > 1 ? flat_inst_arcs_by_level_start[1].cast<std::size_t>() : 0;
  for (std::size_t index = 0; index < flat_inst_arcs_by_level.size(); ++index) {
    const auto row = flat_inst_arcs_by_level[index].cast<py::tuple>();
    int64_t source = row[0].cast<int64_t>();
    if (index < clock_arc_count) {
      if (source < 0 || static_cast<std::size_t>(source) >= clock_pins.size())
        throw std::runtime_error("sparse clock arc has invalid clock-pin index");
      source = clock_pins[source].cast<int64_t>();
    }
    const ArcKey key{source, row[1].cast<int64_t>(), row[6].cast<int64_t>(), row[4].cast<int64_t>(), row[5].cast<int64_t>()};
    if (!cells.emplace(key, index).second)
      throw std::runtime_error("duplicate sparse base cell arc");
  }
  for (auto [source_view, output] : {std::pair{&snapshot.sparse, &timing_sparse}, std::pair{&snapshot.sparse_min, &timing_min_sparse}}) {
    const auto& view = *source_view;
    auto& timing_sparse = *output;
    if (!view.enabled)
      continue;
    std::vector<int64_t> state_pins(view.states.size(), -1);
    std::vector<int64_t> order;
    order.reserve(view.states.size());
    for (std::size_t index = 0; index < view.states.size(); ++index) {
      const auto pin = pins.find(view.states[index].pin);
      if (pin != pins.end()) {
        state_pins[index] = pin->second;
        order.push_back(index);
      }
    }
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
      const auto& x = view.states[a];
      const auto& y = view.states[b];
      return std::tuple(state_pins[a], x.domain, x.tag, x.source_type) < std::tuple(state_pins[b], y.domain, y.tag, y.source_type);
    });
    std::vector<int64_t> remap(view.states.size(), -1), offsets(pin_names.size() + 1, 0), state_pin, domain, tag, launch_tag, source_type, role, level, slew_group;
    std::vector<uint8_t> valid, seed;
    std::vector<double> reference;
    for (std::size_t index = 0; index < order.size(); ++index) {
      remap[order[index]] = index;
      const auto& state = view.states[order[index]];
      const auto pin = state_pins[order[index]];
      ++offsets[pin + 1];
      state_pin.push_back(pin);
      domain.push_back(state.domain);
      tag.push_back(state.tag);
      launch_tag.push_back(state.launch_tag);
      source_type.push_back(state.source_type);
      role.push_back(state.role);
      level.push_back(state.level);
      slew_group.push_back(state.slew_group);
      for (auto v : state.valid)
        valid.push_back(v);
      for (auto v : state.seed)
        seed.push_back(v);
      reference.insert(reference.end(), state.reference.begin(), state.reference.end());
    }
    std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());
    std::vector<int64_t> cell_edges, slew_edges, net_edges;
    std::vector<int64_t> cell_masks, slew_masks, net_masks;
    auto remapCells = [&](const auto& edges, auto& output, auto& masks) {
      for (const auto& edge : edges) {
        if (remap.at(edge.sink) < 0)
          continue;  // physical export omits unconnected leaves
        if (remap.at(edge.source) < 0)
          throw std::runtime_error("sparse intermediate pin missing from placement: " + view.states[edge.source].pin);
        const auto& arc = snapshot.graph_arcs.at(edge.base_arc);
        const ArcKey key{pins.at(arc.source_pin), pins.at(arc.sink_pin), arc.library_arc_id, arc.sense, arc.timing_type};
        const auto id = cells.find(key);
        if (id == cells.end())
          throw std::runtime_error("sparse base cell arc missing from runtime");
        output.insert(output.end(), {id->second, remap.at(edge.source), remap.at(edge.sink)});
        masks.push_back(edge.transition_mask);
      }
    };
    remapCells(view.cell_edges, cell_edges, cell_masks);
    remapCells(view.slew_only_cell_edges, slew_edges, slew_masks);
    std::vector<double> net_adjustments;
    for (std::size_t index = 0; index < view.net_edges.size(); ++index) {
      const auto& edge = view.net_edges[index];
      if (remap.at(edge.sink) < 0)
        continue;
      if (remap.at(edge.source) < 0)
        throw std::runtime_error("sparse net driver missing from placement: " + view.states[edge.source].pin);
      net_edges.insert(net_edges.end(), {pins.at(snapshot.net_arcs.at(edge.base_arc).sink_pin), remap.at(edge.source), remap.at(edge.sink)});
      net_masks.push_back(edge.transition_mask);
      const auto& adjustment = view.net_threshold_adjustments.at(index);
      net_adjustments.insert(net_adjustments.end(), adjustment.begin(), adjustment.end());
    }
    std::vector<int64_t> check_index;
    std::vector<double> check_values;
    std::vector<int64_t> model_check_values;
    for (const auto& row : view.checks) {
      if (remap.at(row.state) < 0)
        throw std::runtime_error("sparse constrained endpoint missing from placement: " + view.states[row.state].pin);
      check_index.insert(check_index.end(), {remap.at(row.state), row.capture_domain, row.endpoint_arc, row.transition, row.constraint,
                                             static_cast<int64_t>(row.kind), row.valid});
      check_values.insert(check_values.end(), {row.intercept_ps, row.check_coefficient, row.clock_slew_ps, row.reference_required_ps});
      // The structural and model-only paths can produce sub-femtosecond
      // roundoff in the affine intercept. Sign model coefficients at 1e-9 ps
      // resolution so that this numerical noise does not change model identity.
      model_check_values.insert(model_check_values.end(), {
          static_cast<int64_t>(std::llround(row.intercept_ps * 1e9)),
          static_cast<int64_t>(std::llround(row.check_coefficient * 1e9)),
          static_cast<int64_t>(std::llround(row.clock_slew_ps * 1e9))});
    }
    py::dict arrays;
    std::vector<double> pin_cap(pin_names.size() * 2, 0);
    for (std::size_t index = 0; index < snapshot.pins.size(); ++index) {
      const auto pin = pins.find(snapshot.pins[index].name);
      if (pin == pins.end())
        continue;
      const auto& values = view.reference_pin_cap.at(index);
      pin_cap[pin->second * 2] = values[0];
      pin_cap[pin->second * 2 + 1] = values[1];
    }
    arrays["pin_reference_cap"] = array(pin_cap, 2);
    arrays["pin_state_start"] = array(offsets);
    arrays["state_pin"] = array(state_pin);
    arrays["state_domain"] = array(domain);
    arrays["state_tag"] = array(tag);
    arrays["state_launch_tag"] = array(launch_tag);
    arrays["state_source_type"] = array(source_type);
    arrays["state_role"] = array(role);
    arrays["state_level"] = array(level);
    arrays["state_slew_group"] = array(slew_group);
    arrays["state_valid"] = array(valid, 2);
    arrays["state_seed"] = array(seed, 2);
    arrays["state_reference"] = array(reference, 4);
    arrays["cell_edges"] = array(cell_edges, 3);
    arrays["slew_only_cell_edges"] = array(slew_edges, 3);
    arrays["net_edges"] = array(net_edges, 3);
    arrays["cell_transition_mask"] = array(cell_masks);
    arrays["slew_only_cell_transition_mask"] = array(slew_masks);
    arrays["net_transition_mask"] = array(net_masks);
    arrays["net_threshold_adjustments"] = array(net_adjustments, 4);
    arrays["check_index"] = array(check_index, 7);
    arrays["check_values"] = array(check_values, 4);
    std::vector<int64_t> context_state;
    if (view.early) {
      using ContextKey = std::tuple<int64_t, std::string, std::string, int64_t>;
      std::map<ContextKey, int64_t> state_ids;
      for (const auto old_id : order) {
        const auto& state = view.states[old_id];
        if (state.role == 0)
          state_ids.emplace(ContextKey{pins.at(state.pin), view.domains.at(state.domain), view.tags.at(state.tag), state.source_type}, remap.at(old_id));
      }
      for (const auto item : hold_contexts) {
        const auto row = item.cast<py::dict>();
        const ContextKey key{row["endpoint_pin"].cast<int64_t>(), row["launch_clock"].cast<std::string>(),
                             "L\x1f" + row["path_tag"].cast<std::string>(), row["source_type"].cast<int64_t>()};
        const auto state = state_ids.find(key);
        if (state == state_ids.end())
          throw std::runtime_error("min context has no canonical sparse arrival state");
        context_state.push_back(state->second);
      }
    }
    arrays["context_state"] = array(context_state);
    py::dict metadata;
    metadata["format_version"] = 3;
    metadata["analysis"] = view.early ? "min" : "max";
    metadata["excluded_unconnected_states"] = view.states.size() - order.size();
    metadata["domains"] = view.domains;
    metadata["tags"] = view.tags;
    metadata["launch_tags"] = view.launch_tags;
    metadata["unsupported"] = view.unsupported;
    metadata["sequential_instances"] = view.sequential_instances;
    metadata["clock_reached_pins"] = view.clock_reached_pins;
    metadata["clock_reached_sequential_pins"] = view.clock_reached_sequential_pins;
    metadata["clock_pin_sample"] = view.clock_pin_sample;
    metadata["capabilities"] = std::vector<std::string>{view.early ? "sparse_min_v1" : "sparse_max_v1", "native_required_affine_v1", "native_transition_mask_v1"};
    timing_sparse["metadata"] = metadata;
    timing_sparse["arrays"] = arrays;
    timing_sparse["semantic_digest"] = digest(arrays, metadata);
    py::dict model_arrays;
    for (const char* name : {"pin_reference_cap", "pin_state_start", "state_pin", "state_domain", "state_tag", "state_launch_tag", "state_source_type", "state_role",
                             "state_level", "state_slew_group", "state_valid", "state_seed", "cell_edges", "slew_only_cell_edges",
                             "net_edges", "cell_transition_mask", "slew_only_cell_transition_mask", "net_transition_mask", "net_threshold_adjustments", "check_index", "context_state"}) {
      model_arrays[name] = arrays[name];
    }
    model_arrays["check_values"] = array(model_check_values, 3);
    timing_sparse["model_digest"] = digest(model_arrays, metadata);
    py::dict state_arrays;
    for (const char* name :
         {"pin_state_start", "state_pin", "state_domain", "state_tag", "state_launch_tag", "state_source_type", "state_role", "state_slew_group", "state_valid"}) {
      state_arrays[name] = arrays[name];
    }
    py::dict state_metadata;
    state_metadata["domains"] = metadata["domains"];
    state_metadata["tags"] = metadata["tags"];
    state_metadata["launch_tags"] = metadata["launch_tags"];
    timing_sparse["state_key_digest"] = digest(state_arrays, state_metadata);
  }
}

}  // namespace python_interface
