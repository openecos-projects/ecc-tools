// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "CapExtractor.hpp"

#include "Utility.hpp"

namespace ircx {

// public

void CapExtractor::initInst()
{
  if (_ce_instance == nullptr) {
    _ce_instance = new CapExtractor();
  }
}

CapExtractor& CapExtractor::getInst()
{
  if (_ce_instance == nullptr) {
    RCXLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ce_instance;
}

void CapExtractor::destroyInst()
{
  if (_ce_instance != nullptr) {
    delete _ce_instance;
    _ce_instance = nullptr;
  }
}

// function

void CapExtractor::extract()
{
  Monitor monitor;
  RCXLOG.info(Loc::current(), "Starting...");

  extractCapacitance();

  RCXLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

CapExtractor* CapExtractor::_ce_instance = nullptr;

void CapExtractor::extractCapacitance()
{
  int32_t corner_num = static_cast<int32_t>(RCXDM.getDatabase().get_corner_data_list().size());
  for (int32_t corner_idx = 0; corner_idx < corner_num; ++corner_idx) {
    extractCornerCapacitance(corner_idx);
  }
  RCXDM.getDatabase().get_rc_data().merge_net_coupling_cap_entry_list();
}

void CapExtractor::extractCornerCapacitance(int32_t corner_idx)
{
  int32_t net_num = RCXDM.getDatabase().get_layout_data().get_regular_net_num();
  int32_t thread_num = RCXUTIL.getThreadNum(net_num, RCXDM.getConfig().thread_number);
#pragma omp parallel for schedule(dynamic) num_threads(thread_num)
  for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
    extractNetCapacitance(corner_idx, net_idx);
  }
}

void CapExtractor::extractNetCapacitance(int32_t corner_idx, int32_t net_idx)
{
  std::span<TopoEdge> edge_list = RCXDM.getDatabase().get_topo_pool().get_net_edge_list(net_idx);
  for (int32_t edge_idx = 0; edge_idx < static_cast<int32_t>(edge_list.size()); ++edge_idx) {
    extractEdgeCapacitance(corner_idx, net_idx, edge_idx);
  }
}

void CapExtractor::extractEdgeCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx)
{
  Database& database = RCXDM.getDatabase();
  std::span<TopoEdge> edge_list = database.get_topo_pool().get_net_edge_list(net_idx);
  TopoEdge& edge = edge_list[edge_idx];
  if (edge.get_is_via()) {
    return;
  }

  NetEnv& net_env = database.get_net_env_list()[net_idx];
  NetEtchProfile& net_etch_profile = database.get_corner_net_etch_profile_pool().get_item(CornerNetIdx(corner_idx, net_idx));
  std::span<EdgeEnvInterval> env_interval_list = net_env.get_edge_interval_list(edge_idx);
  std::span<EdgeEtchInterval> etch_interval_list = net_etch_profile.get_edge_interval_list(edge_idx);
  int32_t interval_num = static_cast<int32_t>(std::min(env_interval_list.size(), etch_interval_list.size()));
  for (int32_t interval_idx = 0; interval_idx < interval_num; ++interval_idx) {
    extractEdgeIntervalCapacitance(corner_idx, net_idx, edge_idx, interval_idx);
  }
}

void CapExtractor::extractEdgeIntervalCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, int32_t interval_idx)
{
  Database& database = RCXDM.getDatabase();
  NetEnv& net_env = database.get_net_env_list()[net_idx];
  EdgeEnvInterval& env_interval = net_env.get_edge_interval_list(edge_idx)[interval_idx];
  std::vector<int32_t> coord_list;
  coord_list.push_back(env_interval.get_start_coord());
  coord_list.push_back(env_interval.get_end_coord());
  for (CrossLayerOverlap& cross_layer_overlap : env_interval.get_cross_layer_overlap_list()) {
    coord_list.push_back(std::max(env_interval.get_start_coord(), cross_layer_overlap.get_start_coord()));
    coord_list.push_back(std::min(env_interval.get_end_coord(), cross_layer_overlap.get_end_coord()));
  }
  RCXUTIL.sortAndUnique(coord_list);

  for (int32_t coord_idx = 0; coord_idx + 1 < static_cast<int32_t>(coord_list.size()); ++coord_idx) {
    extractCapacitanceSpan(corner_idx, net_idx, edge_idx, interval_idx, coord_list[coord_idx], coord_list[coord_idx + 1]);
  }
}

void CapExtractor::extractCapacitanceSpan(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, int32_t interval_idx, int32_t start_coord, int32_t end_coord)
{
  if (end_coord <= start_coord) {
    return;
  }

  Database& database = RCXDM.getDatabase();
  CornerData& corner_data = database.get_corner_data_list()[corner_idx];
  std::span<TopoEdge> edge_list = database.get_topo_pool().get_net_edge_list(net_idx);
  TopoEdge& edge = edge_list[edge_idx];
  ProcessConductor* conductor = getProcessConductor(corner_data, edge.get_layer_idx());
  if (conductor == nullptr) {
    return;
  }

  NetEnv& net_env = database.get_net_env_list()[net_idx];
  NetEtchProfile& net_etch_profile = database.get_corner_net_etch_profile_pool().get_item(CornerNetIdx(corner_idx, net_idx));
  EdgeEnvInterval& env_interval = net_env.get_edge_interval_list(edge_idx)[interval_idx];
  EdgeEtchInterval& etch_interval = net_etch_profile.get_edge_interval_list(edge_idx)[interval_idx];
  std::string below_layer_name;
  std::string above_layer_name;
  getCrossLayerName(env_interval.get_cross_layer_overlap_list(), start_coord, end_coord, below_layer_name, above_layer_name);

  CapTableConfig* cap_table_config = getCapTableConfig(corner_data, conductor->get_layer_name(), below_layer_name, above_layer_name);
  if (cap_table_config == nullptr) {
    return;
  }

  double micron_per_dbu = 1 / 1.0 / database.get_layout_data().get_dbu_per_micron();
  double span_length = (end_coord - start_coord) * micron_per_dbu;
  TopoEdge* lower_adjacent_edge = env_interval.get_lower_adjacent_edge();
  TopoEdge* upper_adjacent_edge = env_interval.get_upper_adjacent_edge();
  if (lower_adjacent_edge != nullptr && upper_adjacent_edge != nullptr) {
    double lower_coupling_capacitance = 0.0;
    double lower_ground_capacitance = 0.0;
    double upper_coupling_capacitance = 0.0;
    double upper_ground_capacitance = 0.0;
    getCapacitance(*cap_table_config, etch_interval.get_capacitance_lower_spacing(), lower_coupling_capacitance, lower_ground_capacitance);
    getCapacitance(*cap_table_config, etch_interval.get_capacitance_upper_spacing(), upper_coupling_capacitance, upper_ground_capacitance);
    addGroundCapacitance(corner_idx, net_idx, edge_idx, lower_adjacent_edge, span_length * lower_ground_capacitance);
    addGroundCapacitance(corner_idx, net_idx, edge_idx, upper_adjacent_edge, span_length * upper_ground_capacitance);
    addCouplingCapacitance(corner_idx, net_idx, edge_idx, lower_adjacent_edge, span_length * lower_coupling_capacitance / 2.0);
    addCouplingCapacitance(corner_idx, net_idx, edge_idx, upper_adjacent_edge, span_length * upper_coupling_capacitance / 2.0);
    return;
  }

  if (lower_adjacent_edge != nullptr || upper_adjacent_edge != nullptr) {
    TopoEdge* adjacent_edge = lower_adjacent_edge != nullptr ? lower_adjacent_edge : upper_adjacent_edge;
    double spacing = lower_adjacent_edge != nullptr ? etch_interval.get_capacitance_lower_spacing() : etch_interval.get_capacitance_upper_spacing();
    double coupling_capacitance = 0.0;
    double ground_capacitance = 0.0;
    getCapacitance(*cap_table_config, spacing, coupling_capacitance, ground_capacitance);
    addGroundCapacitance(corner_idx, net_idx, edge_idx, adjacent_edge, 2.0 * span_length * ground_capacitance);
    addCouplingCapacitance(corner_idx, net_idx, edge_idx, adjacent_edge, span_length * coupling_capacitance);
    return;
  }

  double coupling_capacitance = 0.0;
  double ground_capacitance = 0.0;
  getFarthestCapacitance(*cap_table_config, coupling_capacitance, ground_capacitance);
  std::vector<double>& ground_capacitance_list = database.get_rc_data().get_corner_net_ground_capacitance_list(CornerNetIdx(corner_idx, net_idx));
  ground_capacitance_list[edge_idx] += 2.0 * span_length * (coupling_capacitance + ground_capacitance);
}

void CapExtractor::getCrossLayerName(std::vector<CrossLayerOverlap>& cross_layer_overlap_list, int32_t start_coord, int32_t end_coord,
                                     std::string& below_layer_name, std::string& above_layer_name)
{
  below_layer_name = "SUBSTRATE";
  above_layer_name.clear();
  int32_t below_layer_idx = 0;
  int32_t above_layer_idx = -1;
  for (CrossLayerOverlap& cross_layer_overlap : cross_layer_overlap_list) {
    if (cross_layer_overlap.get_start_coord() > start_coord || end_coord > cross_layer_overlap.get_end_coord()) {
      continue;
    }
    if (cross_layer_overlap.get_below_layer_idx() != 0 && (below_layer_idx == 0 || below_layer_idx < cross_layer_overlap.get_below_layer_idx())) {
      below_layer_idx = cross_layer_overlap.get_below_layer_idx();
    }
    if (cross_layer_overlap.get_above_layer_idx() != 0 && cross_layer_overlap.get_above_layer_idx() < above_layer_idx) {
      above_layer_idx = cross_layer_overlap.get_above_layer_idx();
    }
  }

  LayerTable& layer_table = RCXDM.getDatabase().get_layer_table();
  if (below_layer_idx != 0) {
    std::string& design_layer_name = layer_table.get_design_idx_to_name_map()[below_layer_idx];
    below_layer_name = layer_table.get_design_name_to_process_name_map()[design_layer_name];
  }
  if (above_layer_idx != -1) {
    std::string& design_layer_name = layer_table.get_design_idx_to_name_map()[above_layer_idx];
    above_layer_name = layer_table.get_design_name_to_process_name_map()[design_layer_name];
  }
}

void CapExtractor::addGroundCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, TopoEdge* adjacent_edge, double ground_capacitance)
{
  if (ground_capacitance <= 0.0) {
    return;
  }

  std::vector<double>& ground_capacitance_list = RCXDM.getDatabase().get_rc_data().get_corner_net_ground_capacitance_list(CornerNetIdx(corner_idx, net_idx));
  if (adjacent_edge->get_net_idx() == net_idx) {
    ground_capacitance_list[edge_idx] += ground_capacitance / 2.0;
  } else {
    ground_capacitance_list[edge_idx] += ground_capacitance;
  }
}

void CapExtractor::addCouplingCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, TopoEdge* adjacent_edge, double coupling_capacitance)
{
  if (coupling_capacitance <= 0.0) {
    return;
  }

  Database& database = RCXDM.getDatabase();
  if (adjacent_edge->get_is_special_net()) {
    std::vector<double>& ground_capacitance_list = database.get_rc_data().get_corner_net_ground_capacitance_list(CornerNetIdx(corner_idx, net_idx));
    ground_capacitance_list[edge_idx] += coupling_capacitance;
  } else if (adjacent_edge->get_net_idx() != net_idx) {
    int32_t edge_global_idx = database.get_topo_pool().get_edge_idx(net_idx, edge_idx);
    int32_t adjacent_edge_global_idx = database.get_topo_pool().get_edge_idx(adjacent_edge->get_net_idx(), adjacent_edge->get_edge_idx());
    database.get_rc_data().append_net_coupling_cap_entry(net_idx, edge_global_idx, adjacent_edge_global_idx, corner_idx, coupling_capacitance);
  }
}

ProcessConductor* CapExtractor::getProcessConductor(CornerData& corner_data, int32_t design_layer_idx)
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

CapTableConfig* CapExtractor::getCapTableConfig(CornerData& corner_data, std::string& process_layer_name, std::string& below_layer_name,
                                                std::string& above_layer_name)
{
  for (CapTableConfig& cap_table_config : corner_data.get_cap_table_config_list()) {
    CapTableType type = above_layer_name.empty() ? CapTableType::kA : CapTableType::kB;
    if (cap_table_config.get_type() == type && cap_table_config.get_layer_name() == process_layer_name
        && cap_table_config.get_over_layer_name() == below_layer_name && cap_table_config.get_under_layer_name() == above_layer_name) {
      return &cap_table_config;
    }
  }
  return nullptr;
}

void CapExtractor::getCapacitance(CapTableConfig& cap_table_config, double spacing, double& coupling_capacitance, double& ground_capacitance)
{
  std::vector<CapTableEntry>& entry_list = cap_table_config.get_entry_list();
  if (entry_list.empty()) {
    return;
  }
  spacing = std::max(spacing, 0.0);
  if (spacing > entry_list.back().get_spacing()) {
    ground_capacitance = entry_list.back().get_ground_capacitance();
    return;
  }
  if (spacing <= entry_list.front().get_spacing()) {
    coupling_capacitance = entry_list.front().get_coupling_capacitance();
    ground_capacitance = entry_list.front().get_ground_capacitance();
    return;
  }

  for (int32_t entry_idx = 0; entry_idx + 1 < static_cast<int32_t>(entry_list.size()); ++entry_idx) {
    CapTableEntry& first_entry = entry_list[entry_idx];
    CapTableEntry& second_entry = entry_list[entry_idx + 1];
    if (first_entry.get_spacing() <= spacing && spacing <= second_entry.get_spacing()) {
      double spacing_delta = second_entry.get_spacing() - first_entry.get_spacing();
      if (spacing_delta == 0.0) {
        coupling_capacitance = (first_entry.get_coupling_capacitance() + second_entry.get_coupling_capacitance()) / 2.0;
        ground_capacitance = (first_entry.get_ground_capacitance() + second_entry.get_ground_capacitance()) / 2.0;
        return;
      }
      coupling_capacitance
          = first_entry.get_coupling_capacitance()
            + (second_entry.get_coupling_capacitance() - first_entry.get_coupling_capacitance()) * (spacing - first_entry.get_spacing()) / spacing_delta;
      ground_capacitance
          = first_entry.get_ground_capacitance()
            + (second_entry.get_ground_capacitance() - first_entry.get_ground_capacitance()) * (spacing - first_entry.get_spacing()) / spacing_delta;
      return;
    }
  }
  coupling_capacitance = entry_list.back().get_coupling_capacitance();
  ground_capacitance = entry_list.back().get_ground_capacitance();
}

void CapExtractor::getFarthestCapacitance(CapTableConfig& cap_table_config, double& coupling_capacitance, double& ground_capacitance)
{
  std::vector<CapTableEntry>& entry_list = cap_table_config.get_entry_list();
  if (entry_list.empty()) {
    return;
  }
  coupling_capacitance = entry_list.back().get_coupling_capacitance();
  ground_capacitance = entry_list.back().get_ground_capacitance();
}

}  // namespace ircx
