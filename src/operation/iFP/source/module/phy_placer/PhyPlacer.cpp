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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "PhyPlacer.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ifp {

// public

void PhyPlacer::initInst()
{
  if (_pp_instance == nullptr) {
    _pp_instance = new PhyPlacer();
  }
}

PhyPlacer& PhyPlacer::getInst()
{
  if (_pp_instance == nullptr) {
    FPLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_pp_instance;
}

void PhyPlacer::destroyInst()
{
  if (_pp_instance != nullptr) {
    delete _pp_instance;
    _pp_instance = nullptr;
  }
}

// function

void PhyPlacer::place()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  PPModel pp_model;
  placePhyCell(pp_model);

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PhyPlacer::placePhyCell(PPModel& pp_model)
{
  Config& config = FPDM.getConfig();
  if (config.tap_distance_micron <= 0.0 || config.boundary_tap_rule_micron <= 0.0 || config.tapcell_name.empty()
      || config.left_endcap_name.empty() || config.right_endcap_name.empty() || config.top_endcap_name_list.empty()
      || config.bottom_endcap_name_list.empty() || config.top_boundary_tap_name_list.empty() || config.bottom_boundary_tap_name_list.empty()) {
    return;
  }

  Database& database = FPDM.getDatabase();
  int32_t tap_distance = FPUTIL.transMicronToDBU(config.tap_distance_micron, database.get_micron_dbu());
  int32_t boundary_tap_rule = FPUTIL.transMicronToDBU(config.boundary_tap_rule_micron, database.get_micron_dbu());
  adjustTapDistance(tap_distance);
  adjustTapDistance(boundary_tap_rule);
  if (tap_distance <= 0 || boundary_tap_rule <= 0 || buildPPRegionList(pp_model) == 0) {
    return;
  }

  buildPPBoundaryRegionList(pp_model);

  int32_t endcap_idx = 1;
  int32_t tapcell_idx = 1;
  int32_t boundary_tap_idx = 1;
  insertSideEndcap(pp_model, endcap_idx);
  insertWellTap(pp_model, tap_distance, tapcell_idx);
  insertBoundaryWellTap(pp_model, tap_distance, tapcell_idx);
  insertBoundaryTap(pp_model, boundary_tap_rule, boundary_tap_idx);
  insertEdgeEndcap(pp_model, endcap_idx);
}

void PhyPlacer::adjustTapDistance(int32_t& inst_space)
{
  Database& database = FPDM.getDatabase();
  if (database.get_core().get_core_site_name().empty()) {
    return;
  }
  Site& core_site = database.get_site_map()[database.get_core().get_core_site_name()];
  if (core_site.get_width() <= 0 || inst_space % core_site.get_width() == 0) {
    return;
  }
  inst_space = inst_space / core_site.get_width() * core_site.get_width();
}

int32_t PhyPlacer::buildPPRegionList(PPModel& pp_model)
{
  std::vector<Row>& row_list = FPDM.getDatabase().get_row_list();
  int32_t row_idx = -1;
  int32_t previous_y_coord = INT32_MIN;
  for (Row& row : row_list) {
    if (row.get_y() != previous_y_coord) {
      row_idx++;
      previous_y_coord = row.get_y();
    }
    buildPPRegionInRow(pp_model, row, row_idx);
    pp_model.set_top_y_coord(std::max(pp_model.get_top_y_coord(), row.get_y()));
    pp_model.set_bottom_y_coord(std::min(pp_model.get_bottom_y_coord(), row.get_y()));
  }
  return static_cast<int32_t>(pp_model.get_pp_region_list().size());
}

void PhyPlacer::buildPPRegionInRow(PPModel& pp_model, Row& row, int32_t row_idx)
{
  std::vector<std::pair<int32_t, int32_t>> macro_blockage_interval_list = getMacroBlockageIntervalList(row);
  if (macro_blockage_interval_list.empty()) {
    addPPRegion(pp_model, row, row_idx, row.get_ll_x(), row.get_ur_x());
    return;
  }

  int32_t current_x_coord = row.get_ll_x();
  for (std::pair<int32_t, int32_t>& macro_blockage_interval : macro_blockage_interval_list) {
    if (macro_blockage_interval.second <= current_x_coord) {
      continue;
    }
    if (macro_blockage_interval.first > current_x_coord) {
      addPPRegion(pp_model, row, row_idx, current_x_coord, macro_blockage_interval.first);
    }
    current_x_coord = std::max(current_x_coord, macro_blockage_interval.second);
    if (current_x_coord >= row.get_ur_x()) {
      return;
    }
  }
  if (current_x_coord < row.get_ur_x()) {
    addPPRegion(pp_model, row, row_idx, current_x_coord, row.get_ur_x());
  }
}

std::vector<std::pair<int32_t, int32_t>> PhyPlacer::getMacroBlockageIntervalList(Row& row)
{
  Database& database = FPDM.getDatabase();
  Site& site = database.get_site_map()[row.get_site_name()];
  std::vector<std::pair<int32_t, int32_t>> macro_blockage_interval_list;
  for (Instance& instance : database.get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }
    PlanarRect& placement_halo_rect = instance.get_placement_halo_rect();
    if (row.get_ur_y() <= placement_halo_rect.get_ll_y() || placement_halo_rect.get_ur_y() <= row.get_ll_y()) {
      continue;
    }
    int32_t site_origin_x = row.get_site_origin_x();
    int32_t start_x_coord = std::max(
        row.get_ll_x(), site_origin_x + FPUTIL.alignDown(placement_halo_rect.get_ll_x() - site_origin_x, site.get_width()));
    int32_t end_x_coord
        = std::min(row.get_ur_x(), site_origin_x + FPUTIL.alignUp(placement_halo_rect.get_ur_x() - site_origin_x, site.get_width()));
    if (start_x_coord < end_x_coord) {
      macro_blockage_interval_list.emplace_back(start_x_coord, end_x_coord);
    }
  }
  std::sort(macro_blockage_interval_list.begin(), macro_blockage_interval_list.end());
  return macro_blockage_interval_list;
}

void PhyPlacer::addPPRegion(PPModel& pp_model, Row& row, int32_t row_idx, int32_t start_coord, int32_t end_coord)
{
  Site& site = FPDM.getDatabase().get_site_map()[row.get_site_name()];
  if (site.get_width() <= 0 || site.get_height() <= 0) {
    return;
  }

  int32_t site_origin_x = row.get_site_origin_x();
  int32_t site_start_coord = site_origin_x + FPUTIL.alignUp(start_coord - site_origin_x, site.get_width());
  int32_t site_end_coord = site_origin_x + FPUTIL.alignDown(end_coord - site_origin_x, site.get_width());
  if (site_start_coord >= site_end_coord) {
    return;
  }

  PPRegion pp_region;
  pp_region.set_row_idx(row_idx);
  pp_region.set_start_coord(site_start_coord);
  pp_region.set_end_coord(site_end_coord);
  pp_region.set_site_origin_x(site_origin_x);
  pp_region.set_site_width(site.get_width());
  pp_region.set_site_height(site.get_height());
  pp_region.set_y_coord(row.get_y());
  pp_region.set_orient(row.get_orient());
  pp_model.get_pp_region_list().push_back(pp_region);
}

void PhyPlacer::buildPPBoundaryRegionList(PPModel& pp_model)
{
  addCorePPBoundaryRegion(pp_model);
  addMacroPPBoundaryRegion(pp_model);
}

void PhyPlacer::addCorePPBoundaryRegion(PPModel& pp_model)
{
  for (PPRegion& pp_region : pp_model.get_pp_region_list()) {
    if (pp_region.get_y_coord() == pp_model.get_top_y_coord()) {
      addPPBoundaryRegion(pp_model, pp_region, pp_region.get_start_coord(), pp_region.get_end_coord(), PPBoundaryType::kTop);
    }
    if (pp_region.get_y_coord() == pp_model.get_bottom_y_coord()) {
      addPPBoundaryRegion(pp_model, pp_region, pp_region.get_start_coord(), pp_region.get_end_coord(), PPBoundaryType::kBottom);
    }
  }
}

void PhyPlacer::addPPBoundaryRegion(PPModel& pp_model, PPRegion& pp_region, int32_t start_coord, int32_t end_coord,
                                    PPBoundaryType boundary_type)
{
  if (start_coord >= end_coord || boundary_type == PPBoundaryType::kNone) {
    return;
  }

  int32_t merged_start_coord = start_coord;
  int32_t merged_end_coord = end_coord;
  std::vector<PPRegion>& boundary_region_list = pp_model.get_boundary_region_list();
  for (int32_t region_idx = 0; region_idx < static_cast<int32_t>(boundary_region_list.size());) {
    PPRegion& boundary_region = boundary_region_list[region_idx];
    if (boundary_region.get_y_coord() != pp_region.get_y_coord() || boundary_region.get_orient() != pp_region.get_orient()
        || boundary_region.get_site_origin_x() != pp_region.get_site_origin_x()
        || boundary_region.get_site_width() != pp_region.get_site_width()
        || boundary_region.get_site_height() != pp_region.get_site_height()
        || boundary_region.get_boundary_type() != boundary_type || merged_end_coord < boundary_region.get_start_coord()
        || boundary_region.get_end_coord() < merged_start_coord) {
      region_idx++;
      continue;
    }
    merged_start_coord = std::min(merged_start_coord, boundary_region.get_start_coord());
    merged_end_coord = std::max(merged_end_coord, boundary_region.get_end_coord());
    boundary_region_list.erase(boundary_region_list.begin() + region_idx);
  }

  PPRegion boundary_region = pp_region;
  boundary_region.set_start_coord(merged_start_coord);
  boundary_region.set_end_coord(merged_end_coord);
  boundary_region.set_boundary_type(boundary_type);
  boundary_region_list.push_back(boundary_region);
}

void PhyPlacer::addMacroPPBoundaryRegion(PPModel& pp_model)
{
  Database& database = FPDM.getDatabase();
  for (Instance& instance : database.get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }
    PlanarRect& placement_halo_rect = instance.get_placement_halo_rect();
    int32_t top_boundary_y_coord = INT32_MAX;
    int32_t bottom_boundary_y_coord = INT32_MIN;
    for (Row& row : database.get_row_list()) {
      if (row.get_ll_y() >= placement_halo_rect.get_ur_y()) {
        top_boundary_y_coord = std::min(top_boundary_y_coord, row.get_y());
      }
      if (row.get_ur_y() <= placement_halo_rect.get_ll_y()) {
        bottom_boundary_y_coord = std::max(bottom_boundary_y_coord, row.get_y());
      }
    }
    for (Row& row : database.get_row_list()) {
      if (row.get_y() == top_boundary_y_coord) {
        addMacroPPBoundaryRegionInRow(pp_model, row, placement_halo_rect, PPBoundaryType::kBottom);
      }
      if (row.get_y() == bottom_boundary_y_coord) {
        addMacroPPBoundaryRegionInRow(pp_model, row, placement_halo_rect, PPBoundaryType::kTop);
      }
    }
  }
}

void PhyPlacer::addMacroPPBoundaryRegionInRow(PPModel& pp_model, Row& row, PlanarRect& placement_halo_rect,
                                              PPBoundaryType boundary_type)
{
  Site& site = FPDM.getDatabase().get_site_map()[row.get_site_name()];
  int32_t site_origin_x = row.get_site_origin_x();
  int32_t start_coord = site_origin_x + FPUTIL.alignDown(placement_halo_rect.get_ll_x() - site_origin_x, site.get_width());
  int32_t end_coord = site_origin_x + FPUTIL.alignUp(placement_halo_rect.get_ur_x() - site_origin_x, site.get_width());
  for (PPRegion& pp_region : pp_model.get_pp_region_list()) {
    if (pp_region.get_y_coord() != row.get_y()) {
      continue;
    }
    int32_t clipped_start_coord = std::max(start_coord, pp_region.get_start_coord());
    int32_t clipped_end_coord = std::min(end_coord, pp_region.get_end_coord());
    addPPBoundaryRegion(pp_model, pp_region, clipped_start_coord, clipped_end_coord, boundary_type);
  }
}

void PhyPlacer::insertSideEndcap(PPModel& pp_model, int32_t& endcap_idx)
{
  Config& config = FPDM.getConfig();
  Database& database = FPDM.getDatabase();
  CellMaster& left_endcap_master = database.get_cell_master_map()[config.left_endcap_name];
  CellMaster& right_endcap_master = database.get_cell_master_map()[config.right_endcap_name];
  for (PPRegion& pp_region : pp_model.get_pp_region_list()) {
    int32_t left_endcap_width = getCellMasterWidthByOrient(left_endcap_master, pp_region.get_orient());
    int32_t right_endcap_width = getCellMasterWidthByOrient(right_endcap_master, pp_region.get_orient());
    if (left_endcap_width <= 0 || right_endcap_width <= 0) {
      continue;
    }
    int32_t left_endcap_x_coord = pp_region.get_site_origin_x()
                                  + FPUTIL.alignUp(pp_region.get_start_coord() - pp_region.get_site_origin_x(), pp_region.get_site_width());
    int32_t right_endcap_x_coord = pp_region.get_site_origin_x()
                                   + FPUTIL.alignDown(pp_region.get_end_coord() - right_endcap_width - pp_region.get_site_origin_x(),
                                                       pp_region.get_site_width());
    if (left_endcap_x_coord + left_endcap_width <= pp_region.get_end_coord()) {
      addPhyCell(pp_model, pp_region, "BNDRY_CAP_" + std::to_string(endcap_idx++), config.left_endcap_name, left_endcap_x_coord);
    }
    if (left_endcap_x_coord + left_endcap_width <= right_endcap_x_coord) {
      addPhyCell(pp_model, pp_region, "BNDRY_CAP_" + std::to_string(endcap_idx++), config.right_endcap_name, right_endcap_x_coord);
    }
  }
}

int32_t PhyPlacer::getCellMasterWidthByOrient(CellMaster& cell_master, PlacementOrientation orient)
{
  if (orient == PlacementOrientation::kN || orient == PlacementOrientation::kS || orient == PlacementOrientation::kFN
      || orient == PlacementOrientation::kFS) {
    return cell_master.get_width();
  }
  return cell_master.get_height();
}

void PhyPlacer::addPhyCell(PPModel& pp_model, PPRegion& pp_region, std::string instance_name, std::string cell_master_name,
                           int32_t x_coord)
{
  Database& database = FPDM.getDatabase();
  CellMaster& cell_master = database.get_cell_master_map()[cell_master_name];
  if (!isPhyCellOnSite(pp_region, cell_master, x_coord)) {
    return;
  }
  int32_t cell_width = getCellMasterWidthByOrient(cell_master, pp_region.get_orient());
  Instance instance;
  instance.set_name(instance_name);
  instance.set_master_name(cell_master_name);
  instance.set_orient(pp_region.get_orient());
  instance.set_coord(x_coord, pp_region.get_y_coord());
  instance.set_width(cell_master.get_width());
  instance.set_height(cell_master.get_height());
  instance.set_fixed(true);
  instance.set_placed(true);
  instance.set_new_instance(true);
  int32_t instance_idx = static_cast<int32_t>(database.get_instance_list().size());
  database.get_instance_list().push_back(instance);
  database.get_instance_name_to_idx_map()[instance_name] = instance_idx;

  PPRegion occupied_region;
  occupied_region.set_start_coord(x_coord);
  occupied_region.set_end_coord(x_coord + cell_width);
  occupied_region.set_site_origin_x(pp_region.get_site_origin_x());
  occupied_region.set_site_width(pp_region.get_site_width());
  occupied_region.set_site_height(pp_region.get_site_height());
  occupied_region.set_y_coord(pp_region.get_y_coord());
  occupied_region.set_orient(pp_region.get_orient());
  pp_model.get_occupied_region_list().push_back(occupied_region);
}

bool PhyPlacer::isPhyCellOnSite(PPRegion& pp_region, CellMaster& cell_master, int32_t x_coord)
{
  int32_t cell_width = getCellMasterWidthByOrient(cell_master, pp_region.get_orient());
  if (cell_width <= 0 || pp_region.get_site_width() <= 0 || pp_region.get_site_height() <= 0
      || x_coord < pp_region.get_start_coord() || pp_region.get_end_coord() < x_coord + cell_width
      || (x_coord - pp_region.get_site_origin_x()) % pp_region.get_site_width() != 0
      || cell_width % pp_region.get_site_width() != 0 || cell_master.get_height() != pp_region.get_site_height()) {
    return false;
  }
  return true;
}

void PhyPlacer::insertWellTap(PPModel& pp_model, int32_t tap_distance, int32_t& tapcell_idx)
{
  for (PPRegion& pp_region : pp_model.get_pp_region_list()) {
    int32_t tap_offset = pp_region.get_row_idx() % 2 == 0 ? 0 : tap_distance / 2;
    insertWellTapInRegion(pp_model, pp_region, tap_distance, tap_offset, tapcell_idx);
  }
}

void PhyPlacer::insertWellTapInRegion(PPModel& pp_model, PPRegion& pp_region, int32_t tap_distance, int32_t tap_offset,
                                      int32_t& tapcell_idx)
{
  Database& database = FPDM.getDatabase();
  Config& config = FPDM.getConfig();
  CellMaster& tapcell_master = database.get_cell_master_map()[config.tapcell_name];
  int32_t tapcell_width = getCellMasterWidthByOrient(tapcell_master, pp_region.get_orient());
  if (tapcell_width <= 0) {
    return;
  }
  int32_t x_coord = pp_region.get_site_origin_x() + tap_offset;
  while (x_coord < pp_region.get_start_coord()) {
    x_coord += tap_distance;
  }
  while (x_coord + tapcell_width <= pp_region.get_end_coord()) {
    int32_t search_end_coord = std::min(pp_region.get_end_coord(), x_coord + tap_distance);
    int32_t available_x_coord = getAvailableCellCoord(pp_model, pp_region, x_coord, search_end_coord, tapcell_master);
    if (available_x_coord >= 0) {
      addPhyCell(pp_model, pp_region, "WELLTAP_" + std::to_string(tapcell_idx++), config.tapcell_name, available_x_coord);
    }
    x_coord += tap_distance;
  }
}

void PhyPlacer::insertBoundaryWellTap(PPModel& pp_model, int32_t tap_distance, int32_t& tapcell_idx)
{
  for (PPRegion& boundary_region : pp_model.get_boundary_region_list()) {
    int32_t tap_offset = boundary_region.get_row_idx() % 2 == 0 ? tap_distance / 2 : 0;
    insertWellTapInRegion(pp_model, boundary_region, tap_distance, tap_offset, tapcell_idx);
  }
}

int32_t PhyPlacer::getAvailableCellCoord(PPModel& pp_model, PPRegion& pp_region, int32_t start_coord, int32_t end_coord,
                                         CellMaster& cell_master)
{
  int32_t site_width = pp_region.get_site_width();
  int32_t cell_width = getCellMasterWidthByOrient(cell_master, pp_region.get_orient());
  if (site_width <= 0 || cell_width <= 0 || cell_width % site_width != 0 || cell_master.get_height() != pp_region.get_site_height()) {
    return -1;
  }
  int32_t relative_start_coord = start_coord - pp_region.get_site_origin_x();
  int32_t x_coord = pp_region.get_site_origin_x() + FPUTIL.alignUp(relative_start_coord, site_width);
  for (; x_coord + cell_width <= end_coord; x_coord += site_width) {
    if (isCellAvailable(pp_model, x_coord, x_coord + cell_width, pp_region.get_y_coord())) {
      return x_coord;
    }
  }
  return -1;
}

bool PhyPlacer::isCellAvailable(PPModel& pp_model, int32_t start_coord, int32_t end_coord, int32_t y_coord)
{
  for (PPRegion& occupied_region : pp_model.get_occupied_region_list()) {
    if (occupied_region.get_y_coord() != y_coord || end_coord <= occupied_region.get_start_coord()
        || occupied_region.get_end_coord() <= start_coord) {
      continue;
    }
    return false;
  }
  return true;
}

void PhyPlacer::insertBoundaryTap(PPModel& pp_model, int32_t boundary_tap_rule, int32_t& boundary_tap_idx)
{
  for (PPRegion& boundary_region : pp_model.get_boundary_region_list()) {
    std::vector<std::string>& boundary_tap_name_list = getBoundaryTapNameList(boundary_region.get_boundary_type());
    int32_t x_coord = boundary_region.get_start_coord();
    while (x_coord < boundary_region.get_end_coord()) {
      int32_t search_end_coord = std::min(boundary_region.get_end_coord(), x_coord + boundary_tap_rule);
      for (std::string& boundary_tap_name : boundary_tap_name_list) {
        CellMaster& boundary_tap_master = FPDM.getDatabase().get_cell_master_map()[boundary_tap_name];
        int32_t available_x_coord = getAvailableCellCoord(pp_model, boundary_region, x_coord, search_end_coord, boundary_tap_master);
        if (available_x_coord < 0) {
          continue;
        }
        addPhyCell(pp_model, boundary_region, "BNDRY_CAP_TAP_" + std::to_string(boundary_tap_idx++), boundary_tap_name,
                   available_x_coord);
        break;
      }
      x_coord += 2 * boundary_tap_rule;
    }
  }
}

std::vector<std::string>& PhyPlacer::getBoundaryTapNameList(PPBoundaryType boundary_type)
{
  if (boundary_type == PPBoundaryType::kTop) {
    return FPDM.getConfig().top_boundary_tap_name_list;
  }
  return FPDM.getConfig().bottom_boundary_tap_name_list;
}

void PhyPlacer::insertEdgeEndcap(PPModel& pp_model, int32_t& endcap_idx)
{
  for (PPRegion& boundary_region : pp_model.get_boundary_region_list()) {
    std::vector<std::string>& endcap_name_list = getEdgeEndcapNameList(boundary_region.get_boundary_type());
    std::vector<PPRegion> empty_pp_region_list = getEmptyPPRegionList(pp_model, boundary_region);
    for (PPRegion& empty_region : empty_pp_region_list) {
      fillEdgeEndcap(pp_model, empty_region, endcap_name_list, endcap_idx);
    }
  }
}

std::vector<std::string>& PhyPlacer::getEdgeEndcapNameList(PPBoundaryType boundary_type)
{
  if (boundary_type == PPBoundaryType::kTop) {
    return FPDM.getConfig().top_endcap_name_list;
  }
  return FPDM.getConfig().bottom_endcap_name_list;
}

std::vector<PPRegion> PhyPlacer::getEmptyPPRegionList(PPModel& pp_model, PPRegion& boundary_region)
{
  std::vector<std::pair<int32_t, int32_t>> occupied_interval_list;
  for (PPRegion& occupied_region : pp_model.get_occupied_region_list()) {
    if (occupied_region.get_y_coord() != boundary_region.get_y_coord() || occupied_region.get_end_coord() <= boundary_region.get_start_coord()
        || boundary_region.get_end_coord() <= occupied_region.get_start_coord()) {
      continue;
    }
    int32_t start_coord = std::max(occupied_region.get_start_coord(), boundary_region.get_start_coord());
    int32_t end_coord = std::min(occupied_region.get_end_coord(), boundary_region.get_end_coord());
    occupied_interval_list.emplace_back(start_coord, end_coord);
  }
  std::sort(occupied_interval_list.begin(), occupied_interval_list.end());

  std::vector<PPRegion> empty_pp_region_list;
  int32_t current_x_coord = boundary_region.get_start_coord();
  for (std::pair<int32_t, int32_t>& occupied_interval : occupied_interval_list) {
    if (occupied_interval.second <= current_x_coord) {
      continue;
    }
    if (current_x_coord < occupied_interval.first) {
      PPRegion empty_region = boundary_region;
      empty_region.set_start_coord(current_x_coord);
      empty_region.set_end_coord(occupied_interval.first);
      empty_pp_region_list.push_back(empty_region);
    }
    current_x_coord = std::max(current_x_coord, occupied_interval.second);
    if (current_x_coord >= boundary_region.get_end_coord()) {
      return empty_pp_region_list;
    }
  }
  if (current_x_coord < boundary_region.get_end_coord()) {
    PPRegion empty_region = boundary_region;
    empty_region.set_start_coord(current_x_coord);
    empty_region.set_end_coord(boundary_region.get_end_coord());
    empty_pp_region_list.push_back(empty_region);
  }
  return empty_pp_region_list;
}

void PhyPlacer::fillEdgeEndcap(PPModel& pp_model, PPRegion& empty_region, std::vector<std::string>& endcap_name_list,
                               int32_t& endcap_idx)
{
  int32_t x_coord = empty_region.get_site_origin_x()
                    + FPUTIL.alignUp(empty_region.get_start_coord() - empty_region.get_site_origin_x(), empty_region.get_site_width());
  while (x_coord < empty_region.get_end_coord()) {
    std::string endcap_name
        = getFittingCellMasterName(endcap_name_list, empty_region.get_end_coord() - x_coord, empty_region.get_orient());
    if (endcap_name.empty()) {
      return;
    }
    CellMaster& endcap_master = FPDM.getDatabase().get_cell_master_map()[endcap_name];
    int32_t endcap_width = getCellMasterWidthByOrient(endcap_master, empty_region.get_orient());
    addPhyCell(pp_model, empty_region, "BNDRY_CAP_" + std::to_string(endcap_idx++), endcap_name, x_coord);
    x_coord += endcap_width;
  }
}

std::string PhyPlacer::getFittingCellMasterName(std::vector<std::string>& cell_master_name_list, int32_t max_width,
                                                PlacementOrientation orient)
{
  std::string fitting_cell_master_name;
  int32_t fitting_width = -1;
  for (std::string& cell_master_name : cell_master_name_list) {
    CellMaster& cell_master = FPDM.getDatabase().get_cell_master_map()[cell_master_name];
    int32_t cell_width = getCellMasterWidthByOrient(cell_master, orient);
    if (cell_width <= fitting_width || max_width < cell_width) {
      continue;
    }
    fitting_cell_master_name = cell_master_name;
    fitting_width = cell_width;
  }
  return fitting_cell_master_name;
}

// private

PhyPlacer* PhyPlacer::_pp_instance = nullptr;

}  // namespace ifp
