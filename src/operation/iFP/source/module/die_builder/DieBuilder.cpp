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
#include "DieBuilder.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ifp {

// public

void DieBuilder::initInst()
{
  if (_db_instance == nullptr) {
    _db_instance = new DieBuilder();
  }
}

DieBuilder& DieBuilder::getInst()
{
  if (_db_instance == nullptr) {
    FPLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_db_instance;
}

void DieBuilder::destroyInst()
{
  if (_db_instance != nullptr) {
    delete _db_instance;
    _db_instance = nullptr;
  }
}

// function

void DieBuilder::build()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  buildFloorplan();
  buildTrackList();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DieBuilder::buildFloorplan()
{
  Config& config = FPDM.getConfig();
  double die_width_micron = -1.0;
  double die_height_micron = -1.0;
  if (config.die_mode == DieMode::kDieUtil) {
    Database& database = FPDM.getDatabase();
    if (config.die_utilization <= 0.0 || config.die_utilization > 1.0) {
      FPLOG.error(Loc::current(), "Die utilization must be in (0, 1]!");
      return;
    }
    if (config.die_aspect_ratio <= 0.0) {
      FPLOG.error(Loc::current(), "Die aspect ratio must be greater than 0!");
      return;
    }
    if (database.get_cell_area() <= 0.0) {
      FPLOG.error(Loc::current(), "Cell area must be greater than 0!");
      return;
    }
    if (database.get_micron_dbu() <= 0) {
      FPLOG.error(Loc::current(), "Micron DBU must be greater than 0!");
      return;
    }
    if (config.die_margin_left_micron < 0.0 || config.die_margin_right_micron < 0.0 || config.die_margin_top_micron < 0.0
        || config.die_margin_bottom_micron < 0.0) {
      FPLOG.error(Loc::current(), "Die margins must not be negative!");
      return;
    }

    auto site_iter = database.get_site_map().find(config.die_site_name);
    if (site_iter == database.get_site_map().end() || site_iter->second.get_width() <= 0 || site_iter->second.get_height() <= 0) {
      FPLOG.error(Loc::current(), "The site '", config.die_site_name, "' does not exist or has invalid dimensions!");
      return;
    }

    int32_t micron_dbu = database.get_micron_dbu();
    Site& core_site = site_iter->second;
    double core_area = database.get_cell_area() / config.die_utilization;
    double core_height_micron = std::sqrt(core_area / config.die_aspect_ratio);
    double core_width_micron = core_area / core_height_micron;
    int32_t core_width = FPUTIL.alignUp(static_cast<int32_t>(std::ceil(core_width_micron * micron_dbu)), core_site.get_width());
    int32_t core_height = FPUTIL.alignUp(static_cast<int32_t>(std::ceil(core_height_micron * micron_dbu)), core_site.get_height());
    int32_t margin_left = FPUTIL.transMicronToDBU(config.die_margin_left_micron, micron_dbu);
    int32_t margin_right = FPUTIL.transMicronToDBU(config.die_margin_right_micron, micron_dbu);
    int32_t margin_top = FPUTIL.transMicronToDBU(config.die_margin_top_micron, micron_dbu);
    int32_t margin_bottom = FPUTIL.transMicronToDBU(config.die_margin_bottom_micron, micron_dbu);

    die_width_micron = (margin_left + core_width + margin_right) / static_cast<double>(micron_dbu);
    die_height_micron = (margin_bottom + core_height + margin_top) / static_cast<double>(micron_dbu);
    buildDie(0.0, 0.0, die_width_micron, die_height_micron);
    buildCore(config.die_margin_left_micron, config.die_margin_bottom_micron, (margin_left + core_width) / static_cast<double>(micron_dbu),
              (margin_bottom + core_height) / static_cast<double>(micron_dbu), config.die_site_name);
    return;
  } else if (config.die_mode == DieMode::kDieSize) {
    die_width_micron = config.die_width_micron;
    die_height_micron = config.die_height_micron;
  } else {
    return;
  }

  buildDie(0.0, 0.0, die_width_micron, die_height_micron);
  buildCore(config.die_margin_left_micron, config.die_margin_bottom_micron, die_width_micron - config.die_margin_right_micron,
            die_height_micron - config.die_margin_top_micron, config.die_site_name);
}

void DieBuilder::buildDie(double die_lx, double die_ly, double die_ux, double die_uy)
{
  Database& database = FPDM.getDatabase();
  Die& die = database.get_die();
  die.set_rect(FPUTIL.transMicronToDBU(die_lx, database.get_micron_dbu()), FPUTIL.transMicronToDBU(die_ly, database.get_micron_dbu()),
               FPUTIL.transMicronToDBU(die_ux, database.get_micron_dbu()), FPUTIL.transMicronToDBU(die_uy, database.get_micron_dbu()));
  FPDM.getDatabase().set_die_updated(true);
}

void DieBuilder::buildCore(double core_lx, double core_ly, double core_ux, double core_uy, std::string site_name)
{
  Database& database = FPDM.getDatabase();
  Site& core_site = database.get_site_map()[site_name];

  int32_t site_width = core_site.get_width();
  int32_t site_height = core_site.get_height();
  int32_t requested_lx = FPUTIL.transMicronToDBU(core_lx, database.get_micron_dbu());
  int32_t requested_ly = FPUTIL.transMicronToDBU(core_ly, database.get_micron_dbu());
  int32_t requested_ux = FPUTIL.transMicronToDBU(core_ux, database.get_micron_dbu());
  int32_t requested_uy = FPUTIL.transMicronToDBU(core_uy, database.get_micron_dbu());
  int32_t core_width = FPUTIL.alignDown(requested_ux - requested_lx, site_width);
  int32_t core_height = FPUTIL.alignDown(requested_uy - requested_ly, site_height);
  int32_t core_lx_int = requested_lx;
  int32_t core_ly_int = requested_ly;
  int32_t core_ux_int = core_lx_int + core_width;
  int32_t core_uy_int = core_ly_int + core_height;

  Core& core = database.get_core();
  core.set_rect(core_lx_int, core_ly_int, core_ux_int, core_uy_int);
  core.set_core_site_name(site_name);
  core.set_io_site_name(site_name);
  core.set_corner_site_name(site_name);
  database.set_core_updated(true);
  buildRowList();
}

void DieBuilder::buildRowList()
{
  Database& database = FPDM.getDatabase();
  Core& core = database.get_core();
  Site& core_site = database.get_site_map()[core.get_core_site_name()];
  std::vector<Row>& new_row_list = database.get_new_row_list();
  std::vector<Row>& row_list = database.get_row_list();

  new_row_list.clear();
  row_list.clear();

  int32_t site_height = core_site.get_height();
  int32_t row_num = std::abs(core.get_height()) / site_height;
  for (int32_t row_idx = 0; row_idx < row_num; row_idx++) {
    int32_t y_coord = core.get_ll_y() + row_idx * site_height;
    Row row;
    row.set_name("ROW_" + std::to_string(row_idx));
    row.set_site_name(core.get_core_site_name());
    row.set_site_origin_x(core.get_ll_x());
    row.set_y(y_coord);
    row.set_orient(row_idx % 2 == 0 ? PlacementOrientation::kFS : PlacementOrientation::kN);
    row.set_rect(core.get_ll_x(), y_coord, core.get_ur_x(), y_coord + site_height);
    new_row_list.push_back(row);
    row_list.push_back(row);
  }
}

void DieBuilder::buildTrackList()
{
  Database& database = FPDM.getDatabase();
  database.get_new_track_list().clear();
  for (RoutingLayer& routing_layer : database.get_routing_layer_list()) {
    int32_t x_pitch = routing_layer.get_pitch_x() > 0 ? routing_layer.get_pitch_x() : routing_layer.get_prefer_track_pitch();
    int32_t y_pitch = routing_layer.get_pitch_y() > 0 ? routing_layer.get_pitch_y() : routing_layer.get_prefer_track_pitch();
    if (x_pitch <= 0 || y_pitch <= 0) {
      continue;
    }

    int32_t offset = std::max(routing_layer.get_prefer_track_offset(), 0);
    buildTrack(routing_layer.get_name(), offset, x_pitch, offset, y_pitch);
  }
}

void DieBuilder::buildTrack(std::string layer_name, int32_t x_offset, int32_t x_pitch, int32_t y_offset, int32_t y_pitch)
{
  Track track;
  track.set_layer_name(layer_name);
  track.set_x_offset(x_offset);
  track.set_x_pitch(x_pitch);
  track.set_y_offset(y_offset);
  track.set_y_pitch(y_pitch);
  FPDM.getDatabase().get_new_track_list().push_back(track);
  FPDM.getDatabase().set_track_updated(true);
}

// private

DieBuilder* DieBuilder::_db_instance = nullptr;

}  // namespace ifp
