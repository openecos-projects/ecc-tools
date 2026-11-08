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
  _placed_macro_idx_list.clear();
  _occupied_region_indices.clear();
  for (int32_t idx = 0; idx < static_cast<int32_t>(FPDM.getDatabase().get_instance_list().size()); idx++) {
    Instance& instance = FPDM.getDatabase().get_instance_list()[idx];
    if (instance.get_macro() && instance.get_placed()) {
      _placed_macro_idx_list.push_back(idx);
    }
  }

  Config& config = FPDM.getConfig();
  if (!config.corner_cell_names.empty()) {
    if (config.corner_cell_names.size() != 8) {
      FPLOG.error(Loc::current(), "Physical corners require all eight top/bottom, left/right, inner/outer roles.");
    }
    for (const auto* vertical : {"top", "bottom"}) {
      for (const auto* side : {"left", "right"}) {
        for (const auto* kind : {"inner", "outer"}) {
          const std::string role = std::string(vertical) + "_" + side + "_" + kind;
          if (!config.corner_cell_names.contains(role) || config.corner_cell_names.at(role).empty()) {
            FPLOG.error(Loc::current(), "Missing physical corner role '", role, "'.");
          }
        }
      }
    }
  }
  if (config.tap_distance_micron <= 0.0 || config.boundary_tap_rule_micron <= 0.0 || config.tapcell_name.empty() || config.left_endcap_name.empty()
      || config.right_endcap_name.empty() || config.top_endcap_name_list.empty() || config.bottom_endcap_name_list.empty()
      || config.top_boundary_tap_name_list.empty() || config.bottom_boundary_tap_name_list.empty()) {
    if (!config.tapcell_name.empty() || !config.left_endcap_name.empty() || !config.right_endcap_name.empty()
        || !config.top_endcap_name_list.empty() || !config.bottom_endcap_name_list.empty()) {
      FPLOG.error(Loc::current(), "Incomplete physical-cell configuration: provide tap, side, edge and boundary-tap rules together.");
    }
    return;
  }

  Database& database = FPDM.getDatabase();
  std::vector<std::string> masters{config.tapcell_name, config.left_endcap_name, config.right_endcap_name};
  for (const auto* list : {&config.top_endcap_name_list, &config.bottom_endcap_name_list,
                          &config.top_boundary_tap_name_list, &config.bottom_boundary_tap_name_list}) {
    masters.insert(masters.end(), list->begin(), list->end());
  }
  for (const auto& [role, name] : config.corner_cell_names) {
    masters.push_back(name);
  }
  for (const auto& name : masters) {
    if (!database.get_cell_master_map().contains(name)) {
      FPLOG.error(Loc::current(), "Unknown physical-cell master '", name, "'.");
    }
  }
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
  insertCornerEndcap(pp_model, endcap_idx);
  insertSideEndcap(pp_model, endcap_idx);
  insertBoundaryTap(pp_model, boundary_tap_rule, boundary_tap_idx);
  insertEdgeEndcap(pp_model, endcap_idx);
  insertWellTap(pp_model, tap_distance, tapcell_idx);
  for (auto& boundary : pp_model.get_boundary_region_list()) {
    if (!getEmptyPPRegionList(pp_model, boundary).empty()) {
      FPLOG.error(Loc::current(), "Uncovered physical boundary at y=", boundary.get_y_coord(), ".");
    }
  }
  FPLOG.info(Loc::current(), "Placed ", endcap_idx - 1, " endcaps/corners, ", boundary_tap_idx - 1,
             " boundary taps and ", tapcell_idx - 1, " internal taps; all boundary intervals covered.");
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

int32_t PhyPlacer::getPdnRailMargin() const
{
  int32_t margin = 0;
  int32_t micron_dbu = FPDM.getDatabase().get_micron_dbu();
  for (const PGRail& pg_rail : FPDM.getConfig().pg_rail_list) {
    int32_t rail_width = FPUTIL.transMicronToDBU(pg_rail.get_width_micron(), micron_dbu);
    if (rail_width > 0) {
      margin = std::max(margin, rail_width / 2 + 1);
    }
  }
  return margin;
}

std::vector<std::pair<int32_t, int32_t>> PhyPlacer::getMacroBlockageIntervalList(Row& row)
{
  Database& database = FPDM.getDatabase();
  Site& site = database.get_site_map()[row.get_site_name()];
  std::vector<std::pair<int32_t, int32_t>> macro_blockage_interval_list;
  int32_t rail_margin = getPdnRailMargin();
  for (int32_t instance_idx : _placed_macro_idx_list) {
    Instance& instance = database.get_instance_list()[instance_idx];
    PlanarRect& placement_halo_rect = instance.get_placement_halo_rect();
    if (row.get_ur_y() + rail_margin <= placement_halo_rect.get_ll_y() || placement_halo_rect.get_ur_y() <= row.get_ll_y() - rail_margin) {
      continue;
    }
    int32_t site_origin_x = row.get_site_origin_x();
    int32_t start_x_coord = std::max(row.get_ll_x(), site_origin_x + FPUTIL.alignDown(placement_halo_rect.get_ll_x() - site_origin_x, site.get_width()));
    int32_t end_x_coord = std::min(row.get_ur_x(), site_origin_x + FPUTIL.alignUp(placement_halo_rect.get_ur_x() - site_origin_x, site.get_width()));
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
  std::map<int32_t, std::vector<PPRegion*>> rows;
  for (auto& region : pp_model.get_pp_region_list()) {
    rows[region.get_y_coord()].push_back(&region);
  }
  for (auto& [y_coord, regions] : rows) {
    std::sort(regions.begin(), regions.end(), [](const PPRegion* first, const PPRegion* second) {
      return first->get_start_coord() < second->get_start_coord();
    });
    const int32_t site_height = regions.front()->get_site_height();
    if (site_height > 0) {
      if (rows.find(y_coord - site_height) == rows.end()) {
        for (PPRegion* region : regions) {
          addPPBoundaryRegion(pp_model, *region, region->get_start_coord(), region->get_end_coord(), PPBoundaryType::kBottom);
        }
      }
      if (rows.find(y_coord + site_height) == rows.end()) {
        for (PPRegion* region : regions) {
          addPPBoundaryRegion(pp_model, *region, region->get_start_coord(), region->get_end_coord(), PPBoundaryType::kTop);
        }
      }
    }
    for (size_t index = 0; index + 1 < regions.size(); ++index) {
      const int32_t gap_start = regions[index]->get_end_coord();
      const int32_t gap_end = regions[index + 1]->get_start_coord();
      if (gap_start >= gap_end) {
        continue;
      }
      const int32_t site_height = regions[index]->get_site_height();
      for (int32_t offset : {-site_height, site_height}) {
        auto adjacent_iter = rows.find(y_coord + offset);
        if (adjacent_iter == rows.end()) {
          continue;
        }
        const PPBoundaryType boundary_type = offset > 0 ? PPBoundaryType::kBottom : PPBoundaryType::kTop;
        for (PPRegion* adjacent : adjacent_iter->second) {
          const int32_t start_coord = std::max(gap_start, adjacent->get_start_coord());
          const int32_t end_coord = std::min(gap_end, adjacent->get_end_coord());
          if (start_coord < end_coord) {
            addPPBoundaryRegion(pp_model, *adjacent, start_coord, end_coord, boundary_type);
          }
        }
      }
    }
  }
}

void PhyPlacer::addPPBoundaryRegion(PPModel& pp_model, PPRegion& pp_region, int32_t start_coord, int32_t end_coord, PPBoundaryType boundary_type)
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
        || boundary_region.get_site_origin_x() != pp_region.get_site_origin_x() || boundary_region.get_site_width() != pp_region.get_site_width()
        || boundary_region.get_site_height() != pp_region.get_site_height() || boundary_region.get_boundary_type() != boundary_type
        || merged_end_coord < boundary_region.get_start_coord() || boundary_region.get_end_coord() < merged_start_coord) {
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

void PhyPlacer::insertCornerEndcap(PPModel& pp_model, int32_t& endcap_idx)
{
  // Row-end corners are physical endcaps, independent of IO ring corner cells.
  const Config& config = FPDM.getConfig();
  auto corners = config.corner_cell_names;
  if (corners.empty() && config.io_placer_mode == IOPlacerMode::kIOPin) {
    for (const auto* vertical : {"top", "bottom"}) {
      for (const auto* side : {"left", "right"}) {
        for (const auto* kind : {"inner", "outer"}) {
          const std::string role = std::string(vertical) + "_" + side + "_" + kind;
          corners[role] = std::string(side) == "left" ? config.left_endcap_name : config.right_endcap_name;
        }
      }
    }
  }
  std::map<std::pair<int32_t, int32_t>, std::pair<std::string, PlacementOrientation>> placed;
  for (auto& boundary : pp_model.get_boundary_region_list()) {
    for (bool left : {true, false}) {
      bool outer = false;
      const int32_t edge = left ? boundary.get_start_coord() : boundary.get_end_coord();
      for (const auto& region : pp_model.get_pp_region_list()) {
        if (region.get_y_coord() == boundary.get_y_coord() && region.get_start_coord() < boundary.get_end_coord()
            && boundary.get_start_coord() < region.get_end_coord()
            && edge == (left ? region.get_start_coord() : region.get_end_coord())) {
          outer = true;
          break;
        }
      }
      const std::string role = std::string(boundary.get_boundary_type() == PPBoundaryType::kTop ? "top_" : "bottom_")
                               + (left ? "left_" : "right_") + (outer ? "outer" : "inner");
      auto selected = corners.find(role);
      if (selected == corners.end()) {
        continue;
      }
      auto& master = FPDM.getDatabase().get_cell_master_map().at(selected->second);
      const int32_t width = getCellMasterWidthByOrient(master, boundary.get_orient());
      if (width <= 0) {
        continue;
      }
      auto orient = boundary.get_orient();
      const auto& orientations = FPDM.getConfig().phy_orientations;
      auto mapping = orientations.find(role);
      if (mapping != orientations.end() && mapping->second.contains(orient)) {
        orient = mapping->second.at(orient);
      }
      // Convex corners occupy the exposed interval; concave corners flank it.
      const int32_t preferred_x = left == outer ? edge : edge - width;
      const int32_t alternate_x = left == outer ? edge - width : edge;
      bool placed_role = false;
      for (int32_t x : {preferred_x, alternate_x}) {
        const auto key = std::pair{boundary.get_y_coord(), x};
        if (auto previous = placed.find(key); previous != placed.end()) {
          if (previous->second == std::pair{selected->second, orient}) {
            placed_role = true;
            break;
          }
          continue;
        }
        auto owner = std::find_if(pp_model.get_pp_region_list().begin(), pp_model.get_pp_region_list().end(), [&](const auto& region) {
          return region.get_y_coord() == boundary.get_y_coord() && region.get_start_coord() <= x && x + width <= region.get_end_coord();
        });
        if (owner == pp_model.get_pp_region_list().end() || !isCellAvailable(pp_model, x, x + width, boundary.get_y_coord())) {
          continue;
        }
        addPhyCell(pp_model, *owner, "BNDRY_CORNER_" + std::to_string(endcap_idx++), selected->second, x, role);
        placed.emplace(key, std::pair{selected->second, orient});
        placed_role = true;
        break;
      }
      if (!placed_role) {
        FPLOG.warn(Loc::current(), "Skipping overlapping physical corner role '", role, "' at edge=", edge, ", y=", boundary.get_y_coord(), ".");
      }
    }
  }
  if (!corners.empty()) {
    FPLOG.info(Loc::current(), "Placed ", placed.size(), " physical corners on usable row intervals.");
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
      FPLOG.error(Loc::current(), "Invalid side-endcap master width.");
    }
    const int32_t region_width = pp_region.get_end_coord() - pp_region.get_start_coord();
    if (region_width < left_endcap_width + right_endcap_width
        && !(config.left_endcap_name == config.right_endcap_name && region_width == left_endcap_width)) {
      FPLOG.warn(Loc::current(), "Skipping side endcaps: row interval [", pp_region.get_start_coord(), ",", pp_region.get_end_coord(),
                 "] is too narrow for both side endcaps at y=", pp_region.get_y_coord(), ".");
      continue;
    }
    int32_t left_endcap_x_coord
        = pp_region.get_site_origin_x() + FPUTIL.alignUp(pp_region.get_start_coord() - pp_region.get_site_origin_x(), pp_region.get_site_width());
    int32_t right_endcap_x_coord
        = pp_region.get_site_origin_x()
          + FPUTIL.alignDown(pp_region.get_end_coord() - right_endcap_width - pp_region.get_site_origin_x(), pp_region.get_site_width());
    if (left_endcap_x_coord + left_endcap_width <= pp_region.get_end_coord()
        && isCellAvailable(pp_model, left_endcap_x_coord, left_endcap_x_coord + left_endcap_width, pp_region.get_y_coord())) {
      addPhyCell(pp_model, pp_region, "BNDRY_CAP_" + std::to_string(endcap_idx++), config.left_endcap_name, left_endcap_x_coord, "left");
    }
    if (left_endcap_x_coord + left_endcap_width <= right_endcap_x_coord
        && isCellAvailable(pp_model, right_endcap_x_coord, right_endcap_x_coord + right_endcap_width, pp_region.get_y_coord())) {
      addPhyCell(pp_model, pp_region, "BNDRY_CAP_" + std::to_string(endcap_idx++), config.right_endcap_name, right_endcap_x_coord, "right");
    }
  }
}

int32_t PhyPlacer::getCellMasterWidthByOrient(CellMaster& cell_master, PlacementOrientation orient)
{
  if (orient == PlacementOrientation::kN || orient == PlacementOrientation::kS || orient == PlacementOrientation::kFN || orient == PlacementOrientation::kFS) {
    return cell_master.get_width();
  }
  return cell_master.get_height();
}

void PhyPlacer::addPhyCell(PPModel& pp_model, PPRegion& pp_region, std::string instance_name, std::string cell_master_name, int32_t x_coord,
                         const std::string& role)
{
  Database& database = FPDM.getDatabase();
  CellMaster& cell_master = database.get_cell_master_map()[cell_master_name];
  if (!isPhyCellOnSite(pp_region, cell_master, x_coord)) {
    FPLOG.error(Loc::current(), "Physical cell '", cell_master_name, "' does not fit its row/site interval.");
  }
  auto orient = pp_region.get_orient();
  auto role_it = FPDM.getConfig().phy_orientations.find(role);
  if (role_it != FPDM.getConfig().phy_orientations.end()) {
    auto mapping = role_it->second.find(orient);
    if (mapping != role_it->second.end()) {
      orient = mapping->second;
    }
  }
  const bool legal_orient = orient == PlacementOrientation::kN
                            || (orient == PlacementOrientation::kFS && cell_master.get_symmetry_x())
                            || (orient == PlacementOrientation::kFN && cell_master.get_symmetry_y())
                            || (orient == PlacementOrientation::kS && cell_master.get_symmetry_x() && cell_master.get_symmetry_y());
  if (!legal_orient) {
    FPLOG.error(Loc::current(), "Physical-cell orientation is not permitted by LEF SYMMETRY for '", cell_master_name, "', role '", role, "'.");
  }
  if (!isCellAvailable(pp_model, x_coord, x_coord + cell_master.get_width(), pp_region.get_y_coord())) {
    FPLOG.error(Loc::current(), "Overlapping physical cell '", instance_name, "'.");
  }
  int32_t cell_width = getCellMasterWidthByOrient(cell_master, pp_region.get_orient());
  Instance instance;
  instance.set_name(instance_name);
  instance.set_master_name(cell_master_name);
  instance.set_orient(orient);
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
  size_t occupied_region_idx = pp_model.get_occupied_region_list().size();
  pp_model.get_occupied_region_list().push_back(occupied_region);
  _occupied_region_indices[occupied_region.get_y_coord()].push_back(occupied_region_idx);
}

bool PhyPlacer::isPhyCellOnSite(PPRegion& pp_region, CellMaster& cell_master, int32_t x_coord)
{
  int32_t cell_width = getCellMasterWidthByOrient(cell_master, pp_region.get_orient());
  if (cell_width <= 0 || pp_region.get_site_width() <= 0 || pp_region.get_site_height() <= 0 || x_coord < pp_region.get_start_coord()
      || pp_region.get_end_coord() < x_coord + cell_width || (x_coord - pp_region.get_site_origin_x()) % pp_region.get_site_width() != 0
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

void PhyPlacer::insertWellTapInRegion(PPModel& pp_model, PPRegion& pp_region, int32_t tap_distance, int32_t tap_offset, int32_t& tapcell_idx)
{
  Database& database = FPDM.getDatabase();
  Config& config = FPDM.getConfig();
  CellMaster& tapcell_master = database.get_cell_master_map()[config.tapcell_name];
  int32_t tapcell_width = getCellMasterWidthByOrient(tapcell_master, pp_region.get_orient());
  if (tapcell_width <= 0) {
    return;
  }
  int32_t left_endcap_width = 0;
  int32_t right_endcap_width = 0;
  auto left_endcap_iter = database.get_cell_master_map().find(config.left_endcap_name);
  if (left_endcap_iter != database.get_cell_master_map().end()) {
    left_endcap_width = getCellMasterWidthByOrient(left_endcap_iter->second, pp_region.get_orient());
  }
  auto right_endcap_iter = database.get_cell_master_map().find(config.right_endcap_name);
  if (right_endcap_iter != database.get_cell_master_map().end()) {
    right_endcap_width = getCellMasterWidthByOrient(right_endcap_iter->second, pp_region.get_orient());
  }
  const int32_t boundary_guard = std::max(left_endcap_width, right_endcap_width);
  const int32_t row_ll_y = pp_region.get_y_coord();
  const int32_t row_ur_y = row_ll_y + pp_region.get_site_height();
  auto near_macro_edge = [&](int32_t x_coord, int32_t cell_width) {
    for (int32_t macro_idx : _placed_macro_idx_list) {
      const PlanarRect& halo = database.get_instance_list()[macro_idx].get_placement_halo_rect();
      if (halo.get_ur_y() <= row_ll_y || row_ur_y <= halo.get_ll_y()) {
        continue;
      }
      for (int32_t edge : {halo.get_ll_x(), halo.get_ur_x()}) {
        if (x_coord < edge + boundary_guard && edge - boundary_guard < x_coord + cell_width) {
          return true;
        }
      }
    }
    return false;
  };
  auto near_boundary_edge = [&](int32_t x_coord, int32_t cell_width) {
    for (const PPRegion& boundary : pp_model.get_boundary_region_list()) {
      if (std::abs(boundary.get_y_coord() - row_ll_y) > pp_region.get_site_height()) {
        continue;
      }
      for (int32_t edge : {boundary.get_start_coord(), boundary.get_end_coord()}) {
        if (x_coord < edge + boundary_guard && edge - boundary_guard < x_coord + cell_width) {
          return true;
        }
      }
    }
    return false;
  };
  int32_t x_coord = pp_region.get_site_origin_x() + tap_offset;
  while (x_coord < pp_region.get_start_coord()) {
    x_coord += tap_distance;
  }
  while (x_coord + tapcell_width <= pp_region.get_end_coord()) {
    int32_t search_end_coord = std::min(pp_region.get_end_coord(), x_coord + tap_distance);
    int32_t available_x_coord = getAvailableCellCoord(pp_model, pp_region, x_coord, search_end_coord, tapcell_master);
    if (available_x_coord >= 0) {
      const bool inside_endcap_guard = available_x_coord < pp_region.get_start_coord() + boundary_guard
                                       || pp_region.get_end_coord() - boundary_guard < available_x_coord + tapcell_width;
      if (!inside_endcap_guard && !near_macro_edge(available_x_coord, tapcell_width)
          && !near_boundary_edge(available_x_coord, tapcell_width)) {
        addPhyCell(pp_model, pp_region, "WELLTAP_" + std::to_string(tapcell_idx++), config.tapcell_name, available_x_coord);
      }
    }
    x_coord += tap_distance;
  }
}

int32_t PhyPlacer::getAvailableCellCoord(PPModel& pp_model, PPRegion& pp_region, int32_t start_coord, int32_t end_coord, CellMaster& cell_master)
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
  auto iter = _occupied_region_indices.find(y_coord);
  if (iter == _occupied_region_indices.end()) {
    return true;
  }
  std::vector<PPRegion>& occupied_region_list = pp_model.get_occupied_region_list();
  for (size_t occupied_region_idx : iter->second) {
    PPRegion& occupied_region = occupied_region_list[occupied_region_idx];
    if (end_coord <= occupied_region.get_start_coord() || occupied_region.get_end_coord() <= start_coord) {
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
        addPhyCell(pp_model, boundary_region, "BNDRY_CAP_TAP_" + std::to_string(boundary_tap_idx++), boundary_tap_name, available_x_coord,
                   boundary_region.get_boundary_type() == PPBoundaryType::kTop ? "top" : "bottom");
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
  auto iter = _occupied_region_indices.find(boundary_region.get_y_coord());
  if (iter != _occupied_region_indices.end()) {
    std::vector<PPRegion>& occupied_region_list = pp_model.get_occupied_region_list();
    for (size_t occupied_region_idx : iter->second) {
      PPRegion& occupied_region = occupied_region_list[occupied_region_idx];
      if (occupied_region.get_end_coord() <= boundary_region.get_start_coord()
          || boundary_region.get_end_coord() <= occupied_region.get_start_coord()) {
        continue;
      }
      int32_t start_coord = std::max(occupied_region.get_start_coord(), boundary_region.get_start_coord());
      int32_t end_coord = std::min(occupied_region.get_end_coord(), boundary_region.get_end_coord());
      occupied_interval_list.emplace_back(start_coord, end_coord);
    }
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

void PhyPlacer::fillEdgeEndcap(PPModel& pp_model, PPRegion& empty_region, std::vector<std::string>& endcap_name_list, int32_t& endcap_idx)
{
  const int32_t site = empty_region.get_site_width();
  const int32_t length = empty_region.get_end_coord() - empty_region.get_start_coord();
  if (site <= 0 || length % site != 0) {
    FPLOG.error(Loc::current(), "Endcap interval is not on the row site grid.");
  }
  const int32_t sites = length / site;
  std::vector<int32_t> cost(sites + 1, INT32_MAX);
  std::vector<int32_t> choice(sites + 1, -1);
  cost[0] = 0;
  for (int32_t n = 1; n <= sites; ++n) {
    for (size_t c = 0; c < endcap_name_list.size(); ++c) {
      auto& master = FPDM.getDatabase().get_cell_master_map().at(endcap_name_list[c]);
      const int32_t width = master.get_width() / site;
      if (master.get_width() % site || master.get_height() != empty_region.get_site_height() || width <= 0 || width > n
          || cost[n - width] == INT32_MAX) {
        continue;
      }
      if (cost[n - width] + 1 < cost[n]) {
        cost[n] = cost[n - width] + 1;
        choice[n] = c;
      }
    }
  }
  if (choice[sites] < 0 && sites != 0) {
    FPLOG.error(Loc::current(), "Cannot exactly fill endcap interval [", empty_region.get_start_coord(), ",", empty_region.get_end_coord(),
                "] at y=", empty_region.get_y_coord(), " with configured masters.");
  }
  int32_t x = empty_region.get_start_coord();
  for (int32_t remaining = sites; remaining > 0;) {
    const auto& name = endcap_name_list[choice[remaining]];
    auto& master = FPDM.getDatabase().get_cell_master_map().at(name);
    const std::string role = empty_region.get_boundary_type() == PPBoundaryType::kTop ? "top" : "bottom";
    addPhyCell(pp_model, empty_region, "BNDRY_CAP_" + std::to_string(endcap_idx++), name, x, role);
    x += master.get_width();
    remaining -= master.get_width() / site;
  }
}

// private

PhyPlacer* PhyPlacer::_pp_instance = nullptr;

}  // namespace ifp
