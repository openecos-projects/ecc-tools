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

#include "Utility.hpp"

namespace ircx {

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
      ProcessConductor* conductor = getProcessConductor(corner_data, edge.get_layer_idx());
      if (conductor != nullptr) {
        std::span<EdgeEnvInterval> env_interval_list = net_env.get_edge_interval_list(edge_idx);
        for (EdgeEnvInterval& env_interval : env_interval_list) {
          EdgeEtchInterval edge_interval;
          edge_interval.set_start_coord(env_interval.get_start_coord() * micron_per_dbu);
          edge_interval.set_end_coord(env_interval.get_end_coord() * micron_per_dbu);
          edge_interval.set_center(edge.get_line_segment().get_coord() * micron_per_dbu);
          edge_interval.set_width(edge.get_width() * micron_per_dbu);
          if (env_interval.get_lower_adjacent_edge() != nullptr) {
            edge_interval.set_lower_spacing(
                (env_interval.get_lower_spacing() - edge.get_half_width() - env_interval.get_lower_adjacent_edge()->get_half_width()) * micron_per_dbu);
          }
          if (env_interval.get_upper_adjacent_edge() != nullptr) {
            edge_interval.set_upper_spacing(
                (env_interval.get_upper_spacing() - edge.get_half_width() - env_interval.get_upper_adjacent_edge()->get_half_width()) * micron_per_dbu);
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
  Database& database = RCXDM.getDatabase();
  int32_t corner_num = static_cast<int32_t>(database.get_corner_data_list().size());
  int32_t net_num = database.get_layout_data().get_regular_net_num();
  int32_t thread_num = RCXUTIL.getThreadNum(net_num, RCXDM.getConfig().thread_number);

  for (int32_t corner_idx = 0; corner_idx < corner_num; ++corner_idx) {
#pragma omp parallel for schedule(dynamic) num_threads(thread_num)
    for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
      applyNetEffectiveGeometry(corner_idx, net_idx);
    }
  }
}

void VarProcessor::applyNetEffectiveGeometry(int32_t corner_idx, int32_t net_idx)
{
  Database& database = RCXDM.getDatabase();
  CornerData& corner_data = database.get_corner_data_list()[corner_idx];
  NetEtchProfile& net_etch_profile = database.get_corner_net_etch_profile_pool().get_item(CornerNetIdx(corner_idx, net_idx));
  std::span<TopoEdge> edge_list = database.get_topo_pool().get_net_edge_list(net_idx);

  for (int32_t edge_idx = 0; edge_idx < static_cast<int32_t>(edge_list.size()); ++edge_idx) {
    TopoEdge& edge = edge_list[edge_idx];
    if (edge.get_is_via()) {
      continue;
    }
    ProcessConductor* conductor = getProcessConductor(corner_data, edge.get_layer_idx());
    if (conductor == nullptr) {
      continue;
    }
    std::span<EdgeEtchInterval> edge_interval_list = net_etch_profile.get_edge_interval_list(edge_idx);
    for (EdgeEtchInterval& edge_interval : edge_interval_list) {
      applyEdgeEffectiveGeometry(*conductor, edge_interval);
    }
  }
}

void VarProcessor::applyEdgeEffectiveGeometry(ProcessConductor& conductor, EdgeEtchInterval& edge_interval)
{
  for (ProcessEtchTable& etch_table : conductor.get_etch_table_list()) {
    double lower_etch = 0.0;
    std::optional<double> lower_etch_value = etch_table.get_table().query(edge_interval.get_width(), edge_interval.get_lower_spacing());
    if (lower_etch_value.has_value()) {
      lower_etch = lower_etch_value.value();
    }

    double upper_etch = 0.0;
    std::optional<double> upper_etch_value = etch_table.get_table().query(edge_interval.get_width(), edge_interval.get_upper_spacing());
    if (upper_etch_value.has_value()) {
      upper_etch = upper_etch_value.value();
    }

    edge_interval.set_center(edge_interval.get_center() + 0.5 * lower_etch - 0.5 * upper_etch);
    edge_interval.set_width(edge_interval.get_width() - lower_etch - upper_etch);
  }

  edge_interval.set_thickness(conductor.get_thickness());
}

ProcessConductor* VarProcessor::getProcessConductor(CornerData& corner_data, int32_t design_layer_idx)
{
  LayerTable& layer_table = RCXDM.getDatabase().get_layer_table();
  std::unordered_map<int32_t, std::string>& design_idx_to_name_map = layer_table.get_design_idx_to_name_map();
  if (design_idx_to_name_map.count(design_layer_idx) == 0) {
    return nullptr;
  }

  std::string& design_layer_name = design_idx_to_name_map[design_layer_idx];
  std::unordered_map<std::string, std::string>& design_name_to_process_name_map = layer_table.get_design_name_to_process_name_map();
  if (design_name_to_process_name_map.count(design_layer_name) == 0) {
    return nullptr;
  }

  std::string& process_layer_name = design_name_to_process_name_map[design_layer_name];
  for (ProcessConductor& conductor : corner_data.get_process_conductor_list()) {
    if (conductor.get_layer_name() == process_layer_name) {
      return &conductor;
    }
  }
  return nullptr;
}

}  // namespace ircx
