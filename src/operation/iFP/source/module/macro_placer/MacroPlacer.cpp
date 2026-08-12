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

void MacroPlacer::checkMacroInCore()
{
  Core& core = FPDM.getDatabase().get_core();
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }
    PlanarRect& macro_rect = instance.get_bounding_rect();
    if (core.get_ll_x() <= macro_rect.get_ll_x() && macro_rect.get_ur_x() <= core.get_ur_x()
        && core.get_ll_y() <= macro_rect.get_ll_y() && macro_rect.get_ur_y() <= core.get_ur_y()) {
      continue;
    }
    FPLOG.warn(Loc::current(), "The macro '", instance.get_name(), "' is placed outside core!");
  }
}

void MacroPlacer::buildMacroPlacementHalo(MPComParam& mp_com_param)
{
  Database& database = FPDM.getDatabase();
  int32_t halo = FPUTIL.transMicronToDBU(mp_com_param.get_placement_halo_micron(), database.get_micron_dbu());
  for (Instance& instance : database.get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }
    PlanarRect& macro_rect = instance.get_bounding_rect();
    instance.get_placement_halo_rect().set_rect(macro_rect.get_ll_x() - halo, macro_rect.get_ll_y() - halo, macro_rect.get_ur_x() + halo,
                                                macro_rect.get_ur_y() + halo);
  }
}

void MacroPlacer::buildMacroRoutingHalo(MPComParam& mp_com_param)
{
  Database& database = FPDM.getDatabase();
  int32_t halo = FPUTIL.transMicronToDBU(mp_com_param.get_routing_halo_micron(), database.get_micron_dbu());
  for (Instance& instance : database.get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }
    PlanarRect& macro_rect = instance.get_bounding_rect();
    instance.get_routing_halo_rect().set_rect(macro_rect.get_ll_x() - halo, macro_rect.get_ll_y() - halo, macro_rect.get_ur_x() + halo,
                                              macro_rect.get_ur_y() + halo);
  }
}

void MacroPlacer::cutRowList()
{
  Database& database = FPDM.getDatabase();
  std::vector<Row> cut_row_list;
  for (Row& row : database.get_row_list()) {
    cutRow(row, cut_row_list);
  }
  database.set_row_list(cut_row_list);
  database.set_new_row_list(cut_row_list);
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
  for (Instance& instance : database.get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }
    PlanarRect& placement_halo_rect = instance.get_placement_halo_rect();
    if (placement_halo_rect.get_ur_y() <= row.get_ll_y() || row.get_ur_y() <= placement_halo_rect.get_ll_y()) {
      continue;
    }
    int32_t site_origin_x = row.get_site_origin_x();
    int32_t start_x
        = std::max(row.get_ll_x(), site_origin_x + FPUTIL.alignDown(placement_halo_rect.get_ll_x() - site_origin_x, site.get_width()));
    int32_t end_x
        = std::min(row.get_ur_x(), site_origin_x + FPUTIL.alignUp(placement_halo_rect.get_ur_x() - site_origin_x, site.get_width()));
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

// private

MacroPlacer* MacroPlacer::_mp_instance = nullptr;

}  // namespace ifp
