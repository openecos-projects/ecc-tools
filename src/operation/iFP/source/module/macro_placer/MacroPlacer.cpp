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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "MacroPlacer.hpp"

#include "DataManager.hpp"
#include "FPInterface.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ifp {

// public

void MacroPlacer::initInst()
{
  if (_mp_instance == nullptr) {
    _mp_instance = new MacroPlacer();
  }
}

MacroPlacer& MacroPlacer::getInst()
{
  if (_mp_instance == nullptr) {
    FPLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_mp_instance;
}

void MacroPlacer::destroyInst()
{
  if (_mp_instance != nullptr) {
    delete _mp_instance;
    _mp_instance = nullptr;
  }
}

// function

void MacroPlacer::place()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  MPComParam mp_com_param;
  setMPComParam(mp_com_param);
  _placed_macro_list.clear();
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (instance.get_macro() && instance.get_placed()) {
      _placed_macro_list.push_back(&instance);
    }
  }
  checkMacroPlacement();
  checkMacroInCore();
  buildMacroPlacementHalo(mp_com_param);
  buildMacroRoutingHalo(mp_com_param);
  cutRowList();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void MacroPlacer::setMPComParam(MPComParam& mp_com_param)
{
  mp_com_param.set_placement_halo_micron(FPDM.getConfig().macro_placement_halo);
  mp_com_param.set_routing_halo_micron(FPDM.getConfig().macro_routing_halo);
  FPLOG.info(Loc::current(), "placement_halo_micron: ", mp_com_param.get_placement_halo_micron());
  FPLOG.info(Loc::current(), "routing_halo_micron: ", mp_com_param.get_routing_halo_micron());
}

void MacroPlacer::checkMacroPlacement()
{
  std::string unplaced_macro_name_list;
  int32_t unplaced_macro_num = 0;
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (!instance.get_macro() || instance.get_placed()) {
      continue;
    }
    if (!unplaced_macro_name_list.empty()) {
      unplaced_macro_name_list += ", ";
    }
    unplaced_macro_name_list += instance.get_name();
    unplaced_macro_num++;
  }

  if (unplaced_macro_num > 0) {
    FPLOG.error(Loc::current(), "Found ", unplaced_macro_num, " unplaced macro(s): ", unplaced_macro_name_list,
                ". Please place all macros before running iFP!");
  }
}

void MacroPlacer::checkMacroInCore()
{
  Core& core = FPDM.getDatabase().get_core();
  for (Instance* instance_ptr : _placed_macro_list) {
    Instance& instance = *instance_ptr;
    PlanarRect& macro_rect = instance.get_bounding_rect();
    if (core.get_ll_x() <= macro_rect.get_ll_x() && macro_rect.get_ur_x() <= core.get_ur_x() && core.get_ll_y() <= macro_rect.get_ll_y()
        && macro_rect.get_ur_y() <= core.get_ur_y()) {
      continue;
    }
    FPLOG.error(Loc::current(), "The macro '", instance.get_name(), "' is placed outside core!");
  }
}

void MacroPlacer::buildMacroPlacementHalo(MPComParam& mp_com_param)
{
  Database& database = FPDM.getDatabase();
  int32_t halo = FPUTIL.transMicronToDBU(mp_com_param.get_placement_halo_micron(), database.get_micron_dbu());
  for (Instance* instance_ptr : _placed_macro_list) {
    Instance& instance = *instance_ptr;
    PlanarRect& macro_rect = instance.get_bounding_rect();
    instance.get_placement_halo_rect().set_rect(macro_rect.get_ll_x() - halo, macro_rect.get_ll_y() - halo, macro_rect.get_ur_x() + halo,
                                                macro_rect.get_ur_y() + halo);
  }
}

void MacroPlacer::buildMacroRoutingHalo(MPComParam& mp_com_param)
{
  Database& database = FPDM.getDatabase();
  int32_t halo = FPUTIL.transMicronToDBU(mp_com_param.get_routing_halo_micron(), database.get_micron_dbu());
  for (Instance* instance_ptr : _placed_macro_list) {
    Instance& instance = *instance_ptr;
    PlanarRect& macro_rect = instance.get_bounding_rect();
    instance.get_routing_halo_rect().set_rect(macro_rect.get_ll_x() - halo, macro_rect.get_ll_y() - halo, macro_rect.get_ur_x() + halo,
                                              macro_rect.get_ur_y() + halo);
  }
}

void MacroPlacer::cutRowList()
{
  Database& database = FPDM.getDatabase();
  std::vector<Row> cut_row_list;
  int32_t min_channel_width = getMinPGChannelWidth();
  for (Row& row : database.get_row_list()) {
    cutRow(row, cut_row_list);
  }
  int32_t removed_row_num = 0;
  double removed_area = 0.0;
  std::vector<Row> usable_row_list;
  for (const Row& row : cut_row_list) {
    if (row.get_width() >= min_channel_width) {
      usable_row_list.push_back(row);
      continue;
    }
    for (const Instance& instance : database.get_instance_list()) {
      if (!instance.get_placed() || instance.get_macro() || instance.is_delete_instance()) {
        continue;
      }
      const PlanarRect& rect = instance.get_bounding_rect();
      if (row.get_ll_x() < rect.get_ur_x() && rect.get_ll_x() < row.get_ur_x() && row.get_ll_y() < rect.get_ur_y()
          && rect.get_ll_y() < row.get_ur_y()) {
        FPLOG.error(Loc::current(), "Cannot remove unpowerable row '", row.get_name(), "': it contains placed instance '", instance.get_name(), "'.");
      }
    }
    removed_row_num++;
    removed_area += (row.get_width() / 1.0 / database.get_micron_dbu()) * (row.get_height() / 1.0 / database.get_micron_dbu());
  }
  if (removed_row_num > 0) {
    FPLOG.warn(Loc::current(), "Removed ", removed_row_num, " macro-cut row fragment(s), area=", removed_area,
               " um^2: narrower than the configured two-net PG channel (", min_channel_width, " DBU). No physical cells or rails will be placed there.");
  }
  reportMissingRowIntervals(usable_row_list);
  if (!cut_row_list.empty() && usable_row_list.empty()) {
    FPLOG.error(Loc::current(), "No usable rows remain after reserving the configured PG channel.");
  }
  database.set_row_list(usable_row_list);
  database.set_new_row_list(usable_row_list);
}

int32_t MacroPlacer::getMinPGChannelWidth()
{
  if (_placed_macro_list.empty()) {
    return 0;
  }
  Database& database = FPDM.getDatabase();
  Config& config = FPDM.getConfig();
  int32_t channel_width = INT32_MAX;
  for (const PGStripe& stripe : config.pg_stripe_list) {
    auto layer_iter = database.get_routing_layer_name_to_idx_map().find(stripe.get_layer_name());
    if (layer_iter == database.get_routing_layer_name_to_idx_map().end()) {
      continue;
    }
    const RoutingLayer& layer = database.get_routing_layer_list()[layer_iter->second];
    if (layer.get_prefer_direction() != Direction::kVertical || stripe.get_width_micron() <= 0.0) {
      continue;
    }
    for (const PGRail& rail : config.pg_rail_list) {
      for (const PGLayerPair& pair : config.pg_layer_pair_list) {
        if (!((pair.get_first_layer_name() == rail.get_layer_name() && pair.get_second_layer_name() == layer.get_name())
              || (pair.get_second_layer_name() == rail.get_layer_name() && pair.get_first_layer_name() == layer.get_name()))) {
          continue;
        }
        int32_t width = FPUTIL.transMicronToDBU(stripe.get_width_micron(), database.get_micron_dbu());
        width = std::max(width, FPI.getPGViaMinSize(rail.get_layer_name(), layer.get_name()).first);
        int32_t spacing = std::max(0, layer.get_spacing());
        channel_width = std::min(channel_width, FPUTIL.alignUp(2 * width + spacing, std::max(1, database.get_manufacture_grid())));
      }
    }
  }
  return channel_width == INT32_MAX ? 0 : channel_width;
}

void MacroPlacer::cutRow(Row& row, std::vector<Row>& cut_row_list)
{
  std::vector<std::pair<int32_t, int32_t>> blockage_interval_list = getRowBlockageIntervalList(row);
  if (blockage_interval_list.empty()) {
    cut_row_list.push_back(row);
    return;
  }

  int32_t current_x = row.get_ll_x();
  int32_t cut_row_idx = 0;
  for (std::pair<int32_t, int32_t>& blockage_interval : blockage_interval_list) {
    if (blockage_interval.second <= current_x) {
      continue;
    }
    if (blockage_interval.first > current_x) {
      addCutRow(row, cut_row_list, current_x, blockage_interval.first, cut_row_idx++);
    }
    current_x = std::max(current_x, blockage_interval.second);
    if (current_x >= row.get_ur_x()) {
      return;
    }
  }
  if (current_x < row.get_ur_x()) {
    addCutRow(row, cut_row_list, current_x, row.get_ur_x(), cut_row_idx);
  }
}

std::vector<std::pair<int32_t, int32_t>> MacroPlacer::getRowBlockageIntervalList(Row& row)
{
  Database& database = FPDM.getDatabase();
  Site& site = database.get_site_map()[row.get_site_name()];
  std::vector<std::pair<int32_t, int32_t>> blockage_interval_list;

  int32_t rail_margin = 0;
  for (const PGRail& pg_rail : FPDM.getConfig().pg_rail_list) {
    int32_t rail_width = FPUTIL.transMicronToDBU(pg_rail.get_width_micron(), database.get_micron_dbu());
    if (rail_width > 0) {
      rail_margin = std::max(rail_margin, rail_width / 2 + 1);
    }
  }

  for (Instance* instance_ptr : _placed_macro_list) {
    Instance& instance = *instance_ptr;
    PlanarRect& placement_halo_rect = instance.get_placement_halo_rect();
    if (row.get_ur_y() + rail_margin <= placement_halo_rect.get_ll_y()
        || placement_halo_rect.get_ur_y() <= row.get_ll_y() - rail_margin) {
      continue;
    }
    int32_t site_origin_x = row.get_site_origin_x();
    int32_t start_x = std::max(row.get_ll_x(), site_origin_x + FPUTIL.alignDown(placement_halo_rect.get_ll_x() - site_origin_x, site.get_width()));
    int32_t end_x = std::min(row.get_ur_x(), site_origin_x + FPUTIL.alignUp(placement_halo_rect.get_ur_x() - site_origin_x, site.get_width()));
    if (start_x < end_x) {
      blockage_interval_list.emplace_back(start_x, end_x);
    }
  }
  std::sort(blockage_interval_list.begin(), blockage_interval_list.end(),
            [](const std::pair<int32_t, int32_t>& first, const std::pair<int32_t, int32_t>& second) { return first.first < second.first; });
  return blockage_interval_list;
}

void MacroPlacer::addCutRow(Row& row, std::vector<Row>& cut_row_list, int32_t start_x, int32_t end_x, int32_t cut_row_idx)
{
  Row cut_row;
  cut_row.set_name(row.get_name() + "_" + std::to_string(cut_row_idx));
  cut_row.set_site_name(row.get_site_name());
  cut_row.set_site_origin_x(row.get_site_origin_x());
  cut_row.set_y(row.get_y());
  cut_row.set_orient(row.get_orient());
  cut_row.set_rect(start_x, row.get_ll_y(), end_x, row.get_ur_y());
  cut_row_list.push_back(cut_row);
}

void MacroPlacer::reportMissingRowIntervals(const std::vector<Row>& usable_row_list)
{
  std::map<int32_t, int32_t> missing_row_y_to_ur_y_map;
  for (const Row& row : FPDM.getDatabase().get_row_list()) {
    auto row_iter = missing_row_y_to_ur_y_map.emplace(row.get_y(), row.get_ur_y()).first;
    row_iter->second = std::max(row_iter->second, row.get_ur_y());
  }
  for (const Row& row : usable_row_list) {
    missing_row_y_to_ur_y_map.erase(row.get_y());
  }
  if (missing_row_y_to_ur_y_map.empty()) {
    return;
  }

  std::stringstream missing_y_intervals;
  for (auto row_iter = missing_row_y_to_ur_y_map.begin(); row_iter != missing_row_y_to_ur_y_map.end();) {
    int32_t start_y = row_iter->first;
    int32_t end_y = row_iter->second;
    int32_t row_y_num = 1;
    if (row_iter != missing_row_y_to_ur_y_map.begin()) {
      missing_y_intervals << ", ";
    }
    ++row_iter;
    while (row_iter != missing_row_y_to_ur_y_map.end() && row_iter->first == end_y) {
      end_y = row_iter->second;
      row_y_num++;
      ++row_iter;
    }
    missing_y_intervals << "[" << start_y << "," << end_y << ") (" << row_y_num << " row y-level(s))";
  }
  FPLOG.warn(Loc::current(), "Sparse row grid: ", missing_row_y_to_ur_y_map.size(),
             " original row y-level(s) lost all usable fragments after macro cutting/PG-channel pruning. Missing y intervals (DBU, [begin,end)): ",
             missing_y_intervals.str(),
             ". Downstream placement/sizing must support sparse physical row indices; no dummy rows were inserted and surviving rows were not compacted.");
}

// private

MacroPlacer* MacroPlacer::_mp_instance = nullptr;

}  // namespace ifp
