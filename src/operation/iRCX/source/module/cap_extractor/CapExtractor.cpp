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

#include "PatternCrossLayers.hpp"
#include "GridSides.hpp"
#include "Utility.hpp"
#include "ProcessBinding.hpp"

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

  auto& database = RCXDM.getDatabase();

  // These former switches cannot select an alternate calculation anymore.
  const char* mode = std::getenv("IRCX_MULTI_MODE");
  if (mode && std::string(mode) != "grid")
    RCXLOG.error(Loc::current(), "only PatternGrid capacitance calculation is supported; remove IRCX_MULTI_MODE");
  const char* rank = std::getenv("IRCX_MULTI_RANK");
  if (rank && std::string(rank) != "1")
    RCXLOG.error(Loc::current(), "PatternGrid uses one neighbor per side; remove IRCX_MULTI_RANK");
  const char* table = std::getenv("IRCX_PATTERN_TABLE");
  if (table && *table)
    RCXLOG.error(Loc::current(), "only PatternGrid capacitance calculation is supported; remove IRCX_PATTERN_TABLE");
  const char* interlayer_table = std::getenv("IRCX_XLAYER_TABLE");
  if (interlayer_table && *interlayer_table)
    RCXLOG.error(Loc::current(), "only PatternGrid capacitance calculation is supported; remove IRCX_XLAYER_TABLE");

  _grid.reset();
  _grid_override.reset();
  const char* path = std::getenv("IRCX_PATTERN_GRID");
  if ((path && *path) && database.get_corner_data_list().size() != 1)
    RCXLOG.error(Loc::current(), "global grid override requires exactly one process corner");
  if (path && *path) _grid_override = std::make_shared<const pattern::PatternGrid>(path);
  _neighborhood = std::make_unique<pattern::GridNeighborhood>(database.get_topo_pool());
  _directed.assign(RCXDM.getDatabase().get_layout_data().get_regular_net_num(), {});
  extractCapacitance();
  _neighborhood.reset();
  _grid.reset();
  _grid_override.reset();

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
  finishPatternCoupling();
  RCXDM.getDatabase().get_rc_data().merge_net_coupling_cap_entry_list();
}

void CapExtractor::extractCornerCapacitance(int32_t corner_idx)
{
  const auto& corner = RCXDM.getDatabase().get_corner_data_list()[corner_idx];
  _grid = _grid_override ? _grid_override : corner.get_pattern_grid();
  if (!_grid) RCXLOG.error(Loc::current(), "missing pattern grid for process corner ", corner_idx);
  int32_t net_num = RCXDM.getDatabase().get_layout_data().get_regular_net_num();
  int32_t thread_num = RCXUTIL.getThreadNum(net_num, RCXDM.getConfig().thread_number);
  std::vector<std::string> errors(net_num);
#pragma omp parallel for schedule(dynamic) num_threads(thread_num)
  for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
    errors[net_idx] = extractNetCapacitance(corner_idx, net_idx);
  }
  for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
    if (!errors[net_idx].empty())
      RCXLOG.error(Loc::current(), "Capacitance extraction failed for net ", net_idx, ": ", errors[net_idx]);
  }
}

std::string CapExtractor::extractNetCapacitance(int32_t corner_idx, int32_t net_idx)
{
  std::span<TopoEdge> edge_list = RCXDM.getDatabase().get_topo_pool().get_net_edge_list(net_idx);
  for (int32_t edge_idx = 0; edge_idx < static_cast<int32_t>(edge_list.size()); ++edge_idx) {
    auto error = extractEdgeCapacitance(corner_idx, net_idx, edge_idx);
    if (!error.empty())
      return error;
  }
  return {};
}

std::string CapExtractor::extractEdgeCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx)
{
  Database& database = RCXDM.getDatabase();
  std::span<TopoEdge> edge_list = database.get_topo_pool().get_net_edge_list(net_idx);
  TopoEdge& edge = edge_list[edge_idx];
  if (edge.get_is_via()) {
    return {};
  }

  NetEnv& net_env = database.get_net_env_list()[net_idx];
  std::span<EdgeEnvInterval> env_interval_list = net_env.get_edge_interval_list(edge_idx);
  int32_t interval_num = static_cast<int32_t>(env_interval_list.size());
  for (int32_t interval_idx = 0; interval_idx < interval_num; ++interval_idx) {
    auto error = extractEdgeIntervalCapacitance(corner_idx, net_idx, edge_idx, interval_idx);
    if (!error.empty())
      return error;
  }
  return {};
}

std::string CapExtractor::extractEdgeIntervalCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, int32_t interval_idx)
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
    auto error = extractCapacitanceSpan(corner_idx, net_idx, edge_idx, interval_idx, coord_list[coord_idx], coord_list[coord_idx + 1]);
    if (!error.empty())
      return error;
  }
  return {};
}

std::string CapExtractor::extractCapacitanceSpan(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, int32_t interval_idx, int32_t start_coord,
                                                 int32_t end_coord)
{
  if (end_coord <= start_coord) return {};
  auto& database = RCXDM.getDatabase();
  auto& corner = database.get_corner_data_list()[corner_idx];
  auto& edge = database.get_topo_pool().get_net_edge_list(net_idx)[edge_idx];
  const auto* conductor = getProcessConductor(corner, edge.get_layer_idx());
  if (!conductor) return {};

  auto& interval = database.get_net_env_list()[net_idx].get_edge_interval_list(edge_idx)[interval_idx];
  const auto [below_layer, above_layer] = pattern::crossLayers(interval.get_cross_layer_overlap_list(), start_coord, end_coord);
  // Captab contexts use LEF/DEF names; process names belong to the ITF binding.
  const auto& design_names = database.get_layer_table().get_design_idx_to_name_map();
  const auto& master = design_names.at(edge.get_layer_idx());
  const std::string below = below_layer == 0 ? "SUBSTRATE" : design_names.at(below_layer);
  const std::string above = above_layer == -1 ? "-" : design_names.at(above_layer);
  const double unit = 1.0 / database.get_layout_data().get_dbu_per_micron();
  const auto parts = _neighborhood->pieces(edge, start_coord, end_coord, unit);
  auto& ground = database.get_rc_data().get_corner_net_ground_capacitance_list(CornerNetIdx(corner_idx, net_idx));
  for (const auto& part : parts) {
    if (part.duplicate) continue;
    const auto sides = pattern::gridSides(edge, part, unit);
    std::string error;
    const auto caps = _grid->queryChecked(master, below, above, edge.get_width() * unit,
                                          sides.lower ? sides.lower_spacing : -1., sides.upper ? sides.upper_spacing : -1., error);
    if (!caps) return error;
    _neighborhood->recordGridCoverage(part.hi - part.lo, caps->width_clamped, caps->gap_clamped);
    const double length = unit * (part.hi - part.lo);
    ground[edge_idx] += length * caps->ground;
    if (sides.lower)
      addDirectedCoupling(corner_idx, net_idx, edge_idx, sides.lower, part.lo, part.hi, length * caps->left);
    if (sides.upper)
      addDirectedCoupling(corner_idx, net_idx, edge_idx, sides.upper, part.lo, part.hi, length * caps->right);
  }
  return {};
}

const ProcessConductor* CapExtractor::getProcessConductor(CornerData& corner_data, int32_t design_layer_idx)
{
  return findProcessConductor(corner_data, RCXDM.getDatabase().get_layer_table(), design_layer_idx);
}

void CapExtractor::addDirectedCoupling(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, TopoEdge* neighbor, int32_t lo, int32_t hi, double cap)
{
  auto& database = RCXDM.getDatabase();
  if (neighbor->get_is_special_net()) {
    database.get_rc_data().get_corner_net_ground_capacitance_list(CornerNetIdx(corner_idx, net_idx))[edge_idx] += cap;
  } else if (neighbor->get_net_idx() != net_idx) {
    auto& pool = database.get_topo_pool();
    _directed[net_idx].push_back(
        {pool.get_edge_idx(net_idx, edge_idx), pool.get_edge_idx(neighbor->get_net_idx(), neighbor->get_edge_idx()), corner_idx, lo, hi, cap});
  }
}

void CapExtractor::finishPatternCoupling()
{
  std::map<std::tuple<int32_t, int32_t, int32_t>, pattern::ReciprocalCap> pairs;
  for (const auto& net : _directed) {
    for (const auto& observation : net) {
      auto key = std::tuple{std::min(observation.first, observation.second), std::max(observation.first, observation.second), observation.corner};
      pairs[key].add(observation.first < observation.second, observation.lo, observation.hi, observation.cap);
    }
  }
  auto& database = RCXDM.getDatabase();
  for (const auto& [key, reciprocal] : pairs) {
    auto [first, second, corner] = key;
    database.get_rc_data().append_net_coupling_cap_entry(database.get_topo_pool().get_edge(first).get_net_idx(), first, second, corner, reciprocal.value());
  }
  _directed.clear();
}

}  // namespace ircx
