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
#include "py_icts.h"

#include <algorithm>

#include "CTSAPI.hh"

namespace python_interface {
bool CtsAutoRun(const std::filesystem::path& cts_config, const std::filesystem::path& cts_work_dir)
{
  return CTS_API_INST.runCTS(cts_config.string(), cts_work_dir.string()).ok();
}

bool CtsReport(const std::filesystem::path& path)
{
  return CTS_API_INST.report(path.string()).ok();
}

bool CtsDestroy()
{
  return CTS_API_INST.destroyCTS().ok();
}

pybind11::dict CtsTimingFeature()
{
  namespace py = pybind11;

  const auto clocks = icts::CTSAPI::outputClockTiming();
  py::dict result;
  result["schema_version"] = 1;
  result["analysis_stage"] = "cts_fast_sta_post_optimization";
  result["availability"] = clocks.empty() ? "unavailable" : "available";
  result["clock_count"] = clocks.size();

  py::list clock_list;
  double worst_optimized_skew_ns = 0.0;
  double worst_max_insertion_latency_ns = 0.0;
  std::size_t target_unmet_count = 0U;
  for (const auto& clock : clocks) {
    py::dict clock_dict;
    clock_dict["clock"] = clock.clock;
    clock_dict["sink_count"] = clock.sink_count;
    clock_dict["target_skew_ns"] = clock.target_skew_ns;
    clock_dict["initial_skew_ns"] = clock.initial_skew_ns;
    clock_dict["optimized_skew_ns"] = clock.optimized_skew_ns;
    clock_dict["min_insertion_latency_ns"] = clock.min_insertion_latency_ns;
    clock_dict["max_insertion_latency_ns"] = clock.max_insertion_latency_ns;
    clock_dict["mean_insertion_latency_ns"] = clock.mean_insertion_latency_ns;
    clock_dict["target_met"] = clock.target_met;
    clock_list.append(clock_dict);
    worst_optimized_skew_ns = std::max(worst_optimized_skew_ns, clock.optimized_skew_ns);
    worst_max_insertion_latency_ns = std::max(worst_max_insertion_latency_ns, clock.max_insertion_latency_ns);
    target_unmet_count += clock.target_met ? 0U : 1U;
  }
  result["clocks"] = clock_list;
  if (!clocks.empty()) {
    result["worst_optimized_skew_ns"] = worst_optimized_skew_ns;
    result["worst_max_insertion_latency_ns"] = worst_max_insertion_latency_ns;
    result["target_unmet_count"] = target_unmet_count;
  }
  return result;
}

pybind11::dict CtsClockGraph()
{
  namespace py = pybind11;

  const auto graph = icts::CTSAPI::outputClockGraph();
  py::dict result;
  result["timing_source"] = graph.timing_source;
  result["arrival_reference"] = graph.arrival_reference;
  py::list clocks;
  for (const auto& network : graph.clocks) {
    py::dict clock;
    clock["name"] = network.name;
    clock["net_name"] = network.net_name;
    clock["timing_available"] = network.timing_available;
    clock["diagnostic"] = network.diagnostic;
    py::list roots;
    for (const auto root : network.roots) {
      roots.append(root);
    }
    clock["roots"] = std::move(roots);
    py::list nodes;
    for (const auto& node : network.nodes) {
      py::dict item;
      item["id"] = node.id;
      item["name"] = node.name;
      item["instance_name"] = node.instance_name;
      item["pin_name"] = node.pin_name;
      item["master"] = node.master;
      item["kind"] = node.kind;
      item["cell_role"] = node.cell_role;
      item["x_dbu"] = node.x_dbu;
      item["y_dbu"] = node.y_dbu;
      item["position_valid"] = node.position_valid;
      item["sink"] = node.sink;
      item["inactive"] = node.inactive;
      item["arrival_ns"] = node.arrival_valid ? py::cast(node.arrival_ns) : py::none();
      item["slew_ns"] = node.slew_valid ? py::cast(node.slew_ns) : py::none();
      py::list states;
      for (const auto& state : node.timing_states) {
        py::dict timing;
        timing["analysis"] = state.analysis;
        timing["source_edge"] = state.source_edge;
        timing["local_edge"] = state.local_edge;
        timing["predecessor"] = state.predecessor;
        timing["arrival_ns"] = state.arrival_valid ? py::cast(state.arrival_ns) : py::none();
        timing["slew_ns"] = state.slew_valid ? py::cast(state.slew_ns) : py::none();
        states.append(std::move(timing));
      }
      item["timing_states"] = std::move(states);
      nodes.append(std::move(item));
    }
    clock["nodes"] = std::move(nodes);
    py::list edges;
    for (const auto& edge : network.edges) {
      py::dict item;
      item["from"] = edge.from;
      item["to"] = edge.to;
      item["cell_arc"] = edge.cell_arc;
      item["negative_unate"] = edge.negative_unate;
      item["net_name"] = edge.net_name;
      item["cell_master"] = edge.cell_master;
      edges.append(std::move(item));
    }
    clock["edges"] = std::move(edges);
    clocks.append(std::move(clock));
  }
  result["clocks"] = std::move(clocks);
  return result;
}

}  // namespace python_interface
