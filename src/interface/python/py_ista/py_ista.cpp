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
#include "py_ista.h"

#include <unordered_map>

#include "py_path_utils.h"
#include "STAInterface.hpp"

namespace python_interface {

bool initStaConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);
void initStaConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map);

bool initSTA(const std::optional<std::filesystem::path>& config, std::map<std::string, std::string>& config_dict)
{
  const std::string config_ = path_or_empty(config);
  std::map<std::string, std::any> config_map;

  bool pass = false;
  pass = config_.empty() ? true : initStaConfigMapByJSON(config_, config_map);
  if (!pass) {
    return false;
  }
  initStaConfigMapByDict(config_dict, config_map);
  STAI.initSTA(config_map);
  return true;
}

bool runSTA()
{
  STAI.runSTA();
  return true;
}

bool prepareTimingModel()
{
  STAI.prepareTimingModel();
  return true;
}

bool exportTimingModel()
{
  // The value is consumed through the PyPlaceDB model-only constructor. Keep
  // this binding as a readiness operation so Python callers can validate the
  // lifecycle without copying a large native snapshot through pybind.
  (void) STAI.exportTimingModel();
  return true;
}

bool extractLib()
{
  STAI.extractLib();
  return true;
}

bool destroySTA()
{
  STAI.destroySTA();
  return true;
}

bool isModelReady()
{
  return STAI.isModelReady();
}

bool isTimingReady()
{
  return STAI.isTimingReady();
}

pybind11::dict staClockGraph()
{
  namespace py = pybind11;

  const auto graph = STAI.exportClockTreeGraph();
  py::dict result;
  result["timing_source"] = "ista";
  result["source_edge"] = "rise";
  py::list diagnostics;
  for (const auto& diagnostic : graph.diagnostics) {
    diagnostics.append(diagnostic);
  }
  result["diagnostics"] = std::move(diagnostics);
  py::list clocks;
  for (const auto& clock : graph.clocks) {
    py::dict item;
    item["name"] = clock.name;
    item["master_clock"] = clock.master_clock_name;
    item["source_edge"] = clock.source_edge;
    item["timing_available"] = clock.timing_available;
    py::list clock_diagnostics;
    for (const auto& diagnostic : clock.diagnostics) {
      clock_diagnostics.append(diagnostic);
    }
    item["diagnostics"] = std::move(clock_diagnostics);
    py::list nodes;
    std::unordered_map<std::string, std::uint64_t> node_id_by_name;
    node_id_by_name.reserve(clock.nodes.size());
    for (std::size_t index = 0U; index < clock.nodes.size(); ++index) {
      const auto& node = clock.nodes[index];
      py::dict entry;
      entry["id"] = static_cast<std::uint64_t>(index);
      entry["name"] = node.name;
      entry["instance_name"] = node.instance_name;
      entry["pin_name"] = node.pin_name;
      entry["master"] = node.master;
      entry["kind"] = node.kind;
      entry["cell_role"] = node.cell_role;
      entry["x_dbu"] = node.x_dbu;
      entry["y_dbu"] = node.y_dbu;
      entry["position_valid"] = node.position_valid;
      entry["sink"] = node.sink;
      nodes.append(std::move(entry));
      node_id_by_name.emplace(node.name, static_cast<std::uint64_t>(index));
    }
    for (std::size_t index = 0U; index < clock.nodes.size(); ++index) {
      const auto& node = clock.nodes[index];
      py::list states;
      for (const auto& state : node.timing_states) {
        py::dict timing;
        timing["analysis"] = state.analysis;
        timing["source_edge"] = state.source_edge;
        timing["local_edge"] = state.local_edge;
        const auto predecessor = node_id_by_name.find(state.predecessor);
        timing["predecessor"] = predecessor == node_id_by_name.end() ? py::none() : py::cast(predecessor->second);
        timing["arrival_ns"] = state.arrival_ns ? py::cast(*state.arrival_ns) : py::none();
        timing["slew_ns"] = state.slew_ns ? py::cast(*state.slew_ns) : py::none();
        states.append(std::move(timing));
      }
      py::cast<py::dict>(nodes[index])["timing_states"] = std::move(states);
    }
    item["nodes"] = std::move(nodes);
    py::list roots;
    for (const auto& root : clock.roots) {
      const auto found = node_id_by_name.find(root);
      if (found != node_id_by_name.end()) {
        roots.append(found->second);
      }
    }
    item["roots"] = std::move(roots);
    py::list edges;
    for (const auto& edge : clock.edges) {
      const auto from = node_id_by_name.find(edge.from);
      const auto to = node_id_by_name.find(edge.to);
      if (from == node_id_by_name.end() || to == node_id_by_name.end()) {
        continue;
      }
      py::dict entry;
      entry["from"] = from->second;
      entry["to"] = to->second;
      entry["cell_arc"] = edge.cell_arc;
      entry["negative_unate"] = edge.negative_unate;
      entry["net_name"] = edge.net_name;
      entry["cell_master"] = edge.cell_master;
      edges.append(std::move(entry));
    }
    item["edges"] = std::move(edges);
    clocks.append(std::move(item));
  }
  result["clocks"] = std::move(clocks);
  return result;
}

}  // namespace python_interface
