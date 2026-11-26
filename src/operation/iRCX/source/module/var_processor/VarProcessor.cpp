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
#include "VarProcessor.hpp"

#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

#include "Utility.hpp"
#include "ProcessBinding.hpp"
#include "ProcessEvaluator.hpp"
#include "DensityGeometry.hpp"
#include "LayoutMetal.hpp"

namespace ircx {
namespace {
double transverseCenter(TopoEdge& edge)
{
  auto& shape = edge.get_shape();
  return edge.get_line_segment().get_is_horizontal()
      ? RCXUTIL.minY(shape) + (RCXUTIL.maxY(shape) - RCXUTIL.minY(shape)) / 2.0
      : RCXUTIL.minX(shape) + (RCXUTIL.maxX(shape) - RCXUTIL.minX(shape)) / 2.0;
}
}

// public

void VarProcessor::initInst()
{
  if (_vp_instance == nullptr) {
    _vp_instance = new VarProcessor();
  }
}

VarProcessor& VarProcessor::getInst()
{
  if (_vp_instance == nullptr) {
    RCXLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_vp_instance;
}

void VarProcessor::destroyInst()
{
  if (_vp_instance != nullptr) {
    delete _vp_instance;
    _vp_instance = nullptr;
  }
}

// function

void VarProcessor::process()
{
  Monitor monitor;
  RCXLOG.info(Loc::current(), "Starting...");

  buildCornerNetEtchProfilePool();
  applyCornerNetEffectiveGeometryList();

  RCXLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

VarProcessor* VarProcessor::_vp_instance = nullptr;

void VarProcessor::buildCornerNetEtchProfilePool()
{
  Database& database = RCXDM.getDatabase();
  int32_t corner_num = static_cast<int32_t>(database.get_corner_data_list().size());
  int32_t net_num = database.get_layout_data().get_regular_net_num();
  CornerNetPool<NetEtchProfile>& corner_net_etch_profile_pool = database.get_corner_net_etch_profile_pool();
  corner_net_etch_profile_pool.init(corner_num, net_num);

  for (int32_t corner_idx = 0; corner_idx < corner_num; ++corner_idx) {
    int32_t thread_num = RCXUTIL.getThreadNum(net_num, RCXDM.getConfig().thread_number);
#pragma omp parallel for schedule(dynamic) num_threads(thread_num)
    for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
      buildNetEtchProfile(corner_idx, net_idx);
    }
  }
}

void VarProcessor::buildNetEtchProfile(int32_t corner_idx, int32_t net_idx)
{
  Database& database = RCXDM.getDatabase();
  CornerData& corner_data = database.get_corner_data_list()[corner_idx];
  NetEtchProfile& net_etch_profile = database.get_corner_net_etch_profile_pool().get_item(CornerNetIdx(corner_idx, net_idx));
  NetEnv& net_env = database.get_net_env_list()[net_idx];
  std::span<TopoEdge> edge_list = database.get_topo_pool().get_net_edge_list(net_idx);
  double micron_per_dbu = 1 / 1.0 / database.get_layout_data().get_dbu_per_micron();

  for (int32_t edge_idx = 0; edge_idx < static_cast<int32_t>(edge_list.size()); ++edge_idx) {
    TopoEdge& edge = edge_list[edge_idx];
    std::vector<EdgeEtchInterval> edge_interval_list;
    if (!edge.get_is_via()) {
      const ProcessConductor* conductor = getProcessConductor(corner_data, edge.get_layer_idx());
      if (conductor != nullptr) {
        std::span<EdgeEnvInterval> env_interval_list = net_env.get_edge_interval_list(edge_idx);
        for (EdgeEnvInterval& env_interval : env_interval_list) {
          EdgeEtchInterval edge_interval;
          edge_interval.set_start_coord(env_interval.get_start_coord() * micron_per_dbu);
          edge_interval.set_end_coord(env_interval.get_end_coord() * micron_per_dbu);
          edge_interval.set_center(transverseCenter(edge) * micron_per_dbu);
          edge_interval.set_width(edge.get_width() * micron_per_dbu);
          if (env_interval.get_lower_adjacent_edge() != nullptr) {
            edge_interval.set_lower_spacing(
                std::max(0.0, transverseCenter(edge) - transverseCenter(*env_interval.get_lower_adjacent_edge())
                    - (edge.get_width() + env_interval.get_lower_adjacent_edge()->get_width()) / 2.0) * micron_per_dbu);
          }
          if (env_interval.get_upper_adjacent_edge() != nullptr) {
            edge_interval.set_upper_spacing(
                std::max(0.0, transverseCenter(*env_interval.get_upper_adjacent_edge()) - transverseCenter(edge)
                    - (edge.get_width() + env_interval.get_upper_adjacent_edge()->get_width()) / 2.0) * micron_per_dbu);
          }
          edge_interval.set_thickness(conductor->get_thickness());
          edge_interval_list.push_back(std::move(edge_interval));
        }
      }
    }
    net_etch_profile.append_edge_interval_list(std::move(edge_interval_list));
  }
}

void VarProcessor::applyCornerNetEffectiveGeometryList()
{
  auto& database = RCXDM.getDatabase();
  for (std::size_t corner = 0; corner < database.get_corner_data_list().size(); ++corner) {
    const auto result = applyCornerEffectiveGeometry(corner);
    if (!result)
      RCXLOG.error(Loc::current(), "Geometry evaluation failed for corner ", corner, ": ", result.error);
  }
}

ProcessResult<void, std::string> VarProcessor::applyCornerEffectiveGeometry(std::size_t corner_index)
{
  auto& database = RCXDM.getDatabase();
  auto& corner = database.get_corner_data_list()[corner_index];
  const double scale = corner.get_half_node_scale_factor();
  if (!std::isfinite(scale) || scale <= 0)
    return std::string("invalid geometry scale");
  const double unit = scale / database.get_layout_data().get_dbu_per_micron();
  std::vector<WireGeometryInput> wires;
  std::vector<TopoEdge*> edges;
  std::vector<std::span<EdgeEnvInterval>> environments;
  std::unordered_map<const TopoEdge*, std::size_t> indices;
  auto append = [&](TopoEdge& edge, std::span<EdgeEnvInterval> environment) {
    if (edge.get_is_via()) return;
    const auto* conductor = getProcessConductor(corner, edge.get_layer_idx());
    if (!conductor) return;  // Missing bindings are diagnosed by extraction.
    indices.emplace(&edge, wires.size());
    edges.push_back(&edge);
    environments.push_back(environment);
    wires.push_back({conductor, transverseCenter(edge) * unit, edge.get_width() * unit, {}});
  };
  auto& topology = database.get_topo_pool();
  for (int32_t net = 0; net < database.get_layout_data().get_regular_net_num(); ++net) {
    auto edge_list = topology.get_net_edge_list(net);
    for (std::size_t i = 0; i < edge_list.size(); ++i)
      append(edge_list[i], database.get_net_env_list()[net].get_edge_interval_list(i));
  }
  auto& special_edges = topology.get_special_edge_pool();
  auto& special_env = database.get_special_edge_env_list();
  if (special_edges.size() != special_env.size())
    return std::string("missing special-wire environments");
  for (std::size_t i = 0; i < special_edges.size(); ++i) append(special_edges[i], special_env[i]);

  for (std::size_t i = 0; i < wires.size(); ++i) {
    for (const auto& environment : environments[i]) {
      WireGeometrySpan span{environment.get_start_coord() * unit, environment.get_end_coord() * unit};
      for (bool lower : {true, false}) {
        const auto* neighbor = lower ? environment.get_lower_adjacent_edge() : environment.get_upper_adjacent_edge();
        if (!neighbor) continue;
        const auto found = indices.find(neighbor);
        if (found == indices.end()) return std::string("missing process geometry for adjacent wire");
        (lower ? span.lower_neighbor : span.upper_neighbor) = found->second;
      }
      wires[i].spans.push_back(span);
    }
  }
  std::set<int32_t> density_layers;
  for (std::size_t i = 0; i < wires.size(); ++i)
    if (wires[i].conductor->get_thickness_polynomial()) density_layers.insert(edges[i]->get_layer_idx());
  std::map<int32_t, MetalDensity> drawn_density;
  if (!density_layers.empty()) {
    auto metal = collectLayoutMetal(database.get_layout_data(), scale);
    if (!metal) return metal.error;
    for (int32_t layer : density_layers) {
      const auto shapes = metal->find(layer);
      if (shapes == metal->end()) continue;
      auto index = MetalDensity::build(shapes->second);
      if (!index) return index.error;
      drawn_density.emplace(layer, std::move(*index));
    }
  }
  for (auto domain : {ProcessEffectType::kResistance, ProcessEffectType::kCapacitance}) {
    auto result = density_layers.empty() ? evaluateWireGeometryProfiles(wires, domain) : evaluateWireEtchProfiles(wires, domain);
    if (!result) return result.error;
    if (!density_layers.empty()) {
      for (std::size_t i = 0; i < wires.size(); ++i) {
        const auto drawn = drawn_density.find(edges[i]->get_layer_idx());
        auto finalized = finalizeDensityGeometry(wires[i], (*result)[i], edges[i]->get_line_segment().get_is_horizontal(),
            drawn == drawn_density.end() ? nullptr : &drawn->second, nullptr, domain);
        if (!finalized) return finalized.error;
        (*result)[i] = std::move(*finalized);
      }
      const auto valid = validateWireGeometryProfiles(wires, *result, domain);
      if (!valid) return valid.error;
    }
    for (std::size_t i = 0; i < wires.size(); ++i) {
      const auto& edge = *edges[i];
      if (edge.get_is_special_net()) continue;
      auto& profile = database.get_corner_net_etch_profile_pool().get_item({static_cast<int32_t>(corner_index), edge.get_net_idx()});
      auto intervals = profile.get_edge_interval_list(edge.get_edge_idx());
      std::vector<std::vector<EffectiveWireSegment>> pieces(intervals.size());
      for (auto& segment : (*result)[i]) pieces[segment.source_span].push_back(std::move(segment));
      for (std::size_t j = 0; j < intervals.size(); ++j) {
        if (domain == ProcessEffectType::kResistance) intervals[j].set_resistance_segments(std::move(pieces[j]));
        else intervals[j].set_capacitance_segments(std::move(pieces[j]));
      }
    }
  }
  return {};
}

const ProcessConductor* VarProcessor::getProcessConductor(CornerData& corner_data, int32_t design_layer_idx)
{
  return findProcessConductor(corner_data, RCXDM.getDatabase().get_layer_table(), design_layer_idx);
}

}  // namespace ircx
