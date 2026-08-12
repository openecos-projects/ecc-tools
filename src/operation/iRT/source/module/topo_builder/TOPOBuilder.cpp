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
#include "TOPOBuilder.hpp"

#include "Utility.hpp"
#include "flute3/flute.h"

namespace irt {

// public

void TOPOBuilder::initInst()
{
  if (_tb_instance == nullptr) {
    _tb_instance = new TOPOBuilder();
  }
}

TOPOBuilder& TOPOBuilder::getInst()
{
  if (_tb_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_tb_instance;
}

void TOPOBuilder::destroyInst()
{
  if (_tb_instance != nullptr) {
    delete _tb_instance;
    _tb_instance = nullptr;
  }
}

// function

void TOPOBuilder::init()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  Flute::readLUT();

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

std::vector<Segment<PlanarCoord>> TOPOBuilder::getPlanarTopoList(const TBTask& tb_task)
{
  TBSteinerRepairStat steiner_repair_stat;
  return getPlanarTopoList(tb_task, steiner_repair_stat);
}

std::vector<Segment<PlanarCoord>> TOPOBuilder::getPlanarTopoList(const TBTask& tb_task, TBSteinerRepairStat& steiner_repair_stat)
{
  steiner_repair_stat = {};
  std::vector<Segment<PlanarCoord>> raw_topo_list = getFlutePlanarTopoList(tb_task.get_planar_coord_list());
  return legalizePlanarTopo(tb_task, std::move(raw_topo_list), steiner_repair_stat);
}

void TOPOBuilder::destroy()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  Flute::deleteLUT();

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

TOPOBuilder* TOPOBuilder::_tb_instance = nullptr;

std::vector<Segment<PlanarCoord>> TOPOBuilder::getFlutePlanarTopoList(const std::vector<PlanarCoord>& planar_coord_list)
{
  std::vector<Segment<PlanarCoord>> planar_topo_list;
  if (planar_coord_list.size() <= 1) {
    return planar_topo_list;
  }

  int32_t point_num = static_cast<int32_t>(planar_coord_list.size());
  std::vector<Flute::DTYPE> x_list(point_num);
  std::vector<Flute::DTYPE> y_list(point_num);
  for (int32_t i = 0; i < point_num; i++) {
    x_list[i] = planar_coord_list[i].get_x();
    y_list[i] = planar_coord_list[i].get_y();
  }
  Flute::Tree flute_tree = Flute::flute(point_num, x_list.data(), y_list.data(), FLUTE_ACCURACY);
  for (int32_t i = 0; i < 2 * flute_tree.deg - 2; i++) {
    int32_t neighbor_idx = flute_tree.branch[i].n;
    PlanarCoord first_coord(flute_tree.branch[i].x, flute_tree.branch[i].y);
    PlanarCoord second_coord(flute_tree.branch[neighbor_idx].x, flute_tree.branch[neighbor_idx].y);
    if (first_coord != second_coord) {
      planar_topo_list.emplace_back(first_coord, second_coord);
    }
  }
  Flute::free_tree(flute_tree);
  return planar_topo_list;
}

std::vector<Segment<PlanarCoord>> TOPOBuilder::legalizePlanarTopo(const TBTask& tb_task,
                                                                 std::vector<Segment<PlanarCoord>> raw_topo_list,
                                                                 TBSteinerRepairStat& steiner_repair_stat)
{
  const std::vector<PlanarRect>& planar_obs_list = tb_task.get_planar_obs_list();
  if (planar_obs_list.empty()) {
    return raw_topo_list;
  }
  if (!tb_task.has_planar_search_region() || tb_task.get_planar_search_region().isIncorrect()) {
    RTLOG.error(Loc::current(), "The planar search region is invalid!");
    return raw_topo_list;
  }
  for (const PlanarRect& planar_obs : planar_obs_list) {
    if (planar_obs.isIncorrect()) {
      RTLOG.error(Loc::current(), "The planar obstacle is invalid!");
      return raw_topo_list;
    }
  }

  std::set<PlanarCoord, CmpPlanarCoordByXASC> terminal_coord_set(tb_task.get_planar_coord_list().begin(),
                                                                tb_task.get_planar_coord_list().end());
  auto isInsideSearchRegion = [&](const PlanarCoord& coord) {
    const PlanarRect& planar_search_region = tb_task.get_planar_search_region();
    return planar_search_region.get_ll_x() <= coord.get_x() && coord.get_x() <= planar_search_region.get_ur_x()
           && planar_search_region.get_ll_y() <= coord.get_y() && coord.get_y() <= planar_search_region.get_ur_y();
  };
  std::map<PlanarCoord, PlanarCoord, CmpPlanarCoordByXASC> steiner_legal_coord_map;
  auto legalizeSteinerCoord = [&](const PlanarCoord& coord) {
    if (terminal_coord_set.find(coord) != terminal_coord_set.end() || !isSteinerForbiddenCoord(planar_obs_list, coord)) {
      return coord;
    }
    auto legal_iter = steiner_legal_coord_map.find(coord);
    if (legal_iter != steiner_legal_coord_map.end()) {
      return legal_iter->second;
    }

    steiner_repair_stat.raw_steiner_in_macro++;
    PlanarCoord legal_coord = getNearestLegalCoord(planar_obs_list, tb_task.get_planar_search_region(), coord);
    steiner_legal_coord_map[coord] = legal_coord;
    if (isSteinerForbiddenCoord(planar_obs_list, legal_coord)) {
      steiner_repair_stat.failed_steiner_legalize_num++;
      const PlanarRect& planar_search_region = tb_task.get_planar_search_region();
      RTLOG.warn(Loc::current(), "steiner_legalize_failed, coord: (", coord.get_x(), ",", coord.get_y(), "), reason: ",
                 isInsideSearchRegion(coord) ? "no_legal_coordinate_in_search_region" : "raw_steiner_outside_search_region",
                 ", search_region: (", planar_search_region.get_ll_x(), ",", planar_search_region.get_ll_y(), ")-(",
                 planar_search_region.get_ur_x(), ",", planar_search_region.get_ur_y(), "), obstacle_num: ", planar_obs_list.size(),
                 ", terminal_num: ", terminal_coord_set.size());
    } else {
      steiner_repair_stat.fixed_steiner_in_macro++;
    }
    return legal_coord;
  };

  std::vector<Segment<PlanarCoord>> legal_topo_list;
  legal_topo_list.reserve(raw_topo_list.size());
  for (Segment<PlanarCoord>& raw_topo : raw_topo_list) {
    PlanarCoord first_coord = legalizeSteinerCoord(raw_topo.get_first());
    PlanarCoord second_coord = legalizeSteinerCoord(raw_topo.get_second());
    if (first_coord != second_coord) {
      legal_topo_list.emplace_back(first_coord, second_coord);
    }
  }
  return legal_topo_list;
}

PlanarCoord TOPOBuilder::getNearestLegalCoord(const std::vector<PlanarRect>& planar_obs_list, const PlanarRect& planar_search_region,
                                              const PlanarCoord& coord)
{
  auto isInsideSearchRegion = [&](const PlanarCoord& candidate_coord) {
    return planar_search_region.get_ll_x() <= candidate_coord.get_x() && candidate_coord.get_x() <= planar_search_region.get_ur_x()
           && planar_search_region.get_ll_y() <= candidate_coord.get_y() && candidate_coord.get_y() <= planar_search_region.get_ur_y();
  };
  if (!isInsideSearchRegion(coord) || !isSteinerForbiddenCoord(planar_obs_list, coord)) {
    return coord;
  }

  int32_t max_radius = 0;
  for (const PlanarCoord& corner : {planar_search_region.get_ll(), PlanarCoord(planar_search_region.get_ll_x(), planar_search_region.get_ur_y()),
                                    PlanarCoord(planar_search_region.get_ur_x(), planar_search_region.get_ll_y()), planar_search_region.get_ur()}) {
    max_radius = std::max(max_radius, RTUTIL.getManhattanDistance(coord, corner));
  }
  for (int32_t radius = 1; radius <= max_radius; radius++) {
    bool found_legal_coord = false;
    bool best_is_axis_aligned = false;
    PlanarCoord best_coord;
    auto updateBest = [&](int32_t x, int32_t y) {
      PlanarCoord candidate_coord(x, y);
      if (!isInsideSearchRegion(candidate_coord) || isSteinerForbiddenCoord(planar_obs_list, candidate_coord)) {
        return;
      }
      bool candidate_is_axis_aligned = candidate_coord.get_x() == coord.get_x() || candidate_coord.get_y() == coord.get_y();
      if (!found_legal_coord || (candidate_is_axis_aligned && !best_is_axis_aligned)
          || (candidate_is_axis_aligned == best_is_axis_aligned && CmpPlanarCoordByXASC()(candidate_coord, best_coord))) {
        best_coord = candidate_coord;
        best_is_axis_aligned = candidate_is_axis_aligned;
        found_legal_coord = true;
      }
    };
    for (int32_t dx = -radius; dx <= radius; dx++) {
      updateBest(coord.get_x() + dx, coord.get_y() - radius);
      updateBest(coord.get_x() + dx, coord.get_y() + radius);
    }
    for (int32_t dy = -radius + 1; dy <= radius - 1; dy++) {
      updateBest(coord.get_x() - radius, coord.get_y() + dy);
      updateBest(coord.get_x() + radius, coord.get_y() + dy);
    }
    if (found_legal_coord) {
      return best_coord;
    }
  }
  return coord;
}

bool TOPOBuilder::isSteinerForbiddenCoord(const std::vector<PlanarRect>& planar_obs_list, const PlanarCoord& coord)
{
  for (const PlanarRect& planar_obs : planar_obs_list) {
    if (planar_obs.get_ll_x() <= coord.get_x() && coord.get_x() <= planar_obs.get_ur_x()
        && planar_obs.get_ll_y() <= coord.get_y() && coord.get_y() <= planar_obs.get_ur_y()) {
      return true;
    }
  }
  return false;
}

}  // namespace irt
