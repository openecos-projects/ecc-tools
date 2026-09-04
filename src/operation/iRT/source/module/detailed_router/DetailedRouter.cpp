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
#include "DetailedRouter.hpp"

#include <numeric>

#include "DRBox.hpp"
#include "DRBoxId.hpp"
#include "DRCEngine.hpp"
#include "DRConnectivity.hpp"
#include "DRIterParam.hpp"
#include "DRNet.hpp"
#include "DRNode.hpp"
#include "GDSPlotter.hpp"
#include "Monitor.hpp"
#include "RTInterface.hpp"

namespace irt {

// public

void DetailedRouter::initInst()
{
  if (_dr_instance == nullptr) {
    _dr_instance = new DetailedRouter();
  }
}

DetailedRouter& DetailedRouter::getInst()
{
  if (_dr_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_dr_instance;
}

void DetailedRouter::destroyInst()
{
  if (_dr_instance != nullptr) {
    delete _dr_instance;
    _dr_instance = nullptr;
  }
}

// function

void DetailedRouter::route()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  DRModel dr_model = initDRModel();
  routeDRModel(dr_model);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DetailedRouter* DetailedRouter::_dr_instance = nullptr;

DRModel DetailedRouter::initDRModel()
{
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();

  DRModel dr_model;
  dr_model.set_dr_net_list(convertToDRNetList(net_list));
  readDRModel(dr_model);
  return dr_model;
}

std::vector<DRNet> DetailedRouter::convertToDRNetList(std::vector<Net>& net_list)
{
  std::vector<DRNet> dr_net_list;
  dr_net_list.reserve(net_list.size());
  for (Net& net : net_list) {
    dr_net_list.emplace_back(convertToDRNet(net));
  }
  return dr_net_list;
}

DRNet DetailedRouter::convertToDRNet(Net& net)
{
  DRNet dr_net;
  dr_net.set_origin_net(&net);
  dr_net.set_net_idx(net.get_net_idx());
  dr_net.set_connect_type(net.get_connect_type());
  for (Pin& pin : net.get_pin_list()) {
    dr_net.get_dr_pin_list().emplace_back(pin);
  }
  return dr_net;
}

void DetailedRouter::readDRModel(DRModel& dr_model)
{
  Die& die = RTDM.getDatabase().get_die();

  dr_model.get_net_detailed_result_map() = RTDM.getDatabase().get_net_detailed_result_map();
  dr_model.get_net_detailed_patch_map() = RTDM.getDatabase().get_net_detailed_patch_map();
  for (const Violation& violation : RTDM.getViolationList(die)) {
    dr_model.get_route_violation_list().push_back(violation);
  }
}

void DetailedRouter::routeDRModel(DRModel& dr_model)
{
  int32_t cost_unit = RTDM.getOnlyPitch();
  double prefer_wire_unit = 1;
  double non_prefer_wire_unit = 2.5 * prefer_wire_unit;
  double bend_unit = 2 * prefer_wire_unit * cost_unit;
  double via_unit = 2 * non_prefer_wire_unit * cost_unit;
  double fixed_rect_unit = 4 * non_prefer_wire_unit * cost_unit;
  double routed_rect_unit = 2 * non_prefer_wire_unit * cost_unit;
  double violation_unit = 4 * non_prefer_wire_unit * cost_unit;
  /**
   * prefer_wire_unit, non_prefer_wire_unit, bend_unit, via_unit, size, offset, schedule_interval, fixed_rect_unit, routed_rect_unit,
   * violation_unit, max_routed_times, max_candidate_patch_num, refine_net_num
   */
  std::vector<DRIterParam> dr_iter_param_list;
  // clang-format off
  dr_iter_param_list.emplace_back(prefer_wire_unit, non_prefer_wire_unit, bend_unit, via_unit, 12, 0, 3, fixed_rect_unit, routed_rect_unit, violation_unit, 3, 10, 0);
  dr_iter_param_list.emplace_back(prefer_wire_unit, non_prefer_wire_unit, bend_unit, via_unit, 12, 4, 3, fixed_rect_unit, routed_rect_unit, violation_unit, 3, 10, 0);
  dr_iter_param_list.emplace_back(prefer_wire_unit, non_prefer_wire_unit, bend_unit, via_unit, 12, 8, 3, fixed_rect_unit, routed_rect_unit, violation_unit, 3, 10, 0);
  dr_iter_param_list.emplace_back(prefer_wire_unit, non_prefer_wire_unit, bend_unit, via_unit, 12, 0, 3, 2 * fixed_rect_unit, 2 * routed_rect_unit, 2 * violation_unit, 9, 10, 0);
  dr_iter_param_list.emplace_back(prefer_wire_unit, non_prefer_wire_unit, bend_unit, via_unit, 12, 4, 3, 2 * fixed_rect_unit, 2 * routed_rect_unit, 2 * violation_unit, 9, 10, 0);
  dr_iter_param_list.emplace_back(prefer_wire_unit, non_prefer_wire_unit, bend_unit, via_unit, 12, 8, 3, 2 * fixed_rect_unit, 2 * routed_rect_unit, 2 * violation_unit, 9, 10, 0);
  dr_iter_param_list.emplace_back(prefer_wire_unit, non_prefer_wire_unit, bend_unit, via_unit, 12, 0, 3, 4 * fixed_rect_unit, 4 * routed_rect_unit, 4 * violation_unit, 18, 10, 0);
  dr_iter_param_list.emplace_back(prefer_wire_unit, non_prefer_wire_unit, bend_unit, via_unit, 12, 4, 3, 4 * fixed_rect_unit, 4 * routed_rect_unit, 4 * violation_unit, 18, 10, 0);
  dr_iter_param_list.emplace_back(prefer_wire_unit, non_prefer_wire_unit, bend_unit, via_unit, 12, 8, 3, 4 * fixed_rect_unit, 4 * routed_rect_unit, 4 * violation_unit, 18, 10, 0);
  // clang-format on
  initRoutingState(dr_model);
  for (int32_t i = 0, iter = 1; i < static_cast<int32_t>(dr_iter_param_list.size()); i++, iter++) {
    Monitor iter_monitor;
    RTLOG.info(Loc::current(), "***** Begin iteration ", iter, "/", dr_iter_param_list.size(), "(", RTUTIL.getPercentage(iter, dr_iter_param_list.size()),
               ") *****");
    // debugPlotDRModel(dr_model, "before");
    setDRIterParam(dr_model, iter, dr_iter_param_list[i]);
    initDRBoxMap(dr_model);
    resetRoutingState(dr_model);
    buildBoxSchedule(dr_model);
    splitNetResult(dr_model);
    // debugPlotDRModel(dr_model, "middle");
    routeDRBoxMap(dr_model);
    updateNetResult(dr_model);
    updateNetPatch(dr_model);
    updateViolation(dr_model);
    freeDRBoxMap(dr_model);
    patchFinalMinArea(dr_model);
    updateBestResult(dr_model);
    // debugPlotDRModel(dr_model, "after");
    updateSummary(dr_model);
    printSummary(dr_model);
    outputNetCSV(dr_model);
    outputViolationCSV(dr_model);
    RTLOG.info(Loc::current(), "***** End Iteration ", iter, "/", dr_iter_param_list.size(), "(", RTUTIL.getPercentage(iter, dr_iter_param_list.size()), ")",
               iter_monitor.getStatsInfo(), "*****");
    if (stopIteration(dr_model, dr_iter_param_list)) {
      break;
    }
  }
  selectBestResult(dr_model);
}

void DetailedRouter::initRoutingState(DRModel& dr_model)
{
  dr_model.set_initial_routing(true);
}

void DetailedRouter::setDRIterParam(DRModel& dr_model, int32_t iter, DRIterParam& dr_iter_param)
{
  dr_model.set_iter(iter);
  RTLOG.info(Loc::current(), "prefer_wire_unit: ", dr_iter_param.get_prefer_wire_unit());
  RTLOG.info(Loc::current(), "non_prefer_wire_unit: ", dr_iter_param.get_non_prefer_wire_unit());
  RTLOG.info(Loc::current(), "bend_unit: ", dr_iter_param.get_bend_unit());
  RTLOG.info(Loc::current(), "via_unit: ", dr_iter_param.get_via_unit());
  RTLOG.info(Loc::current(), "size: ", dr_iter_param.get_size());
  RTLOG.info(Loc::current(), "offset: ", dr_iter_param.get_offset());
  RTLOG.info(Loc::current(), "schedule_interval: ", dr_iter_param.get_schedule_interval());
  RTLOG.info(Loc::current(), "fixed_rect_unit: ", dr_iter_param.get_fixed_rect_unit());
  RTLOG.info(Loc::current(), "routed_rect_unit: ", dr_iter_param.get_routed_rect_unit());
  RTLOG.info(Loc::current(), "violation_unit: ", dr_iter_param.get_violation_unit());
  RTLOG.info(Loc::current(), "max_routed_times: ", dr_iter_param.get_max_routed_times());
  RTLOG.info(Loc::current(), "max_candidate_patch_num: ", dr_iter_param.get_max_candidate_patch_num());
  RTLOG.info(Loc::current(), "refine_net_num: ", dr_iter_param.get_refine_net_num());
  dr_model.set_dr_iter_param(dr_iter_param);
}

void DetailedRouter::initDRBoxMap(DRModel& dr_model)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  DRIterParam& dr_iter_param = dr_model.get_dr_iter_param();

  int32_t size = dr_iter_param.get_size();
  int32_t offset = dr_iter_param.get_offset();
  while (offset >= size) {
    offset -= size;
  }
  std::vector<int32_t> x_scale_list;
  {
    int32_t x_gcell_num = 0;
    for (ScaleGrid& x_grid : gcell_axis.get_x_grid_list()) {
      x_gcell_num += x_grid.get_step_num();
    }
    x_scale_list.push_back(0);
    for (int32_t x_scale = offset; x_scale <= x_gcell_num; x_scale += size) {
      x_scale_list.push_back(x_scale);
    }
    x_scale_list.push_back(x_gcell_num);
    std::ranges::sort(x_scale_list);
    x_scale_list.erase(std::ranges::unique(x_scale_list).begin(), x_scale_list.end());
  }
  std::vector<int32_t> y_scale_list;
  {
    int32_t y_gcell_num = 0;
    for (ScaleGrid& y_grid : gcell_axis.get_y_grid_list()) {
      y_gcell_num += y_grid.get_step_num();
    }
    y_scale_list.push_back(0);
    for (int32_t y_scale = offset; y_scale <= y_gcell_num; y_scale += size) {
      y_scale_list.push_back(y_scale);
    }
    y_scale_list.push_back(y_gcell_num);
    std::ranges::sort(y_scale_list);
    y_scale_list.erase(std::ranges::unique(y_scale_list).begin(), y_scale_list.end());
  }
  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();
  {
    int32_t x_box_num = static_cast<int32_t>(x_scale_list.size()) - 1;
    int32_t y_box_num = static_cast<int32_t>(y_scale_list.size()) - 1;
    dr_box_map.init(x_box_num, y_box_num);
  }
  for (int32_t x = 0; x < dr_box_map.get_x_size(); x++) {
    for (int32_t y = 0; y < dr_box_map.get_y_size(); y++) {
      int32_t grid_ll_x = x_scale_list[x];
      int32_t grid_ll_y = y_scale_list[y];
      int32_t grid_ur_x = x_scale_list[x + 1] - 1;
      int32_t grid_ur_y = y_scale_list[y + 1] - 1;

      PlanarRect ll_gcell_rect = RTUTIL.getRealRectByGCell(PlanarCoord(grid_ll_x, grid_ll_y), gcell_axis);
      PlanarRect ur_gcell_rect = RTUTIL.getRealRectByGCell(PlanarCoord(grid_ur_x, grid_ur_y), gcell_axis);
      PlanarRect box_real_rect(ll_gcell_rect.get_ll(), ur_gcell_rect.get_ur());

      DRBox& dr_box = dr_box_map[x][y];

      EXTPlanarRect dr_box_rect;
      dr_box_rect.set_real_rect(box_real_rect);
      dr_box_rect.set_grid_rect(RTUTIL.getOpenGCellGridRect(box_real_rect, gcell_axis));
      dr_box.set_box_rect(dr_box_rect);
      DRBoxId dr_box_id;
      dr_box_id.set_x(x);
      dr_box_id.set_y(y);
      dr_box.set_dr_box_id(dr_box_id);
      dr_box.set_dr_iter_param(&dr_iter_param);
      dr_box.set_initial_routing(dr_model.get_initial_routing());
      dr_box.set_dirty(false);
    }
  }

  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::vector<int32_t> gcell_x_box_idx_list(gcell_map.get_x_size(), -1);
  std::vector<int32_t> gcell_y_box_idx_list(gcell_map.get_y_size(), -1);
  for (int32_t x = 0; x < dr_box_map.get_x_size(); x++) {
    for (int32_t grid_x = dr_box_map[x][0].get_box_rect().get_grid_ll_x(); grid_x <= dr_box_map[x][0].get_box_rect().get_grid_ur_x(); grid_x++) {
      gcell_x_box_idx_list[grid_x] = x;
    }
  }
  for (int32_t y = 0; y < dr_box_map.get_y_size(); y++) {
    for (int32_t grid_y = dr_box_map[0][y].get_box_rect().get_grid_ll_y(); grid_y <= dr_box_map[0][y].get_box_rect().get_grid_ur_y(); grid_y++) {
      gcell_y_box_idx_list[grid_y] = y;
    }
  }
  dr_model.set_gcell_x_box_idx_list(gcell_x_box_idx_list);
  dr_model.set_gcell_y_box_idx_list(gcell_y_box_idx_list);
}

void DetailedRouter::resetRoutingState(DRModel& dr_model)
{
  dr_model.set_initial_routing(false);
}

void DetailedRouter::buildBoxSchedule(DRModel& dr_model)
{
  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();
  int32_t schedule_interval = dr_model.get_dr_iter_param().get_schedule_interval();

  std::vector<std::vector<DRBoxId>> dr_box_id_list_list;
  for (int32_t start_x = 0; start_x < schedule_interval; start_x++) {
    for (int32_t start_y = 0; start_y < schedule_interval; start_y++) {
      std::vector<DRBoxId> dr_box_id_list;
      for (int32_t x = start_x; x < dr_box_map.get_x_size(); x += schedule_interval) {
        for (int32_t y = start_y; y < dr_box_map.get_y_size(); y += schedule_interval) {
          dr_box_id_list.emplace_back(x, y);
        }
      }
      if (!dr_box_id_list.empty()) {
        dr_box_id_list_list.push_back(dr_box_id_list);
      }
    }
  }
  dr_model.set_dr_box_id_list_list(dr_box_id_list_list);
}

void DetailedRouter::splitNetResult(DRModel& dr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();

  // split detailed segment by box, and distribute ownership to box according to midpoint
  std::map<int32_t, std::vector<Segment<LayerCoord>>>& net_detailed_result_map = dr_model.get_net_detailed_result_map();
  std::vector<std::vector<Segment<LayerCoord>>*> segment_list_list;
  segment_list_list.reserve(net_detailed_result_map.size());
  for (auto& [net_idx, segment_list] : net_detailed_result_map) {
    segment_list_list.push_back(&segment_list);
  }
#pragma omp parallel for schedule(dynamic, 1)
  for (size_t i = 0; i < segment_list_list.size(); i++) {
    std::vector<Segment<LayerCoord>>& segment_list = *segment_list_list[i];
    std::vector<Segment<LayerCoord>> new_segment_list;
    new_segment_list.reserve(segment_list.size());
    for (Segment<LayerCoord>& segment : segment_list) {
      LayerCoord& first_coord = segment.get_first();
      LayerCoord& second_coord = segment.get_second();
      bool is_horizontal = RTUTIL.isHorizontal(first_coord, second_coord);
      bool is_vertical = RTUTIL.isVertical(first_coord, second_coord);
      if (first_coord.get_layer_idx() != second_coord.get_layer_idx() || (!is_horizontal && !is_vertical)) {
        new_segment_list.push_back(segment);
        continue;
      }

      int32_t first_scale = is_horizontal ? first_coord.get_x() : first_coord.get_y();
      int32_t second_scale = is_horizontal ? second_coord.get_x() : second_coord.get_y();
      RTUTIL.swapByASC(first_scale, second_scale);
      std::set<int32_t> pre_scale_set;
      std::set<int32_t> split_scale_set;
      std::set<int32_t> post_scale_set;
      std::vector<ScaleGrid>& grid_list = is_horizontal ? gcell_axis.get_x_grid_list() : gcell_axis.get_y_grid_list();
      RTUTIL.getTrackScaleSet(grid_list, first_scale, second_scale, pre_scale_set, split_scale_set, post_scale_set);
      split_scale_set.erase(first_scale);
      split_scale_set.insert(second_scale);

      int32_t previous_scale = first_scale;
      for (int32_t current_scale : split_scale_set) {
        LayerCoord split_first = first_coord;
        LayerCoord split_second = first_coord;
        if (is_horizontal) {
          split_first.set_x(previous_scale);
          split_second.set_x(current_scale);
        } else {
          split_first.set_y(previous_scale);
          split_second.set_y(current_scale);
        }
        new_segment_list.emplace_back(split_first, split_second);
        previous_scale = current_scale;
      }
    }
    segment_list = std::move(new_segment_list);
  }

  std::vector<int32_t>& x_box_idx_list = dr_model.get_gcell_x_box_idx_list();
  std::vector<int32_t>& y_box_idx_list = dr_model.get_gcell_y_box_idx_list();
  for (auto& [net_idx, segment_list] : net_detailed_result_map) {
    for (Segment<LayerCoord>& segment : segment_list) {
      LayerCoord& first_coord = segment.get_first();
      LayerCoord& second_coord = segment.get_second();
      PlanarCoord midpoint((first_coord.get_x() + second_coord.get_x()) / 2, (first_coord.get_y() + second_coord.get_y()) / 2);
      int32_t grid_x = RTUTIL.getGCellGridLB(midpoint.get_x(), gcell_axis.get_x_grid_list());
      int32_t grid_y = RTUTIL.getGCellGridLB(midpoint.get_y(), gcell_axis.get_y_grid_list());
      DRBox& owner_box = dr_box_map[x_box_idx_list[grid_x]][y_box_idx_list[grid_y]];
      owner_box.get_net_task_detailed_result_map()[net_idx].push_back(segment);
    }
  }
  net_detailed_result_map.clear();

  for (auto& [net_idx, patch_list] : dr_model.get_net_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      PlanarCoord midpoint = patch.get_real_rect().getMidPoint();
      int32_t grid_x = RTUTIL.getGCellGridLB(midpoint.get_x(), gcell_axis.get_x_grid_list());
      int32_t grid_y = RTUTIL.getGCellGridLB(midpoint.get_y(), gcell_axis.get_y_grid_list());
      DRBox& owner_box = dr_box_map[x_box_idx_list[grid_x]][y_box_idx_list[grid_y]];
      owner_box.get_net_task_detailed_patch_map()[net_idx].push_back(patch);
    }
  }
  dr_model.get_net_detailed_patch_map().clear();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

std::set<DRBoxId, CmpDRBoxId> DetailedRouter::getDRBoxIdSet(DRModel& dr_model, PlanarRect real_rect)
{
  Die& die = RTDM.getDatabase().get_die();
  if (!RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
    return {};
  }
  real_rect = RTUTIL.getRegularRect(real_rect, die.get_real_rect());
  PlanarRect grid_rect = RTUTIL.getClosedGCellGridRect(real_rect, RTDM.getDatabase().get_gcell_axis());
  std::vector<int32_t>& x_box_idx_list = dr_model.get_gcell_x_box_idx_list();
  std::vector<int32_t>& y_box_idx_list = dr_model.get_gcell_y_box_idx_list();
  int32_t ll_x = x_box_idx_list[grid_rect.get_ll_x()];
  int32_t ll_y = y_box_idx_list[grid_rect.get_ll_y()];
  int32_t ur_x = x_box_idx_list[grid_rect.get_ur_x()];
  int32_t ur_y = y_box_idx_list[grid_rect.get_ur_y()];

  std::set<DRBoxId, CmpDRBoxId> dr_box_id_set;
  for (int32_t x = ll_x; x <= ur_x; x++) {
    for (int32_t y = ll_y; y <= ur_y; y++) {
      dr_box_id_set.emplace(x, y);
    }
  }
  return dr_box_id_set;
}

namespace {

bool overlapCheckRegion(int32_t layer_idx, const PlanarRect& real_rect, const std::vector<LayerRect>& check_region_list)
{
  if (check_region_list.empty()) {
    return true;
  }
  return std::ranges::any_of(check_region_list, [&](const LayerRect& check_region) {
    return layer_idx == check_region.get_layer_idx() && RTUTIL.isClosedOverlap(real_rect, check_region);
  });
}

bool isPreviousBoundaryGCell(int32_t gcell_idx, int32_t previous_offset, int32_t size)
{
  int32_t remainder = (gcell_idx - previous_offset) % size;
  if (remainder < 0) {
    remainder += size;
  }
  return remainder == 0 || remainder == size - 1;
}

bool coordOnPreviousBoxBoundary(const PlanarCoord& coord, ScaleAxis& gcell_axis, int32_t previous_offset, int32_t size)
{
  int32_t grid_x = RTUTIL.getGCellGridLB(coord.get_x(), gcell_axis.get_x_grid_list());
  int32_t grid_y = RTUTIL.getGCellGridLB(coord.get_y(), gcell_axis.get_y_grid_list());
  return isPreviousBoundaryGCell(grid_x, previous_offset, size) || isPreviousBoundaryGCell(grid_y, previous_offset, size);
}

bool isViaMasterIdxValid(const ViaMasterIdx& via_master_idx, int32_t below_layer_idx)
{
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();
  int32_t via_idx = via_master_idx.get_via_idx();
  return via_master_idx.isValid() && via_master_idx.get_below_layer_idx() == below_layer_idx && below_layer_idx >= 0
         && below_layer_idx < static_cast<int32_t>(layer_via_master_list.size()) && via_idx >= 0
         && via_idx < static_cast<int32_t>(layer_via_master_list[below_layer_idx].size());
}

bool hasSameUndirectedEndpoints(const Segment<LayerCoord>& segment, const LayerCoord& first, const LayerCoord& second)
{
  return (segment.get_first() == first && segment.get_second() == second) || (segment.get_first() == second && segment.get_second() == first);
}

ViaMasterIdx getRequiredAPViaMasterIdx(int32_t net_idx, AccessPoint& access_point)
{
  const std::vector<ViaMasterIdx>& candidate_via_list = access_point.get_candidate_via_list();
  if (candidate_via_list.empty()) {
    return ViaMasterIdx();
  }
  if (candidate_via_list.size() != 1) {
    RTLOG.error(Loc::current(), "The access point via master is not unique! net_idx: ", net_idx, ", pin_idx: ", access_point.get_pin_idx());
  }
  const ViaMasterIdx& via_master_idx = candidate_via_list.front();
  int32_t below_layer_idx = via_master_idx.get_below_layer_idx();
  if (!isViaMasterIdxValid(via_master_idx, below_layer_idx)) {
    RTLOG.error(Loc::current(), "The access point has an invalid via master index! net_idx: ", net_idx, ", pin_idx: ", access_point.get_pin_idx(),
                ", below_layer_idx: ", below_layer_idx, ", via_idx: ", via_master_idx.get_via_idx());
  }
  int32_t access_layer_idx = access_point.get_layer_idx();
  if (access_layer_idx != below_layer_idx && access_layer_idx != below_layer_idx + 1) {
    RTLOG.error(Loc::current(), "The access point via master is not adjacent to the access layer! net_idx: ", net_idx,
                ", pin_idx: ", access_point.get_pin_idx(), ", access_layer_idx: ", access_layer_idx, ", below_layer_idx: ", below_layer_idx);
  }
  return via_master_idx;
}

void resolveResultViaMasterIdx(int32_t net_idx, std::vector<DRPin>& dr_pin_list, std::vector<Segment<LayerCoord>>& indexed_via_segment_list,
                               Segment<LayerCoord>& via_segment, int32_t& legacy_default_via_num)
{
  if (via_segment.get_first().get_planar_coord() != via_segment.get_second().get_planar_coord()
      || std::abs(via_segment.get_first().get_layer_idx() - via_segment.get_second().get_layer_idx()) != 1) {
    RTLOG.error(Loc::current(), "The result via is not between adjacent routing layers! net_idx: ", net_idx);
  }
  int32_t below_layer_idx = std::min(via_segment.get_first().get_layer_idx(), via_segment.get_second().get_layer_idx());
  for (Segment<LayerCoord>& indexed_via_segment : indexed_via_segment_list) {
    if (!hasSameUndirectedEndpoints(via_segment, indexed_via_segment.get_first(), indexed_via_segment.get_second())) {
      continue;
    }
    if (via_segment.hasValidViaMaster() && via_segment.get_via_master_idx() != indexed_via_segment.get_via_master_idx()) {
      RTLOG.error(Loc::current(), "The detailed result has conflicting via master indexes! net_idx: ", net_idx);
    }
    via_segment.set_via_master_idx(indexed_via_segment.get_via_master_idx());
  }
  ViaMasterIdx required_via_master_idx;
  for (DRPin& dr_pin : dr_pin_list) {
    AccessPoint& access_point = dr_pin.get_access_point();
    LayerCoord access_point_coord = access_point.getRealLayerCoord();
    if (access_point_coord != via_segment.get_first() && access_point_coord != via_segment.get_second()) {
      continue;
    }
    ViaMasterIdx access_via_master_idx = getRequiredAPViaMasterIdx(net_idx, access_point);
    if (!access_via_master_idx.isValid() || access_via_master_idx.get_below_layer_idx() != below_layer_idx) {
      continue;
    }
    if (required_via_master_idx.isValid() && required_via_master_idx != access_via_master_idx) {
      RTLOG.error(Loc::current(), "The detailed result via has conflicting access point constraints! net_idx: ", net_idx,
                  ", below_layer_idx: ", below_layer_idx);
    }
    required_via_master_idx = access_via_master_idx;
  }
  if (via_segment.hasValidViaMaster()) {
    if (required_via_master_idx.isValid() && via_segment.get_via_master_idx() != required_via_master_idx) {
      RTLOG.error(Loc::current(), "The detailed result violates the access point via master constraint! net_idx: ", net_idx,
                  ", below_layer_idx: ", below_layer_idx);
    }
    return;
  }
  if (required_via_master_idx.isValid()) {
    via_segment.set_via_master_idx(required_via_master_idx);
    return;
  }

  std::vector<std::vector<ViaMaster>>& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();
  if (below_layer_idx < 0 || below_layer_idx >= static_cast<int32_t>(layer_via_master_list.size()) || layer_via_master_list[below_layer_idx].empty()) {
    RTLOG.error(Loc::current(), "The legacy via can not be materialized! net_idx: ", net_idx, ", below_layer_idx: ", below_layer_idx);
  }
  via_segment.set_via_master_idx(layer_via_master_list[below_layer_idx].front().get_via_master_idx());
  legacy_default_via_num++;
}

void checkAPViaMasterConstraint(int32_t net_idx, std::vector<DRPin>& dr_pin_list, std::vector<Segment<LayerCoord>>& detailed_result_list)
{
  for (DRPin& dr_pin : dr_pin_list) {
    AccessPoint& access_point = dr_pin.get_access_point();
    ViaMasterIdx required_via_master_idx = getRequiredAPViaMasterIdx(net_idx, access_point);
    if (!required_via_master_idx.isValid()) {
      continue;
    }
    LayerCoord access_coord = access_point.getRealLayerCoord();
    for (Segment<LayerCoord>& segment : detailed_result_list) {
      if (segment.get_first().get_layer_idx() == segment.get_second().get_layer_idx()
          || (segment.get_first() != access_coord && segment.get_second() != access_coord)) {
        continue;
      }
      if (!segment.hasValidViaMaster() || segment.get_via_master_idx() != required_via_master_idx) {
        RTLOG.error(Loc::current(), "The access point via does not use the required via master! net_idx: ", net_idx, ", pin_idx: ", access_point.get_pin_idx(),
                    ", below_layer_idx: ", required_via_master_idx.get_below_layer_idx());
      }
    }
  }
}

}  // namespace

void DetailedRouter::routeDRBoxMap(DRModel& dr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();

  size_t total_box_num = 0;
  for (std::vector<DRBoxId>& dr_box_id_list : dr_model.get_dr_box_id_list_list()) {
    total_box_num += dr_box_id_list.size();
  }

  size_t routed_box_num = 0;
  for (std::vector<DRBoxId>& dr_box_id_list : dr_model.get_dr_box_id_list_list()) {
    Monitor stage_monitor;
    buildNetEnvironment(dr_model, dr_box_id_list);
    std::vector<std::set<int32_t>> stage_violation_net_set_list(dr_box_id_list.size());
    std::vector<std::vector<Violation>> stage_violation_list_list(dr_box_id_list.size());
    buildStageViolationNetSetList(dr_model, dr_box_id_list, stage_violation_net_set_list);

#pragma omp parallel for schedule(dynamic, 1)
    for (size_t i = 0; i < dr_box_id_list.size(); i++) {
      DRBoxId& dr_box_id = dr_box_id_list[i];
      DRBox& dr_box = dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()];
      initDRBox(dr_model, dr_box, stage_violation_net_set_list[i]);
      if (needRefine(dr_box)) {
        buildRefineTaskList(dr_model, dr_box);
      }
    }
    buildRouteViolation(dr_model, dr_box_id_list);

#pragma omp parallel for schedule(dynamic, 1)
    for (size_t i = 0; i < dr_box_id_list.size(); i++) {
      DRBoxId& dr_box_id = dr_box_id_list[i];
      DRBox& dr_box = dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()];
      bool need_route = needRouting(dr_box);
      bool need_refine = !dr_box.get_refine_net_list().empty();
      if (need_route || need_refine) {
        dr_box.set_dirty(true);
        buildFixedRect(dr_box);
        buildBoxTrackAxis(dr_box);
        buildLayerNodeMap(dr_box);
        buildLayerShadowMap(dr_box);
        buildDRNodeNeighbor(dr_box);
        buildOrientNetMap(dr_box);
        buildNetShadowMap(dr_box);
        exemptPinShape(dr_model, dr_box);
        // debugCheckDRBox(dr_box);
        // debugPlotDRBox(dr_box, "before");
        routeDRBox(dr_box);
        // debugPlotDRBox(dr_box, "after");
      }
      selectBestResult(dr_box);
      // stage violation before free box
      stage_violation_list_list[i] = std::move(dr_box.get_route_violation_list());
      freeDRBox(dr_box);
    }
    updateRouteViolation(dr_model, stage_violation_list_list);
    routed_box_num += dr_box_id_list.size();
    RTLOG.info(Loc::current(), "Routed ", routed_box_num, "/", total_box_num, "(", RTUTIL.getPercentage(routed_box_num, total_box_num), ") boxes with ",
               getRouteViolationNum(dr_model), " violations", stage_monitor.getStatsInfo());
  }

  updateDRModel(dr_model);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DetailedRouter::buildStageViolationNetSetList(DRModel& dr_model, const std::vector<DRBoxId>& dr_box_id_list,
                                                   std::vector<std::set<int32_t>>& stage_violation_net_set_list)
{
  if (dr_model.get_iter() == 1) {
    return;
  }
  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  std::map<DRBoxId, size_t, CmpDRBoxId> box_idx_map;
  for (size_t i = 0; i < dr_box_id_list.size(); i++) {
    box_idx_map[dr_box_id_list[i]] = i;
  }
  for (Violation& violation : dr_model.get_route_violation_list()) {
    PlanarRect repair_rect = RTUTIL.getEnlargedRect(violation.get_violation_shape().get_real_rect(), detection_distance);
    for (const DRBoxId& violation_box_id : getDRBoxIdSet(dr_model, repair_rect)) {
      auto box_iter = box_idx_map.find(violation_box_id);
      if (box_iter == box_idx_map.end()) {
        continue;
      }
      stage_violation_net_set_list[box_iter->second].insert(violation.get_violation_net_set().begin(), violation.get_violation_net_set().end());
    }
  }
}

void DetailedRouter::initDRBox(DRModel& dr_model, DRBox& dr_box, const std::set<int32_t>& violation_net_set)
{
  buildAccessPoint(dr_box);
  initDRTaskList(dr_model, dr_box, violation_net_set);
}

void DetailedRouter::updateRouteViolation(DRModel& dr_model, std::vector<std::vector<Violation>>& stage_violation_list_list)
{
  std::set<Violation, CmpViolation> route_violation_set(dr_model.get_route_violation_list().begin(), dr_model.get_route_violation_list().end());
  for (std::vector<Violation>& violation_list : stage_violation_list_list) {
    route_violation_set.insert(violation_list.begin(), violation_list.end());
  }
  dr_model.get_route_violation_list().assign(route_violation_set.begin(), route_violation_set.end());
}

void DetailedRouter::freeDRBoxMap(DRModel& dr_model)
{
  dr_model.get_dr_box_map().free();
  std::vector<std::vector<DRBoxId>>().swap(dr_model.get_dr_box_id_list_list());
}

void DetailedRouter::buildFixedRect(DRBox& dr_box)
{
  dr_box.set_type_layer_net_fixed_rect_map(RTDM.getTypeLayerNetFixedRectMap(dr_box.get_box_rect()));
}

void DetailedRouter::buildAccessPoint(DRBox& dr_box)
{
  dr_box.set_net_access_point_map(RTDM.getNetAccessPointMap(dr_box.get_box_rect()));
}

void DetailedRouter::buildNetEnvironment(DRModel& dr_model, const std::vector<DRBoxId>& dr_box_id_list)
{
  if (dr_box_id_list.empty()) {
    return;
  }
  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();
  GridMap<bool> active_box_map(dr_box_map.get_x_size(), dr_box_map.get_y_size(), false);
  GridMap<omp_lock_t> environment_lock_map(dr_box_map.get_x_size(), dr_box_map.get_y_size());
  for (const DRBoxId& dr_box_id : dr_box_id_list) {
    active_box_map[dr_box_id.get_x()][dr_box_id.get_y()] = true;
    omp_init_lock(&environment_lock_map[dr_box_id.get_x()][dr_box_id.get_y()]);
    DRBox& dr_box = dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()];
    dr_box.get_net_detailed_result_map().clear();
    dr_box.get_net_detailed_patch_map().clear();
  }

#pragma omp parallel for collapse(2) schedule(dynamic, 1)
  for (int32_t x = 0; x < dr_box_map.get_x_size(); x++) {
    for (int32_t y = 0; y < dr_box_map.get_y_size(); y++) {
      // Task results of active boxes are mutable and active boxes are independent in one schedule.
      if (active_box_map[x][y]) {
        continue;
      }
      DRBox& owner_box = dr_box_map[x][y];
      for (auto& [net_idx, segment_list] : owner_box.get_net_task_detailed_result_map()) {
        for (Segment<LayerCoord>& segment : segment_list) {
          addNetResultToEnvironment(dr_model, active_box_map, environment_lock_map, net_idx, segment);
        }
      }
      for (auto& [net_idx, patch_list] : owner_box.get_net_task_detailed_patch_map()) {
        for (EXTLayerRect& patch : patch_list) {
          addNetPatchToEnvironment(dr_model, active_box_map, environment_lock_map, net_idx, patch);
        }
      }
    }
  }
  for (const DRBoxId& dr_box_id : dr_box_id_list) {
    omp_destroy_lock(&environment_lock_map[dr_box_id.get_x()][dr_box_id.get_y()]);
  }
}

void DetailedRouter::buildDirtyNetEnvironment(DRModel& dr_model, const std::vector<DRBoxId>& dr_box_id_list)
{
  if (dr_box_id_list.empty()) {
    return;
  }
  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();
  GridMap<bool> active_box_map(dr_box_map.get_x_size(), dr_box_map.get_y_size(), false);
  GridMap<omp_lock_t> environment_lock_map(dr_box_map.get_x_size(), dr_box_map.get_y_size());
  for (const DRBoxId& dr_box_id : dr_box_id_list) {
    active_box_map[dr_box_id.get_x()][dr_box_id.get_y()] = true;
    omp_init_lock(&environment_lock_map[dr_box_id.get_x()][dr_box_id.get_y()]);
    DRBox& dr_box = dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()];
    dr_box.get_net_detailed_result_map().clear();
    dr_box.get_net_detailed_patch_map().clear();
  }

  std::vector<std::pair<int32_t, std::vector<Segment<LayerCoord>>*>> net_result_list;
  for (auto& [net_idx, segment_list] : dr_model.get_net_detailed_result_map()) {
    net_result_list.emplace_back(net_idx, &segment_list);
  }
#pragma omp parallel for schedule(dynamic, 1)
  for (size_t i = 0; i < net_result_list.size(); i++) {
    for (Segment<LayerCoord>& segment : *net_result_list[i].second) {
      addNetResultToEnvironment(dr_model, active_box_map, environment_lock_map, net_result_list[i].first, segment);
    }
  }
  std::vector<std::pair<int32_t, std::vector<EXTLayerRect>*>> net_patch_list;
  for (auto& [net_idx, patch_list] : dr_model.get_net_detailed_patch_map()) {
    net_patch_list.emplace_back(net_idx, &patch_list);
  }
#pragma omp parallel for schedule(dynamic, 1)
  for (size_t i = 0; i < net_patch_list.size(); i++) {
    for (EXTLayerRect& patch : *net_patch_list[i].second) {
      addNetPatchToEnvironment(dr_model, active_box_map, environment_lock_map, net_patch_list[i].first, patch);
    }
  }
  for (const DRBoxId& dr_box_id : dr_box_id_list) {
    omp_destroy_lock(&environment_lock_map[dr_box_id.get_x()][dr_box_id.get_y()]);
  }
}

void DetailedRouter::addNetResultToEnvironment(DRModel& dr_model, GridMap<bool>& active_box_map, GridMap<omp_lock_t>& environment_lock_map, int32_t net_idx,
                                               Segment<LayerCoord>& segment)
{
  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  std::set<DRBoxId, CmpDRBoxId> dr_box_id_set;
  for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
    PlanarRect real_rect = RTUTIL.getEnlargedRect(net_shape, detection_distance);
    std::set<DRBoxId, CmpDRBoxId> shape_box_id_set = getDRBoxIdSet(dr_model, real_rect);
    dr_box_id_set.insert(shape_box_id_set.begin(), shape_box_id_set.end());
  }
  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();
  for (const DRBoxId& dr_box_id : dr_box_id_set) {
    if (!active_box_map[dr_box_id.get_x()][dr_box_id.get_y()]) {
      continue;
    }
    omp_set_lock(&environment_lock_map[dr_box_id.get_x()][dr_box_id.get_y()]);
    dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()].get_net_detailed_result_map()[net_idx].push_back(&segment);
    omp_unset_lock(&environment_lock_map[dr_box_id.get_x()][dr_box_id.get_y()]);
  }
}

void DetailedRouter::addNetPatchToEnvironment(DRModel& dr_model, GridMap<bool>& active_box_map, GridMap<omp_lock_t>& environment_lock_map, int32_t net_idx,
                                              EXTLayerRect& patch)
{
  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  PlanarRect real_rect = RTUTIL.getEnlargedRect(patch.get_real_rect(), detection_distance);
  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();
  for (const DRBoxId& dr_box_id : getDRBoxIdSet(dr_model, real_rect)) {
    if (!active_box_map[dr_box_id.get_x()][dr_box_id.get_y()]) {
      continue;
    }
    omp_set_lock(&environment_lock_map[dr_box_id.get_x()][dr_box_id.get_y()]);
    dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()].get_net_detailed_patch_map()[net_idx].push_back(&patch);
    omp_unset_lock(&environment_lock_map[dr_box_id.get_x()][dr_box_id.get_y()]);
  }
}

void DetailedRouter::initDRTaskList(DRModel& dr_model, DRBox& dr_box, const std::set<int32_t>& violation_net_set)
{
  std::set<int32_t> net_idx_set;
  if (dr_box.get_initial_routing()) {
    for (auto& [net_idx, segment_list] : dr_box.get_net_task_detailed_result_map()) {
      net_idx_set.insert(net_idx);
    }
    for (auto& [net_idx, patch_list] : dr_box.get_net_task_detailed_patch_map()) {
      net_idx_set.insert(net_idx);
    }
    for (auto& [net_idx, access_point_set] : dr_box.get_net_access_point_map()) {
      net_idx_set.insert(net_idx);
    }
  } else {
    for (int32_t net_idx : violation_net_set) {
      if (dr_box.get_net_task_detailed_result_map().contains(net_idx) || dr_box.get_net_task_detailed_patch_map().contains(net_idx)) {
        net_idx_set.insert(net_idx);
      }
    }
  }
  for (int32_t net_idx : net_idx_set) {
    buildNetTaskList(dr_model, dr_box, net_idx);
  }
  std::ranges::sort(dr_box.get_dr_task_list(), CmpDRTask());
}

void DetailedRouter::buildRefineTaskList(DRModel& dr_model, DRBox& dr_box)
{
  std::vector<int32_t>& refine_net_list = dr_box.get_refine_net_list();
  refine_net_list.clear();
  int32_t max_refine_net_num = dr_box.get_dr_iter_param()->get_refine_net_num();
  if (max_refine_net_num <= 0) {
    return;
  }

  int32_t size = dr_box.get_dr_iter_param()->get_size();
  constexpr int32_t offset_phase_num = 3;
  if (size <= 0 || size % offset_phase_num != 0) {
    RTLOG.error(Loc::current(), "The DR box size is incompatible with refine offset phases!");
  }
  int32_t offset = dr_box.get_dr_iter_param()->get_offset();
  if (offset < 0 || offset >= size) {
    RTLOG.error(Loc::current(), "The DR box offset is invalid for refine!");
  }
  int32_t previous_offset = (offset - size / offset_phase_num + size) % size;
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();

  struct RefineNetCandidate
  {
    int32_t net_idx = -1;
    int32_t shape_num = 0;
    int64_t wire_length = 0;
    bool crosses_previous_boundary = false;
  };

  std::map<int32_t, RefineNetCandidate> candidate_map;
  for (auto& [net_idx, segment_list] : dr_box.get_net_task_detailed_result_map()) {
    if (segment_list.empty()) {
      continue;
    }
    RefineNetCandidate& candidate = candidate_map[net_idx];
    candidate.net_idx = net_idx;
    for (Segment<LayerCoord>& segment : segment_list) {
      candidate.shape_num++;
      if (segment.get_first().get_layer_idx() == segment.get_second().get_layer_idx()) {
        candidate.wire_length += RTUTIL.getManhattanDistance(segment.get_first().get_planar_coord(), segment.get_second().get_planar_coord());
      }
      if (!candidate.crosses_previous_boundary
          && (coordOnPreviousBoxBoundary(segment.get_first(), gcell_axis, previous_offset, size)
              || coordOnPreviousBoxBoundary(segment.get_second(), gcell_axis, previous_offset, size))) {
        candidate.crosses_previous_boundary = true;
      }
    }
  }
  for (auto& [net_idx, patch_list] : dr_box.get_net_task_detailed_patch_map()) {
    if (patch_list.empty()) {
      continue;
    }
    RefineNetCandidate& candidate = candidate_map[net_idx];
    candidate.net_idx = net_idx;
    for (EXTLayerRect& patch : patch_list) {
      candidate.shape_num++;
      PlanarRect& patch_rect = patch.get_real_rect();
      if (!candidate.crosses_previous_boundary
          && (coordOnPreviousBoxBoundary(patch_rect.get_ll(), gcell_axis, previous_offset, size)
              || coordOnPreviousBoxBoundary(patch_rect.get_ur(), gcell_axis, previous_offset, size))) {
        candidate.crosses_previous_boundary = true;
      }
    }
  }

  std::vector<RefineNetCandidate> candidate_list;
  for (auto& [net_idx, candidate] : candidate_map) {
    if (candidate.crosses_previous_boundary && !hasUncoveredBoxAccessPoint(dr_box, net_idx)) {
      candidate_list.push_back(candidate);
    }
  }
  std::sort(candidate_list.begin(), candidate_list.end(), [](const RefineNetCandidate& first, const RefineNetCandidate& second) {
    if (first.wire_length != second.wire_length) {
      return first.wire_length > second.wire_length;
    }
    if (first.shape_num != second.shape_num) {
      return first.shape_num > second.shape_num;
    }
    return first.net_idx < second.net_idx;
  });

  bool task_list_changed = false;
  for (RefineNetCandidate& candidate : candidate_list) {
    if (static_cast<int32_t>(refine_net_list.size()) >= max_refine_net_num) {
      break;
    }
    bool has_task = false;
    for (DRTask* dr_task : dr_box.get_dr_task_list()) {
      if (dr_task->get_net_idx() == candidate.net_idx) {
        has_task = true;
        break;
      }
    }
    if (!has_task) {
      size_t old_task_num = dr_box.get_dr_task_list().size();
      buildNetTaskList(dr_model, dr_box, candidate.net_idx);
      has_task = old_task_num != dr_box.get_dr_task_list().size();
      task_list_changed = task_list_changed || has_task;
    }
    if (has_task) {
      refine_net_list.push_back(candidate.net_idx);
    }
  }
  if (task_list_changed) {
    std::ranges::sort(dr_box.get_dr_task_list(), CmpDRTask());
  }
}

namespace {

struct DRTaskComponent
{
  std::vector<DRGroup> group_list;
  std::vector<Segment<LayerCoord>> result_list;
  std::vector<int32_t> stable_key;
};

}  // namespace

void DetailedRouter::buildNetTaskList(DRModel& dr_model, DRBox& dr_box, int32_t net_idx)
{
  std::vector<Segment<LayerCoord>>& result_list = dr_box.get_net_task_detailed_result_map()[net_idx];
  std::vector<EXTLayerRect>& patch_list = dr_box.get_net_task_detailed_patch_map()[net_idx];

  DRConnectivity connectivity;
  // 每个segment/patch作为一个实体，获取连通性。
  std::vector<int32_t> segment_entity_idx_list;
  std::vector<int32_t> patch_entity_idx_list;
  for (Segment<LayerCoord>& segment : result_list) {
    std::vector<LayerRect> shape_list;
    for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
      if (net_shape.get_is_routing()) {
        shape_list.push_back(net_shape);
      }
    }
    segment_entity_idx_list.push_back(connectivity.addEntity(std::move(shape_list)));
  }
  patch_entity_idx_list.reserve(patch_list.size());
  for (EXTLayerRect& patch : patch_list) {
    patch_entity_idx_list.push_back(connectivity.addEntity({LayerRect(patch.get_real_rect(), patch.get_layer_idx())}));
  }

  std::map<PlanarCoord, std::vector<int32_t>, CmpPlanarCoordByXASC> gcell_entity_idx_map;
  auto access_iter = dr_box.get_net_access_point_map().find(net_idx);
  if (dr_box.get_initial_routing()) {
    // 初始布线结果按 gcell 切分，同一 gcell 内的实体需要恢复连通关系。
    ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
    for (size_t i = 0; i < result_list.size(); i++) {
      Segment<LayerCoord>& segment = result_list[i];
      PlanarCoord midpoint((segment.get_first().get_x() + segment.get_second().get_x()) / 2, (segment.get_first().get_y() + segment.get_second().get_y()) / 2);
      PlanarCoord gcell_coord(RTUTIL.getGCellGridLB(midpoint.get_x(), gcell_axis.get_x_grid_list()),
                              RTUTIL.getGCellGridLB(midpoint.get_y(), gcell_axis.get_y_grid_list()));
      gcell_entity_idx_map[gcell_coord].push_back(segment_entity_idx_list[i]);
    }
    for (size_t i = 0; i < patch_list.size(); i++) {
      PlanarCoord midpoint = patch_list[i].get_real_rect().getMidPoint();
      PlanarCoord gcell_coord(RTUTIL.getGCellGridLB(midpoint.get_x(), gcell_axis.get_x_grid_list()),
                              RTUTIL.getGCellGridLB(midpoint.get_y(), gcell_axis.get_y_grid_list()));
      gcell_entity_idx_map[gcell_coord].push_back(patch_entity_idx_list[i]);
    }
    if (access_iter != dr_box.get_net_access_point_map().end()) {
      for (AccessPoint* access_point : access_iter->second) {
        PlanarCoord& gcell_coord = access_point->get_grid_coord();
        if (RTUTIL.isInside(dr_box.get_box_rect().get_grid_rect(), gcell_coord) && gcell_entity_idx_map[gcell_coord].empty()) {
          gcell_entity_idx_map[gcell_coord].push_back(connectivity.addEntity({}));
        }
      }
    }
  }
  connectivity.build();
  if (dr_box.get_initial_routing()) {
    for (auto& [gcell_coord, entity_idx_list] : gcell_entity_idx_map) {
      for (size_t i = 1; i < entity_idx_list.size(); i++) {
        connectivity.merge(entity_idx_list.front(), entity_idx_list[i]);
      }
    }
  }
  std::map<int32_t, std::vector<DRGroup>> root_group_list_map;

  if (access_iter != dr_box.get_net_access_point_map().end()) {
    // 初始布线通过 gcell 关联 pin；后续迭代直接按形状包含关系关联。
    std::map<std::pair<int32_t, int32_t>, DRGroup> root_pin_group_map;
    for (AccessPoint* access_point : access_iter->second) {
      if (!RTUTIL.isInside(dr_box.get_box_rect().get_grid_rect(), access_point->get_grid_coord())) {
        continue;
      }
      LayerCoord coord = access_point->getRealLayerCoord();
      std::set<int32_t> root_set;
      if (dr_box.get_initial_routing()) {
        for (int32_t entity_idx : gcell_entity_idx_map[access_point->get_grid_coord()]) {
          root_set.insert(connectivity.getRoot(entity_idx));
        }
      } else {
        root_set = connectivity.getRootSet(coord);
      }
      for (int32_t root : root_set) {
        DRGroup& group = root_pin_group_map[{root, access_point->get_pin_idx()}];
        group.get_coord_direction_map()[coord].insert(RTDM.getDatabase().get_routing_layer_list()[coord.get_layer_idx()].get_prefer_direction());
      }
    }
    for (auto& [root_pin, group] : root_pin_group_map) {
      root_group_list_map[root_pin.first].push_back(std::move(group));
    }
  }

  PlanarRect& box_rect = dr_box.get_box_rect().get_real_rect();
  std::map<int32_t, std::map<LayerCoord, std::set<Direction>, CmpLayerCoordByXASC>> root_terminal_coord_direction_map;
  if (dr_box.get_initial_routing()) {
    // 初始布线没有已完成的物理拓扑，只能按切分边界生成终端。
    std::map<LayerCoord, std::set<Direction>, CmpLayerCoordByXASC> boundary_coord_direction_map;
    for (Segment<LayerCoord>& segment : result_list) {
      LayerCoord& first = segment.get_first();
      LayerCoord& second = segment.get_second();
      if (first.get_layer_idx() != second.get_layer_idx()) {
        continue;
      }
      if (RTUTIL.isHorizontal(first, second)) {
        int32_t first_x = first.get_x();
        int32_t second_x = second.get_x();
        RTUTIL.swapByASC(first_x, second_x);
        if (box_rect.get_ll_y() <= first.get_y() && first.get_y() <= box_rect.get_ur_y()) {
          for (int32_t x : {box_rect.get_ll_x(), box_rect.get_ur_x()}) {
            if (first_x <= x && x <= second_x) {
              boundary_coord_direction_map[LayerCoord(x, first.get_y(), first.get_layer_idx())].insert(Direction::kHorizontal);
            }
          }
        }
      } else if (RTUTIL.isVertical(first, second)) {
        int32_t first_y = first.get_y();
        int32_t second_y = second.get_y();
        RTUTIL.swapByASC(first_y, second_y);
        if (box_rect.get_ll_x() <= first.get_x() && first.get_x() <= box_rect.get_ur_x()) {
          for (int32_t y : {box_rect.get_ll_y(), box_rect.get_ur_y()}) {
            if (first_y <= y && y <= second_y) {
              boundary_coord_direction_map[LayerCoord(first.get_x(), y, first.get_layer_idx())].insert(Direction::kVertical);
            }
          }
        }
      }
    }
    for (auto& [coord, direction_set] : boundary_coord_direction_map) {
      for (int32_t root : connectivity.getRootSet(coord)) {
        root_terminal_coord_direction_map[root][coord].insert(direction_set.begin(), direction_set.end());
      }
    }
  } else {
    // repair 阶段的跨 box 接口必须来自相邻 owner 的实际线段端点。
    auto env_iter = dr_box.get_net_detailed_result_map().find(net_idx);
    if (env_iter != dr_box.get_net_detailed_result_map().end()) {
      for (Segment<LayerCoord>* segment : env_iter->second) {
        LayerCoord& first = segment->get_first();
        LayerCoord& second = segment->get_second();
        bool is_same_layer = first.get_layer_idx() == second.get_layer_idx();
        bool is_horizontal = RTUTIL.isHorizontal(first, second);
        bool is_vertical = RTUTIL.isVertical(first, second);
        if ((is_same_layer && !is_horizontal && !is_vertical && !RTUTIL.isProximal(first, second))
            || (!is_same_layer && first.get_planar_coord() != second.get_planar_coord())) {
          continue;
        }
        for (LayerCoord* coord : {&first, &second}) {
          if (!RTUTIL.isInside(box_rect, coord->get_planar_coord())) {
            continue;
          }
          Direction direction = RTDM.getDatabase().get_routing_layer_list()[coord->get_layer_idx()].get_prefer_direction();
          if (is_horizontal) {
            direction = Direction::kHorizontal;
          } else if (is_vertical) {
            direction = Direction::kVertical;
          }
          for (int32_t root : connectivity.getRootSet(*coord)) {
            root_terminal_coord_direction_map[root][*coord].insert(direction);
          }
        }
      }
    }
  }
  for (auto& [root, coord_direction_map] : root_terminal_coord_direction_map) {
    for (auto& [coord, direction_set] : coord_direction_map) {
      DRGroup group;
      group.get_coord_direction_map()[coord] = direction_set;
      root_group_list_map[root].push_back(std::move(group));
    }
  }

  std::map<int32_t, DRTaskComponent> root_component_map;
  std::vector<std::vector<LayerRect>>& shape_list_list = connectivity.get_shape_list_list();
  for (size_t i = 0; i < shape_list_list.size(); i++) {
    int32_t root = connectivity.getRoot(static_cast<int32_t>(i));
    DRTaskComponent& component = root_component_map[root];
    for (LayerRect& shape : shape_list_list[i]) {
      std::vector<int32_t> key = {shape.get_layer_idx(), shape.get_ll_x(), shape.get_ll_y(), shape.get_ur_x(), shape.get_ur_y()};
      if (component.stable_key.empty() || key < component.stable_key) {
        component.stable_key = std::move(key);
      }
    }
  }
  for (auto& [root, group_list] : root_group_list_map) {
    DRTaskComponent& component = root_component_map[root];
    component.group_list = std::move(group_list);
    if (component.stable_key.empty()) {
      for (DRGroup& group : component.group_list) {
        for (auto& [coord, direction_set] : group.get_coord_direction_map()) {
          std::vector<int32_t> key = {coord.get_layer_idx(), coord.get_x(), coord.get_y(), coord.get_x(), coord.get_y()};
          if (component.stable_key.empty() || key < component.stable_key) {
            component.stable_key = std::move(key);
          }
        }
      }
    }
  }
  for (size_t i = 0; i < result_list.size(); i++) {
    int32_t root = connectivity.getRoot(segment_entity_idx_list[i]);
    root_component_map[root].result_list.push_back(result_list[i]);
  }

  std::vector<DRTaskComponent> component_list;
  component_list.reserve(root_component_map.size());
  for (auto& [root, component] : root_component_map) {
    component_list.push_back(std::move(component));
  }
  // 固定 component_idx，保证跨迭代的任务顺序稳定。
  std::ranges::sort(component_list, [](const DRTaskComponent& a, const DRTaskComponent& b) { return a.stable_key < b.stable_key; });

  dr_box.get_net_component_result_map()[net_idx].clear();
  for (size_t i = 0; i < component_list.size(); i++) {
    DRTaskComponent& component = component_list[i];
    int32_t component_idx = static_cast<int32_t>(i);
    dr_box.get_net_component_result_map()[net_idx][component_idx] = std::move(component.result_list);
    if (component.group_list.size() < 2) {
      continue;
    }
    DRTask* dr_task = new DRTask();
    dr_task->set_net_idx(net_idx);
    dr_task->set_component_idx(component_idx);
    dr_task->set_connect_type(dr_model.get_dr_net_list()[net_idx].get_connect_type());
    dr_task->set_dr_group_list(component.group_list);
    std::vector<PlanarCoord> coord_list;
    for (DRGroup& group : component.group_list) {
      for (auto& [coord, direction_set] : group.get_coord_direction_map()) {
        coord_list.push_back(coord);
      }
    }
    dr_task->set_bounding_box(RTUTIL.getBoundingBox(coord_list));
    dr_box.get_dr_task_list().push_back(dr_task);
  }
}

void DetailedRouter::buildRouteViolation(DRModel& dr_model, const std::vector<DRBoxId>& dr_box_id_list)
{
  if (dr_model.get_route_violation_list().empty()) {
    return;
  }
  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();
  std::map<DRBoxId, size_t, CmpDRBoxId> box_idx_map;
  std::vector<std::set<int32_t>> need_checked_net_set_list(dr_box_id_list.size());
  std::vector<bool> checked_net_set_built_list(dr_box_id_list.size(), false);
  for (size_t i = 0; i < dr_box_id_list.size(); i++) {
    box_idx_map[dr_box_id_list[i]] = i;
  }

  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  std::vector<Violation>& route_violation_list = dr_model.get_route_violation_list();
  std::vector<Violation> remaining_violation_list;
  remaining_violation_list.reserve(route_violation_list.size());
  for (Violation& violation : route_violation_list) {
    bool assigned = false;
    PlanarRect repair_rect = RTUTIL.getEnlargedRect(violation.get_violation_shape().get_real_rect(), detection_distance);
    for (const DRBoxId& dr_box_id : getDRBoxIdSet(dr_model, repair_rect)) {
      auto box_iter = box_idx_map.find(dr_box_id);
      if (box_iter == box_idx_map.end()) {
        continue;
      }
      DRBox& dr_box = dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()];
      if (!RTUTIL.isClosedOverlap(dr_box.get_box_rect().get_real_rect(), repair_rect)) {
        continue;
      }
      size_t box_idx = box_iter->second;
      if (!checked_net_set_built_list[box_idx]) {
        for (DRTask* dr_task : dr_box.get_dr_task_list()) {
          need_checked_net_set_list[box_idx].insert(dr_task->get_net_idx());
        }
        checked_net_set_built_list[box_idx] = true;
      }
      bool exist_checked_net = false;
      for (int32_t violation_net_idx : violation.get_violation_net_set()) {
        if (RTUTIL.exist(need_checked_net_set_list[box_idx], violation_net_idx)) {
          exist_checked_net = true;
          break;
        }
      }
      if (exist_checked_net) {
        dr_box.get_route_violation_list().push_back(violation);
        assigned = true;
        break;
      }
    }
    if (!assigned) {
      remaining_violation_list.push_back(violation);
    }
  }
  route_violation_list = std::move(remaining_violation_list);
}

bool DetailedRouter::needRouting(DRBox& dr_box)
{
  if (dr_box.get_dr_task_list().empty()) {
    return false;
  }
  if (!dr_box.get_initial_routing() && dr_box.get_route_violation_list().empty()) {
    return false;
  }
  return true;
}

bool DetailedRouter::needRefine(DRBox& dr_box)
{
  if (dr_box.get_initial_routing() || dr_box.get_dr_iter_param()->get_refine_net_num() <= 0) {
    return false;
  }
  for (auto& [net_idx, segment_list] : dr_box.get_net_task_detailed_result_map()) {
    if (!segment_list.empty()) {
      return true;
    }
  }
  for (auto& [net_idx, patch_list] : dr_box.get_net_task_detailed_patch_map()) {
    if (!patch_list.empty()) {
      return true;
    }
  }
  return false;
}

void DetailedRouter::buildBoxTrackAxis(DRBox& dr_box)
{
  int32_t manufacture_grid = RTDM.getDatabase().get_manufacture_grid();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  std::vector<int32_t> x_scale_list;
  std::vector<int32_t> y_scale_list;

  PlanarRect& box_real_rect = dr_box.get_box_rect().get_real_rect();
  int32_t ll_x = box_real_rect.get_ll_x();
  int32_t ll_y = box_real_rect.get_ll_y();
  int32_t ur_x = box_real_rect.get_ur_x();
  int32_t ur_y = box_real_rect.get_ur_y();
  // 避免 off_grid
  while (ll_x % manufacture_grid != 0) {
    ll_x++;
  }
  while (ll_y % manufacture_grid != 0) {
    ll_y++;
  }
  while (ur_x % manufacture_grid != 0) {
    ur_x--;
  }
  while (ur_y % manufacture_grid != 0) {
    ur_y--;
  }
  std::map<int32_t, std::pair<std::set<int32_t>, std::set<int32_t>>>& layer_axis_map = dr_box.get_layer_axis_map();
  for (RoutingLayer& routing_layer : routing_layer_list) {
    for (int32_t x_scale : RTUTIL.getScaleList(ll_x, ur_x, routing_layer.getXTrackGridList())) {
      if (routing_layer.isPreferH()) {
        layer_axis_map[routing_layer.get_layer_idx()].first.insert(x_scale);
      }
    }
    for (int32_t y_scale : RTUTIL.getScaleList(ll_y, ur_y, routing_layer.getYTrackGridList())) {
      if (!routing_layer.isPreferH()) {
        layer_axis_map[routing_layer.get_layer_idx()].second.insert(y_scale);
      }
    }
  }
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    for (DRGroup& dr_group : dr_task->get_dr_group_list()) {
      for (auto& [coord, _] : dr_group.get_coord_direction_map()) {
        int32_t layer_idx = coord.get_layer_idx();
        layer_axis_map[layer_idx].first.insert(coord.get_x());
        layer_axis_map[layer_idx].second.insert(coord.get_y());
      }
    }
  }
  for (RoutingLayer& routing_layer : routing_layer_list) {
    for (int32_t x_scale : RTUTIL.getScaleList(ll_x, ur_x, routing_layer.getXTrackGridList())) {
      x_scale_list.push_back(x_scale);
    }
    for (int32_t y_scale : RTUTIL.getScaleList(ll_y, ur_y, routing_layer.getYTrackGridList())) {
      y_scale_list.push_back(y_scale);
    }
  }
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    for (DRGroup& dr_group : dr_task->get_dr_group_list()) {
      for (auto& [coord, _] : dr_group.get_coord_direction_map()) {
        x_scale_list.push_back(coord.get_x());
        y_scale_list.push_back(coord.get_y());
      }
    }
  }

  ScaleAxis& box_track_axis = dr_box.get_box_track_axis();
  std::ranges::sort(x_scale_list);
  x_scale_list.erase(std::ranges::unique(x_scale_list).begin(), x_scale_list.end());
  box_track_axis.set_x_grid_list(RTUTIL.makeScaleGridList(x_scale_list));
  std::ranges::sort(y_scale_list);
  y_scale_list.erase(std::ranges::unique(y_scale_list).begin(), y_scale_list.end());
  box_track_axis.set_y_grid_list(RTUTIL.makeScaleGridList(y_scale_list));
}

void DetailedRouter::buildLayerNodeMap(DRBox& dr_box)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  PlanarCoord& real_ll = dr_box.get_box_rect().get_real_ll();
  PlanarCoord& real_ur = dr_box.get_box_rect().get_real_ur();
  ScaleAxis& box_track_axis = dr_box.get_box_track_axis();
  std::vector<int32_t> x_list = RTUTIL.getScaleList(real_ll.get_x(), real_ur.get_x(), box_track_axis.get_x_grid_list());
  std::vector<int32_t> y_list = RTUTIL.getScaleList(real_ll.get_y(), real_ur.get_y(), box_track_axis.get_y_grid_list());

  std::vector<GridMap<DRNode>>& layer_node_map = dr_box.get_layer_node_map();
  layer_node_map.resize(routing_layer_list.size());
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(layer_node_map.size()); layer_idx++) {
    GridMap<DRNode>& dr_node_map = layer_node_map[layer_idx];
    dr_node_map.init(x_list.size(), y_list.size());
    for (size_t x = 0; x < x_list.size(); x++) {
      for (size_t y = 0; y < y_list.size(); y++) {
        DRNode& dr_node = dr_node_map[x][y];
        dr_node.set_x(x_list[x]);
        dr_node.set_y(y_list[y]);
        dr_node.set_layer_idx(layer_idx);
        dr_node.set_gcell_coord(RTUTIL.getGCellGridCoordByBBox(dr_node, gcell_axis, dr_box.get_box_rect()));
      }
    }
  }
}

void DetailedRouter::buildLayerShadowMap(DRBox& dr_box)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  dr_box.get_layer_shadow_map().resize(routing_layer_list.size());
}

void DetailedRouter::buildDRNodeNeighbor(DRBox& dr_box)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  int32_t bottom_routing_layer_idx = RTDM.getConfig().bottom_routing_layer_idx;
  int32_t top_routing_layer_idx = RTDM.getConfig().top_routing_layer_idx;

  std::vector<GridMap<DRNode>>& layer_node_map = dr_box.get_layer_node_map();
  std::map<int32_t, std::pair<std::set<int32_t>, std::set<int32_t>>>& layer_axis_map = dr_box.get_layer_axis_map();
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(layer_node_map.size()); layer_idx++) {
    bool routing_hv = true;
    if (layer_idx < bottom_routing_layer_idx || top_routing_layer_idx < layer_idx) {
      routing_hv = false;
    }
    GridMap<DRNode>& dr_node_map = layer_node_map[layer_idx];
    std::set<int32_t> neighbor_layer_x_axis_set;
    std::set<int32_t> neighbor_layer_y_axis_set;
    if (layer_idx != 0) {
      neighbor_layer_x_axis_set.insert(layer_axis_map[layer_idx - 1].first.begin(), layer_axis_map[layer_idx - 1].first.end());
      neighbor_layer_y_axis_set.insert(layer_axis_map[layer_idx - 1].second.begin(), layer_axis_map[layer_idx - 1].second.end());
    }
    if (layer_idx != static_cast<int32_t>(layer_node_map.size()) - 1) {
      neighbor_layer_x_axis_set.insert(layer_axis_map[layer_idx + 1].first.begin(), layer_axis_map[layer_idx + 1].first.end());
      neighbor_layer_y_axis_set.insert(layer_axis_map[layer_idx + 1].second.begin(), layer_axis_map[layer_idx + 1].second.end());
    }
    std::set<int32_t>& curr_axis = (routing_layer_list[layer_idx].isPreferH()) ? layer_axis_map[layer_idx].first : layer_axis_map[layer_idx].second;
    for (int32_t x = 0; x < dr_node_map.get_x_size(); x++) {
      for (int32_t y = 0; y < dr_node_map.get_y_size(); y++) {
        DRNode& dr_node = dr_node_map[x][y];
        if (routing_hv) {
          if (!routing_layer_list[layer_idx].isPreferH()) {
            if (RTUTIL.exist(curr_axis, dr_node.get_y()) || RTUTIL.exist(neighbor_layer_y_axis_set, dr_node.get_y())) {
              if (x != 0) {
                dr_node.setNeighborNode(Orientation::kWest, &dr_node_map[x - 1][y]);
              }
              if (x != (dr_node_map.get_x_size() - 1)) {
                dr_node.setNeighborNode(Orientation::kEast, &dr_node_map[x + 1][y]);
              }
            }
            if (RTUTIL.exist(neighbor_layer_x_axis_set, dr_node.get_x())) {
              if (y != 0) {
                dr_node.setNeighborNode(Orientation::kSouth, &dr_node_map[x][y - 1]);
              }
              if (y != (dr_node_map.get_y_size() - 1)) {
                dr_node.setNeighborNode(Orientation::kNorth, &dr_node_map[x][y + 1]);
              }
            }
          } else if (routing_layer_list[layer_idx].isPreferH()) {
            if (RTUTIL.exist(curr_axis, dr_node.get_x()) || RTUTIL.exist(neighbor_layer_x_axis_set, dr_node.get_x())) {
              if (y != 0) {
                dr_node.setNeighborNode(Orientation::kSouth, &dr_node_map[x][y - 1]);
              }
              if (y != (dr_node_map.get_y_size() - 1)) {
                dr_node.setNeighborNode(Orientation::kNorth, &dr_node_map[x][y + 1]);
              }
            }
            if (RTUTIL.exist(neighbor_layer_y_axis_set, dr_node.get_y())) {
              if (x != 0) {
                dr_node.setNeighborNode(Orientation::kWest, &dr_node_map[x - 1][y]);
              }
              if (x != (dr_node_map.get_x_size() - 1)) {
                dr_node.setNeighborNode(Orientation::kEast, &dr_node_map[x + 1][y]);
              }
            }
          }
        }
        if (layer_idx != 0) {
          dr_node.setNeighborNode(Orientation::kBelow, &layer_node_map[layer_idx - 1][x][y]);
        }
        if (layer_idx != static_cast<int32_t>(layer_node_map.size()) - 1) {
          dr_node.setNeighborNode(Orientation::kAbove, &layer_node_map[layer_idx + 1][x][y]);
        }
      }
    }
  }
}

void DetailedRouter::buildOrientNetMap(DRBox& dr_box)
{
  for (auto& [is_routing, layer_net_fixed_rect_map] : dr_box.get_type_layer_net_fixed_rect_map()) {
    for (auto& [layer_idx, net_fixed_rect_map] : layer_net_fixed_rect_map) {
      for (auto& [net_idx, fixed_rect_set] : net_fixed_rect_map) {
        for (const auto& fixed_rect : fixed_rect_set) {
          updateFixedRectToGraph(dr_box, ChangeType::kAdd, net_idx, fixed_rect, is_routing);
        }
      }
    }
  }
  for (auto& [net_idx, segment_list] : dr_box.get_net_detailed_result_map()) {
    for (Segment<LayerCoord>* segment : segment_list) {
      updateFixedRectToGraph(dr_box, ChangeType::kAdd, net_idx, segment);
    }
  }
  for (auto& [net_idx, segment_list] : dr_box.get_net_task_detailed_result_map()) {
    for (Segment<LayerCoord>& segment : segment_list) {
      updateRoutedRectToGraph(dr_box, ChangeType::kAdd, net_idx, segment);
    }
  }
  for (auto& [net_idx, patch_list] : dr_box.get_net_detailed_patch_map()) {
    for (EXTLayerRect* patch : patch_list) {
      updateFixedRectToGraph(dr_box, ChangeType::kAdd, net_idx, patch, true);
    }
  }
  for (auto& [net_idx, patch_list] : dr_box.get_net_task_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      updateRoutedRectToGraph(dr_box, ChangeType::kAdd, net_idx, patch, true);
    }
  }
  for (Violation& violation : dr_box.get_route_violation_list()) {
    addRouteViolationToGraph(dr_box, violation);
  }
}

void DetailedRouter::buildNetShadowMap(DRBox& dr_box)
{
  for (auto& [is_routing, layer_net_fixed_rect_map] : dr_box.get_type_layer_net_fixed_rect_map()) {
    for (auto& [layer_idx, net_fixed_rect_map] : layer_net_fixed_rect_map) {
      for (auto& [net_idx, fixed_rect_set] : net_fixed_rect_map) {
        for (const auto& fixed_rect : fixed_rect_set) {
          updateFixedRectToShadow(dr_box, ChangeType::kAdd, net_idx, fixed_rect, is_routing);
        }
      }
    }
  }
  for (auto& [net_idx, segment_list] : dr_box.get_net_detailed_result_map()) {
    for (Segment<LayerCoord>* segment : segment_list) {
      updateFixedRectToShadow(dr_box, ChangeType::kAdd, net_idx, segment);
    }
  }
  for (auto& [net_idx, segment_list] : dr_box.get_net_task_detailed_result_map()) {
    for (Segment<LayerCoord>& segment : segment_list) {
      updateRoutedRectToShadow(dr_box, ChangeType::kAdd, net_idx, segment);
    }
  }
  for (auto& [net_idx, patch_list] : dr_box.get_net_detailed_patch_map()) {
    for (EXTLayerRect* patch : patch_list) {
      updateFixedRectToShadow(dr_box, ChangeType::kAdd, net_idx, patch, true);
    }
  }
  for (auto& [net_idx, patch_list] : dr_box.get_net_task_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      updateRoutedRectToShadow(dr_box, ChangeType::kAdd, net_idx, patch, true);
    }
  }
}

void DetailedRouter::exemptPinShape(DRModel& dr_model, DRBox& dr_box)
{
  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<DRNet>& dr_net_list = dr_model.get_dr_net_list();
  ScaleAxis& box_track_axis = dr_box.get_box_track_axis();
  std::vector<GridMap<DRNode>>& layer_node_map = dr_box.get_layer_node_map();
  auto& routing_fixed_rect_rtree_map = RTDM.getDatabase().get_type_layer_fixed_rect_rtree_map()[true];

  for (auto& [dr_net_idx, access_point_set] : dr_box.get_net_access_point_map()) {
    std::vector<DRPin>& dr_pin_list = dr_net_list[dr_net_idx].get_dr_pin_list();
    for (AccessPoint* access_point : access_point_set) {
      if (dr_pin_list[access_point->get_pin_idx()].get_is_core()) {
        if (!RTUTIL.existTrackGrid(access_point->get_real_coord(), box_track_axis)) {
          continue;
        }
        PlanarCoord grid_coord = RTUTIL.getTrackGrid(access_point->get_real_coord(), box_track_axis);
        DRNode& dr_node = layer_node_map[access_point->get_layer_idx()][grid_coord.get_x()][grid_coord.get_y()];
        for (Orientation orient : {Orientation::kAbove, Orientation::kBelow}) {
          if (!dr_node.hasFixedRectOrient(orient)) {
            continue;
          }
          dr_node.delFixedRectNet(orient, -1);
          DRNode* neighbor_node = dr_node.getNeighborNode(orient);
          if (neighbor_node != nullptr) {
            neighbor_node->delFixedRectNet(RTUTIL.getOppositeOrientation(orient), -1);
          }
        }
      } else {
        auto rtree_iter = routing_fixed_rect_rtree_map.find(access_point->get_layer_idx());
        PlanarRect real_rect = RTUTIL.getEnlargedRect(access_point->get_real_coord(), detection_distance);
        if (!RTUTIL.existTrackGrid(real_rect, box_track_axis)) {
          continue;
        }
        PlanarRect grid_rect = RTUTIL.getTrackGrid(real_rect, box_track_axis);
        for (int32_t x = grid_rect.get_ll_x(); x <= grid_rect.get_ur_x(); x++) {
          for (int32_t y = grid_rect.get_ll_y(); y <= grid_rect.get_ur_y(); y++) {
            DRNode& dr_node = layer_node_map[access_point->get_layer_idx()][x][y];

            bool within_shape = false;
            if (rtree_iter != routing_fixed_rect_rtree_map.end()) {
              PlanarRect query_rect(dr_node.get_planar_coord(), dr_node.get_planar_coord());
              for (auto query_iter = rtree_iter->second.qbegin(bgi::intersects(RTUTIL.convertToBGRectInt(query_rect))); query_iter != rtree_iter->second.qend();
                   query_iter++) {
                if (query_iter->second.first == dr_net_idx) {
                  continue;
                }
                within_shape = true;
                break;
              }
            }
            if (within_shape) {
              continue;
            }
            bool prefer_horizontal = routing_layer_list[dr_node.get_layer_idx()].isPreferH();
            if (prefer_horizontal) {
              dr_node.delFixedRectNet(Orientation::kEast, -1);
              dr_node.delFixedRectNet(Orientation::kWest, -1);
            } else {
              dr_node.delFixedRectNet(Orientation::kSouth, -1);
              dr_node.delFixedRectNet(Orientation::kNorth, -1);
            }
          }
        }
      }
    }
  }
}

void DetailedRouter::routeDRBox(DRBox& dr_box)
{
  std::vector<int32_t> routing_net_list = initTaskSchedule(dr_box);
  while (!routing_net_list.empty()) {
    for (int32_t net_idx : routing_net_list) {
      routeDRNet(dr_box, net_idx);
    }
    updateRouteViolationList(dr_box);
    updateBestResult(dr_box);
    updateTaskSchedule(dr_box, routing_net_list);
  }
  if (dr_box.get_dr_iter_param()->get_refine_net_num() > 0) {
    appendRepairedNetsToRefine(dr_box);
    if (refineCleanNets(dr_box)) {
      updateRouteViolationList(dr_box);
      updateBestResult(dr_box);
    }
  }
}

std::vector<int32_t> DetailedRouter::initTaskSchedule(DRBox& dr_box)
{
  std::vector<int32_t> routing_net_list;
  if (dr_box.get_initial_routing()) {
    std::set<int32_t> net_idx_set;
    for (DRTask* dr_task : dr_box.get_dr_task_list()) {
      net_idx_set.insert(dr_task->get_net_idx());
    }
    routing_net_list.assign(net_idx_set.begin(), net_idx_set.end());
  } else {
    updateTaskSchedule(dr_box, routing_net_list);
  }
  return routing_net_list;
}

void DetailedRouter::updateGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, std::vector<Segment<LayerCoord>>& segment_list,
                                 std::vector<EXTLayerRect>& patch_list)
{
  for (Segment<LayerCoord>& segment : segment_list) {
    updateRoutedRectToGraph(dr_box, change_type, net_idx, segment);
    updateRoutedRectToShadow(dr_box, change_type, net_idx, segment);
  }
  for (EXTLayerRect& patch : patch_list) {
    updateRoutedRectToGraph(dr_box, change_type, net_idx, patch, true);
    updateRoutedRectToShadow(dr_box, change_type, net_idx, patch, true);
  }
}

std::vector<DRTask*> DetailedRouter::resetDRNetResult(DRBox& dr_box, int32_t net_idx)
{
  std::vector<DRTask*> net_task_list;
  std::set<int32_t> routing_component_set;
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    if (dr_task->get_net_idx() == net_idx) {
      net_task_list.push_back(dr_task);
      routing_component_set.insert(dr_task->get_component_idx());
    }
  }
  std::vector<Segment<LayerCoord>>& result_list = dr_box.get_net_task_detailed_result_map()[net_idx];
  std::vector<EXTLayerRect>& patch_list = dr_box.get_net_task_detailed_patch_map()[net_idx];
  std::vector<Segment<LayerCoord>> empty_result_list;
  updateGraph(dr_box, ChangeType::kDel, net_idx, empty_result_list, patch_list);
  patch_list.clear();

  std::vector<EXTLayerRect> empty_patch_list;
  std::map<int32_t, std::vector<Segment<LayerCoord>>>& component_result_map = dr_box.get_net_component_result_map()[net_idx];
  result_list.clear();
  for (auto component_iter = component_result_map.begin(); component_iter != component_result_map.end();) {
    auto& [component_idx, component_result_list] = *component_iter;
    if (RTUTIL.exist(routing_component_set, component_idx)) {
      updateGraph(dr_box, ChangeType::kDel, net_idx, component_result_list, empty_patch_list);
      component_iter = component_result_map.erase(component_iter);
      continue;
    }
    component_iter++;
  }
  return net_task_list;
}

void DetailedRouter::routeDRNet(DRBox& dr_box, int32_t net_idx)
{
  std::vector<DRTask*> net_task_list;
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    if (dr_task->get_net_idx() == net_idx) {
      net_task_list.push_back(dr_task);
    }
  }
  if (net_task_list.empty()) {
    RTLOG.error(Loc::current(), "The routing net has no task! net_idx: ", net_idx);
  }
  resetDRNetResult(dr_box, net_idx);
  std::vector<Segment<LayerCoord>>& result_list = dr_box.get_net_task_detailed_result_map()[net_idx];
  std::map<int32_t, std::vector<Segment<LayerCoord>>>& component_result_map = dr_box.get_net_component_result_map()[net_idx];
  for (DRTask* dr_task : net_task_list) {
    routeDRTask(dr_box, dr_task);
  }
  for (const auto& [component_idx, component_result_list] : component_result_map) {
    result_list.insert(result_list.end(), component_result_list.begin(), component_result_list.end());
  }
  patchDRTask(dr_box, net_task_list.front());
  dr_box.get_net_routed_times_map()[net_idx]++;
}

double DetailedRouter::getNetResultCost(DRBox& dr_box, int32_t net_idx)
{
  double cost = 0;
  DRIterParam* dr_iter_param = dr_box.get_dr_iter_param();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  auto result_iter = dr_box.get_net_task_detailed_result_map().find(net_idx);
  if (result_iter != dr_box.get_net_task_detailed_result_map().end()) {
    for (Segment<LayerCoord>& segment : result_iter->second) {
      LayerCoord& first = segment.get_first();
      LayerCoord& second = segment.get_second();
      if (first.get_layer_idx() == second.get_layer_idx()) {
        double wire_length = RTUTIL.getManhattanDistance(first.get_planar_coord(), second.get_planar_coord());
        if (routing_layer_list[first.get_layer_idx()].get_prefer_direction() == RTUTIL.getDirection(first, second)) {
          cost += wire_length * dr_iter_param->get_prefer_wire_unit();
        } else {
          cost += wire_length * dr_iter_param->get_non_prefer_wire_unit();
        }
      } else if (first.get_planar_coord() == second.get_planar_coord()) {
        cost += std::abs(first.get_layer_idx() - second.get_layer_idx()) * dr_iter_param->get_via_unit();
      }
    }
  }
  auto patch_iter = dr_box.get_net_task_detailed_patch_map().find(net_idx);
  if (patch_iter != dr_box.get_net_task_detailed_patch_map().end()) {
    for (EXTLayerRect& patch : patch_iter->second) {
      cost += dr_iter_param->get_non_prefer_wire_unit() * (patch.get_real_rect().getXSpan() + patch.get_real_rect().getYSpan());
    }
  }
  return cost;
}

bool DetailedRouter::hasUncoveredBoxAccessPoint(DRBox& dr_box, int32_t net_idx)
{
  auto access_iter = dr_box.get_net_access_point_map().find(net_idx);
  auto result_iter = dr_box.get_net_task_detailed_result_map().find(net_idx);
  if (access_iter == dr_box.get_net_access_point_map().end() || result_iter == dr_box.get_net_task_detailed_result_map().end()) {
    return false;
  }
  PlanarRect& box_real_rect = dr_box.get_box_rect().get_real_rect();
  for (AccessPoint* access_point : access_iter->second) {
    LayerCoord coord = access_point->getRealLayerCoord();
    bool covered = false;
    for (Segment<LayerCoord>& segment : result_iter->second) {
      if (RTUTIL.isInside(segment, coord)) {
        covered = true;
        break;
      }
    }
    if (covered && !RTUTIL.isInside(box_real_rect, coord.get_planar_coord())) {
      return true;
    }
  }
  return false;
}

bool DetailedRouter::coverBoxAccessPoints(DRBox& dr_box, int32_t net_idx, const std::vector<Segment<LayerCoord>>& old_result_list)
{
  auto access_iter = dr_box.get_net_access_point_map().find(net_idx);
  if (access_iter == dr_box.get_net_access_point_map().end()) {
    return true;
  }
  auto result_iter = dr_box.get_net_task_detailed_result_map().find(net_idx);
  if (result_iter == dr_box.get_net_task_detailed_result_map().end()) {
    return false;
  }
  for (AccessPoint* access_point : access_iter->second) {
    LayerCoord coord = access_point->getRealLayerCoord();
    bool old_covered = false;
    for (const Segment<LayerCoord>& segment : old_result_list) {
      if (RTUTIL.isInside(segment, coord)) {
        old_covered = true;
        break;
      }
    }
    if (!old_covered) {
      continue;
    }

    ViaMasterIdx required_via_master_idx = getRequiredAPViaMasterIdx(net_idx, *access_point);
    bool new_covered = false;
    for (Segment<LayerCoord>& segment : result_iter->second) {
      if (!RTUTIL.isInside(segment, coord)) {
        continue;
      }
      if (segment.get_first().get_layer_idx() == segment.get_second().get_layer_idx() || !required_via_master_idx.isValid()) {
        new_covered = true;
        break;
      }
      if (segment.hasValidViaMaster() && segment.get_via_master_idx() == required_via_master_idx) {
        new_covered = true;
        break;
      }
    }
    if (!new_covered) {
      return false;
    }
  }
  return true;
}

void DetailedRouter::appendRepairedNetsToRefine(DRBox& dr_box)
{
  int32_t max_refine_net_num = dr_box.get_dr_iter_param()->get_refine_net_num();
  std::vector<int32_t>& refine_net_list = dr_box.get_refine_net_list();
  if (max_refine_net_num <= 0 || static_cast<int32_t>(refine_net_list.size()) >= max_refine_net_num) {
    return;
  }

  std::set<int32_t> selected_net_set(refine_net_list.begin(), refine_net_list.end());
  std::set<int32_t> task_net_set;
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    task_net_set.insert(dr_task->get_net_idx());
  }
  std::set<int32_t> violation_net_set;
  for (Violation& violation : dr_box.get_route_violation_list()) {
    violation_net_set.insert(violation.get_violation_net_set().begin(), violation.get_violation_net_set().end());
  }

  struct RepairedNetCandidate
  {
    int32_t net_idx = -1;
    int32_t routed_times = 0;
    int64_t wire_length = 0;
  };
  std::vector<RepairedNetCandidate> candidate_list;
  for (auto& [net_idx, routed_times] : dr_box.get_net_routed_times_map()) {
    if (routed_times <= 0 || !RTUTIL.exist(task_net_set, net_idx) || RTUTIL.exist(selected_net_set, net_idx) || RTUTIL.exist(violation_net_set, net_idx)
        || hasUncoveredBoxAccessPoint(dr_box, net_idx)) {
      continue;
    }
    RepairedNetCandidate candidate;
    candidate.net_idx = net_idx;
    candidate.routed_times = routed_times;
    auto result_iter = dr_box.get_net_task_detailed_result_map().find(net_idx);
    if (result_iter != dr_box.get_net_task_detailed_result_map().end()) {
      for (Segment<LayerCoord>& segment : result_iter->second) {
        if (segment.get_first().get_layer_idx() == segment.get_second().get_layer_idx()) {
          candidate.wire_length += RTUTIL.getManhattanDistance(segment.get_first().get_planar_coord(), segment.get_second().get_planar_coord());
        }
      }
    }
    candidate_list.push_back(candidate);
  }
  std::sort(candidate_list.begin(), candidate_list.end(), [](const RepairedNetCandidate& first, const RepairedNetCandidate& second) {
    if (first.routed_times != second.routed_times) {
      return first.routed_times > second.routed_times;
    }
    if (first.wire_length != second.wire_length) {
      return first.wire_length > second.wire_length;
    }
    return first.net_idx < second.net_idx;
  });
  for (RepairedNetCandidate& candidate : candidate_list) {
    if (static_cast<int32_t>(refine_net_list.size()) >= max_refine_net_num) {
      break;
    }
    refine_net_list.push_back(candidate.net_idx);
  }
}

int32_t DetailedRouter::countNetBoxViolation(DRBox& dr_box, int32_t net_idx)
{
  auto result_iter = dr_box.get_net_task_detailed_result_map().find(net_idx);
  auto patch_iter = dr_box.get_net_task_detailed_patch_map().find(net_idx);
  int32_t violation_num = 0;
  for (Violation& violation : dr_box.get_route_violation_list()) {
    if (RTUTIL.exist(violation.get_violation_net_set(), net_idx)) {
      violation_num++;
      continue;
    }
    LayerRect violation_rect(violation.get_violation_shape().get_real_rect(), violation.get_violation_shape().get_layer_idx());
    bool overlap = false;
    if (result_iter != dr_box.get_net_task_detailed_result_map().end()) {
      for (Segment<LayerCoord>& segment : result_iter->second) {
        if (RTUTIL.isOverlap(violation_rect, segment)) {
          overlap = true;
          break;
        }
      }
    }
    if (!overlap && patch_iter != dr_box.get_net_task_detailed_patch_map().end()) {
      for (EXTLayerRect& patch : patch_iter->second) {
        if (patch.get_layer_idx() == violation_rect.get_layer_idx() && RTUTIL.isClosedOverlap(violation_rect, patch.get_real_rect())) {
          overlap = true;
          break;
        }
      }
    }
    if (overlap) {
      violation_num++;
    }
  }
  return violation_num;
}

bool DetailedRouter::refineCleanNets(DRBox& dr_box)
{
  if (dr_box.get_refine_net_list().empty()) {
    return false;
  }
  // Model violations are assigned to one box, while refine evaluates the complete local box state.
  dr_box.get_route_violation_list() = getRouteViolationList(dr_box);

  std::set<int32_t> task_net_set;
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    task_net_set.insert(dr_task->get_net_idx());
  }
  std::vector<int32_t> refine_net_list;
  for (int32_t net_idx : dr_box.get_refine_net_list()) {
    if (!RTUTIL.exist(task_net_set, net_idx)) {
      RTLOG.error(Loc::current(), "The selected refine net has no task! net_idx: ", net_idx);
    }
    if (countNetBoxViolation(dr_box, net_idx) == 0) {
      refine_net_list.push_back(net_idx);
    }
  }
  if (refine_net_list.empty()) {
    return false;
  }

  struct NetRefineSnapshot
  {
    std::vector<Segment<LayerCoord>> result_list;
    std::vector<EXTLayerRect> patch_list;
    std::map<int32_t, std::vector<Segment<LayerCoord>>> component_result_map;
    std::vector<Violation> route_violation_list;
    bool has_routed_times = false;
    int32_t routed_times = 0;
    double cost = 0;
  };

  bool refined = false;
  for (int32_t net_idx : refine_net_list) {
    auto result_iter = dr_box.get_net_task_detailed_result_map().find(net_idx);
    auto patch_iter = dr_box.get_net_task_detailed_patch_map().find(net_idx);
    auto component_iter = dr_box.get_net_component_result_map().find(net_idx);
    if (result_iter == dr_box.get_net_task_detailed_result_map().end() || patch_iter == dr_box.get_net_task_detailed_patch_map().end()
        || component_iter == dr_box.get_net_component_result_map().end()) {
      RTLOG.error(Loc::current(), "The refine net has incomplete owned state! net_idx: ", net_idx);
    }
    NetRefineSnapshot snapshot;
    snapshot.result_list = result_iter->second;
    snapshot.patch_list = patch_iter->second;
    snapshot.component_result_map = component_iter->second;
    snapshot.route_violation_list = dr_box.get_route_violation_list();
    auto routed_times_iter = dr_box.get_net_routed_times_map().find(net_idx);
    if (routed_times_iter != dr_box.get_net_routed_times_map().end()) {
      snapshot.has_routed_times = true;
      snapshot.routed_times = routed_times_iter->second;
    }
    snapshot.cost = getNetResultCost(dr_box, net_idx);

    dr_box.set_refine_routing(true);
    routeDRNet(dr_box, net_idx);
    dr_box.set_refine_routing(false);
    updateRouteViolationList(dr_box);

    bool accept = getNetResultCost(dr_box, net_idx) < snapshot.cost && dr_box.get_route_violation_list().size() <= snapshot.route_violation_list.size()
                  && countNetBoxViolation(dr_box, net_idx) == 0 && coverBoxAccessPoints(dr_box, net_idx, snapshot.result_list);
    if (accept) {
      refined = true;
      updateBestResult(dr_box);
      continue;
    }

    std::vector<DRTask*> net_task_list = resetDRNetResult(dr_box, net_idx);
    dr_box.get_net_task_detailed_result_map().find(net_idx)->second = snapshot.result_list;
    dr_box.get_net_task_detailed_patch_map().find(net_idx)->second = snapshot.patch_list;
    dr_box.get_net_component_result_map().find(net_idx)->second = snapshot.component_result_map;
    if (snapshot.has_routed_times) {
      dr_box.get_net_routed_times_map()[net_idx] = snapshot.routed_times;
    } else {
      dr_box.get_net_routed_times_map().erase(net_idx);
    }
    std::vector<Segment<LayerCoord>> restored_routing_result_list;
    for (DRTask* dr_task : net_task_list) {
      auto snapshot_component_iter = snapshot.component_result_map.find(dr_task->get_component_idx());
      if (snapshot_component_iter == snapshot.component_result_map.end()) {
        RTLOG.error(Loc::current(), "The refine snapshot has no routing component! net_idx: ", net_idx, ", component_idx: ", dr_task->get_component_idx());
      }
      restored_routing_result_list.insert(restored_routing_result_list.end(), snapshot_component_iter->second.begin(), snapshot_component_iter->second.end());
    }
    updateGraph(dr_box, ChangeType::kAdd, net_idx, restored_routing_result_list, snapshot.patch_list);
    std::set<Violation, CmpViolation> snapshot_violation_set(snapshot.route_violation_list.begin(), snapshot.route_violation_list.end());
    std::vector<Violation> restored_violation_list = getRouteViolationList(dr_box);
    std::set<Violation, CmpViolation> restored_violation_set(restored_violation_list.begin(), restored_violation_list.end());
    if (snapshot_violation_set != restored_violation_set) {
      RTLOG.error(Loc::current(), "The refine rollback did not restore the box violations! net_idx: ", net_idx);
    }
    dr_box.get_route_violation_list() = snapshot.route_violation_list;
  }
  return refined;
}

void DetailedRouter::routeDRTask(DRBox& dr_box, DRTask* dr_task)
{
  initSingleRouteTask(dr_box, dr_task);
  while (!isConnectedAllEnd(dr_box)) {
    routeSinglePath(dr_box);
    updatePathResult(dr_box);
    updateDirectionSet(dr_box);
    resetStartAndEnd(dr_box);
    resetSinglePath(dr_box);
  }
  updateTaskResult(dr_box);
  resetSingleRouteTask(dr_box);
}

void DetailedRouter::initSingleRouteTask(DRBox& dr_box, DRTask* dr_task)
{
  ScaleAxis& box_track_axis = dr_box.get_box_track_axis();
  std::vector<GridMap<DRNode>>& layer_node_map = dr_box.get_layer_node_map();
  std::map<LayerCoord, AccessPoint*, CmpLayerCoordByXASC> source_access_point_map;
  auto access_iter = dr_box.get_net_access_point_map().find(dr_task->get_net_idx());
  if (access_iter != dr_box.get_net_access_point_map().end()) {
    for (AccessPoint* access_point : access_iter->second) {
      if (!access_point->get_candidate_via_list().empty()) {
        source_access_point_map[access_point->getRealLayerCoord()] = access_point;
      }
    }
  }

  dr_box.set_curr_route_task(dr_task);
  dr_box.get_source_node_access_point_map().clear();
  {
    std::vector<std::vector<DRNode*>> node_list_list;
    std::vector<DRGroup>& dr_group_list = dr_task->get_dr_group_list();
    for (DRGroup& dr_group : dr_group_list) {
      std::vector<DRNode*> node_list;
      for (auto& [coord, direction_set] : dr_group.get_coord_direction_map()) {
        if (!RTUTIL.existTrackGrid(coord, box_track_axis)) {
          RTLOG.error(Loc::current(), "The coord can not find grid!");
        }
        PlanarCoord grid_coord = RTUTIL.getTrackGrid(coord, box_track_axis);
        DRNode& dr_node = layer_node_map[coord.get_layer_idx()][grid_coord.get_x()][grid_coord.get_y()];
        dr_node.setDirectionSet(direction_set);
        node_list.push_back(&dr_node);
        auto source_iter = source_access_point_map.find(coord);
        if (source_iter != source_access_point_map.end()) {
          dr_box.get_source_node_access_point_map()[&dr_node] = source_iter->second;
        }
      }
      node_list_list.push_back(node_list);
    }
    for (size_t i = 0; i < node_list_list.size(); i++) {
      if (i == 0) {
        dr_box.get_start_node_list_list().push_back(node_list_list[i]);
      } else {
        dr_box.get_end_node_list_list().push_back(node_list_list[i]);
      }
    }
  }
  dr_box.get_path_node_list().clear();
  dr_box.get_single_task_visited_node_list().clear();
  dr_box.get_routing_segment_list().clear();
}

bool DetailedRouter::isConnectedAllEnd(DRBox& dr_box)
{
  return dr_box.get_end_node_list_list().empty();
}

void DetailedRouter::routeSinglePath(DRBox& dr_box)
{
  initPathHead(dr_box);
  while (!searchEnded(dr_box)) {
    expandSearching(dr_box);
    resetPathHead(dr_box);
  }
}

void DetailedRouter::initPathHead(DRBox& dr_box)
{
  std::vector<std::vector<DRNode*>>& start_node_list_list = dr_box.get_start_node_list_list();
  std::vector<DRNode*>& path_node_list = dr_box.get_path_node_list();

  for (std::vector<DRNode*>& start_node_list : start_node_list_list) {
    for (DRNode* start_node : start_node_list) {
      start_node->set_estimated_cost(getEstimateCostToEnd(dr_box, start_node));
      pushToOpenList(dr_box, start_node);
    }
  }
  for (DRNode* path_node : path_node_list) {
    path_node->set_estimated_cost(getEstimateCostToEnd(dr_box, path_node));
    pushToOpenList(dr_box, path_node);
  }
  resetPathHead(dr_box);
}

bool DetailedRouter::searchEnded(DRBox& dr_box)
{
  std::vector<std::vector<DRNode*>>& end_node_list_list = dr_box.get_end_node_list_list();
  DRNode* path_head_node = dr_box.get_path_head_node();

  if (path_head_node == nullptr) {
    dr_box.set_end_node_list_idx(-1);
    return true;
  }
  for (size_t i = 0; i < end_node_list_list.size(); i++) {
    for (DRNode* end_node : end_node_list_list[i]) {
      if (path_head_node == end_node) {
        dr_box.set_end_node_list_idx(static_cast<int32_t>(i));
        return true;
      }
    }
  }
  return false;
}

void DetailedRouter::expandSearching(DRBox& dr_box)
{
  OpenQueue<DRNode>& open_queue = dr_box.get_open_queue();
  DRNode* path_head_node = dr_box.get_path_head_node();

  for (Orientation orientation : DRNode::kOrientationList) {
    DRNode* neighbor_node = path_head_node->getNeighborNode(orientation);
    if (neighbor_node == nullptr) {
      continue;
    }
    if (neighbor_node->isClose()) {
      continue;
    }
    ViaMasterIdx parent_via_master_idx;
    if (orientation == Orientation::kAbove || orientation == Orientation::kBelow) {
      if (!isViaEdgeAllowedByAP(dr_box, path_head_node, neighbor_node, parent_via_master_idx)) {
        continue;
      }
    }
    double known_cost = getKnownCost(dr_box, path_head_node, neighbor_node, orientation);
    if (neighbor_node->isOpen() && known_cost < neighbor_node->get_known_cost()) {
      neighbor_node->set_known_cost(known_cost);
      neighbor_node->set_parent_node(path_head_node);
      neighbor_node->set_parent_via_master_idx(parent_via_master_idx);
      open_queue.push(neighbor_node);
    } else if (neighbor_node->isNone()) {
      neighbor_node->set_known_cost(known_cost);
      neighbor_node->set_parent_node(path_head_node);
      neighbor_node->set_parent_via_master_idx(parent_via_master_idx);
      neighbor_node->set_estimated_cost(getEstimateCostToEnd(dr_box, neighbor_node));
      pushToOpenList(dr_box, neighbor_node);
    }
  }
}

bool DetailedRouter::isViaEdgeAllowedByAP(DRBox& dr_box, DRNode* first_node, DRNode* second_node, ViaMasterIdx& via_master_idx)
{
  via_master_idx = ViaMasterIdx();
  int32_t net_idx = dr_box.get_curr_route_task()->get_net_idx();
  int32_t below_layer_idx = std::min(first_node->get_layer_idx(), second_node->get_layer_idx());
  if (first_node->get_layer_idx() == second_node->get_layer_idx()) {
    RTLOG.error(Loc::current(), "The edge is not a via edge!");
  }
  if (below_layer_idx < 0 || below_layer_idx >= static_cast<int32_t>(RTDM.getDatabase().get_layer_via_master_list().size())) {
    RTLOG.error(Loc::current(), "The via edge has an invalid below layer! layer_idx: ", below_layer_idx);
  }

  for (DRNode* dr_node : {first_node, second_node}) {
    auto source_iter = dr_box.get_source_node_access_point_map().find(dr_node);
    if (source_iter == dr_box.get_source_node_access_point_map().end()) {
      continue;
    }
    ViaMasterIdx required_via_master_idx = getRequiredAPViaMasterIdx(net_idx, *source_iter->second);
    if (!required_via_master_idx.isValid()) {
      continue;
    }
    if (required_via_master_idx.get_below_layer_idx() != below_layer_idx) {
      return false;
    }
    if (via_master_idx.isValid() && via_master_idx != required_via_master_idx) {
      RTLOG.error(Loc::current(), "The via edge has conflicting access point via master constraints! net_idx: ", net_idx,
                  ", below_layer_idx: ", below_layer_idx);
    }
    via_master_idx = required_via_master_idx;
  }
  return true;
}

void DetailedRouter::resetPathHead(DRBox& dr_box)
{
  dr_box.set_path_head_node(popFromOpenList(dr_box));
}

void DetailedRouter::updatePathResult(DRBox& dr_box)
{
  for (Segment<LayerCoord>& routing_segment : getRoutingSegmentListByNode(dr_box.get_path_head_node())) {
    dr_box.get_routing_segment_list().push_back(routing_segment);
  }
}

std::vector<Segment<LayerCoord>> DetailedRouter::getRoutingSegmentListByNode(DRNode* node)
{
  std::vector<Segment<LayerCoord>> routing_segment_list;

  DRNode* curr_node = node;
  DRNode* pre_node = curr_node->get_parent_node();

  if (pre_node == nullptr) {
    // 起点和终点重合
    return routing_segment_list;
  }
  DRNode* planar_first_node = nullptr;
  DRNode* planar_second_node = nullptr;
  Orientation planar_orientation = Orientation::kNone;
  while (pre_node != nullptr) {
    Orientation curr_orientation = RTUTIL.getOrientation(*curr_node, *pre_node);
    if (curr_node->get_layer_idx() != pre_node->get_layer_idx()) {
      if (planar_first_node != nullptr) {
        routing_segment_list.emplace_back(*planar_first_node, *planar_second_node);
        planar_first_node = nullptr;
        planar_second_node = nullptr;
        planar_orientation = Orientation::kNone;
      }
      routing_segment_list.emplace_back(*curr_node, *pre_node, curr_node->get_parent_via_master_idx());
    } else if (planar_first_node != nullptr && curr_orientation == planar_orientation) {
      planar_second_node = pre_node;
    } else {
      if (planar_first_node != nullptr) {
        routing_segment_list.emplace_back(*planar_first_node, *planar_second_node);
      }
      planar_first_node = curr_node;
      planar_second_node = pre_node;
      planar_orientation = curr_orientation;
    }
    curr_node = pre_node;
    pre_node = pre_node->get_parent_node();
  }
  if (planar_first_node != nullptr) {
    routing_segment_list.emplace_back(*planar_first_node, *planar_second_node);
  }

  return routing_segment_list;
}

void DetailedRouter::updateDirectionSet(DRBox& dr_box)
{
  DRNode* path_head_node = dr_box.get_path_head_node();

  DRNode* curr_node = path_head_node;
  DRNode* pre_node = curr_node->get_parent_node();
  while (pre_node != nullptr) {
    curr_node->addDirection(RTUTIL.getDirection(*curr_node, *pre_node));
    pre_node->addDirection(RTUTIL.getDirection(*pre_node, *curr_node));
    curr_node = pre_node;
    pre_node = curr_node->get_parent_node();
  }
}

void DetailedRouter::resetStartAndEnd(DRBox& dr_box)
{
  std::vector<std::vector<DRNode*>>& start_node_list_list = dr_box.get_start_node_list_list();
  std::vector<std::vector<DRNode*>>& end_node_list_list = dr_box.get_end_node_list_list();
  std::vector<DRNode*>& path_node_list = dr_box.get_path_node_list();
  DRNode* path_head_node = dr_box.get_path_head_node();
  int32_t end_node_list_idx = dr_box.get_end_node_list_idx();

  // 对于抵达的终点pin,只保留到达的node
  end_node_list_list[end_node_list_idx].clear();
  end_node_list_list[end_node_list_idx].push_back(path_head_node);

  DRNode* path_node = path_head_node->get_parent_node();
  if (path_node == nullptr) {
    // 起点和终点重合
    path_node = path_head_node;
  } else {
    // 起点和终点不重合
    while (path_node->get_parent_node() != nullptr) {
      path_node_list.push_back(path_node);
      path_node = path_node->get_parent_node();
    }
  }
  if (start_node_list_list.size() == 1) {
    start_node_list_list.front().clear();
    start_node_list_list.front().push_back(path_node);
  }
  start_node_list_list.push_back(end_node_list_list[end_node_list_idx]);
  end_node_list_list.erase(end_node_list_list.begin() + end_node_list_idx);
}

void DetailedRouter::resetSinglePath(DRBox& dr_box)
{
  dr_box.get_open_queue().clear();
  std::vector<DRNode*>& single_path_visited_node_list = dr_box.get_single_path_visited_node_list();
  for (DRNode* visited_node : single_path_visited_node_list) {
    visited_node->set_state(DRNodeState::kNone);
    visited_node->set_parent_node(nullptr);
    visited_node->set_parent_via_master_idx(ViaMasterIdx());
    visited_node->set_known_cost(0);
    visited_node->set_estimated_cost(0);
  }
  single_path_visited_node_list.clear();

  dr_box.set_path_head_node(nullptr);
  dr_box.set_end_node_list_idx(-1);
}

void DetailedRouter::updateTaskResult(DRBox& dr_box)
{
  DRTask* dr_task = dr_box.get_curr_route_task();
  int32_t curr_net_idx = dr_task->get_net_idx();
  std::vector<Segment<LayerCoord>>& routing_segment_list = dr_box.get_net_component_result_map()[curr_net_idx][dr_task->get_component_idx()];
  routing_segment_list = getRoutingSegmentList(dr_box);
  // 新结果添加到graph
  for (Segment<LayerCoord>& routing_segment : routing_segment_list) {
    updateRoutedRectToGraph(dr_box, ChangeType::kAdd, curr_net_idx, routing_segment);
    updateRoutedRectToShadow(dr_box, ChangeType::kAdd, curr_net_idx, routing_segment);
  }
}

std::vector<Segment<LayerCoord>> DetailedRouter::getRoutingSegmentList(DRBox& dr_box)
{
  DRTask* curr_route_task = dr_box.get_curr_route_task();

  std::vector<Segment<LayerCoord>> via_segment_list;
  for (Segment<LayerCoord>& routing_segment : dr_box.get_routing_segment_list()) {
    if (routing_segment.get_first().get_planar_coord() == routing_segment.get_second().get_planar_coord()
        && std::abs(routing_segment.get_first().get_layer_idx() - routing_segment.get_second().get_layer_idx()) == 1) {
      int32_t below_layer_idx = std::min(routing_segment.get_first().get_layer_idx(), routing_segment.get_second().get_layer_idx());
      if (routing_segment.hasValidViaMaster() && !isViaMasterIdxValid(routing_segment.get_via_master_idx(), below_layer_idx)) {
        RTLOG.error(Loc::current(), "The routed via has an invalid via master index! below_layer_idx: ", below_layer_idx,
                    ", via_idx: ", routing_segment.get_via_master_idx().get_via_idx());
      }
      via_segment_list.push_back(routing_segment);
    }
  }

  std::vector<LayerCoord> candidate_root_coord_list;
  std::map<LayerCoord, std::set<int32_t>, CmpLayerCoordByXASC> key_coord_pin_map;
  std::vector<DRGroup>& dr_group_list = curr_route_task->get_dr_group_list();
  for (size_t i = 0; i < dr_group_list.size(); i++) {
    for (auto& [coord, _] : dr_group_list[i].get_coord_direction_map()) {
      candidate_root_coord_list.push_back(coord);
      key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
    }
  }
  MTree<LayerCoord> coord_tree = RTUTIL.getTreeByFullFlow(candidate_root_coord_list, dr_box.get_routing_segment_list(), key_coord_pin_map);

  std::vector<Segment<LayerCoord>> routing_segment_list;
  for (Segment<TNode<LayerCoord>*>& coord_segment : RTUTIL.getSegListByTree(coord_tree)) {
    LayerCoord first = coord_segment.get_first()->value();
    LayerCoord second = coord_segment.get_second()->value();
    if (first.get_planar_coord() != second.get_planar_coord() || first.get_layer_idx() == second.get_layer_idx()) {
      routing_segment_list.emplace_back(first, second);
      continue;
    }
    int32_t below_layer_idx = std::min(first.get_layer_idx(), second.get_layer_idx());
    int32_t above_layer_idx = std::max(first.get_layer_idx(), second.get_layer_idx());
    for (int32_t layer_idx = below_layer_idx; layer_idx < above_layer_idx; layer_idx++) {
      Segment<LayerCoord> unit_via_segment(LayerCoord(first.get_planar_coord(), layer_idx), LayerCoord(first.get_planar_coord(), layer_idx + 1));
      for (Segment<LayerCoord>& via_segment : via_segment_list) {
        if (!hasSameUndirectedEndpoints(unit_via_segment, via_segment.get_first(), via_segment.get_second()) || !via_segment.hasValidViaMaster()) {
          continue;
        }
        if (unit_via_segment.hasValidViaMaster() && unit_via_segment.get_via_master_idx() != via_segment.get_via_master_idx()) {
          RTLOG.error(Loc::current(), "The routed tree has conflicting via master indexes! below_layer_idx: ", layer_idx);
        }
        unit_via_segment.set_via_master_idx(via_segment.get_via_master_idx());
      }
      routing_segment_list.push_back(unit_via_segment);
    }
  }
  return routing_segment_list;
}

void DetailedRouter::resetSingleRouteTask(DRBox& dr_box)
{
  dr_box.set_curr_route_task(nullptr);
  dr_box.get_start_node_list_list().clear();
  dr_box.get_end_node_list_list().clear();
  dr_box.get_path_node_list().clear();
  for (DRNode* single_task_visited_node : dr_box.get_single_task_visited_node_list()) {
    single_task_visited_node->clearDirection();
  }
  dr_box.get_single_task_visited_node_list().clear();
  dr_box.get_routing_segment_list().clear();
  dr_box.get_source_node_access_point_map().clear();
}

// manager open list

void DetailedRouter::pushToOpenList(DRBox& dr_box, DRNode* curr_node)
{
  OpenQueue<DRNode>& open_queue = dr_box.get_open_queue();
  std::vector<DRNode*>& single_task_visited_node_list = dr_box.get_single_task_visited_node_list();
  std::vector<DRNode*>& single_path_visited_node_list = dr_box.get_single_path_visited_node_list();

  open_queue.push(curr_node);
  curr_node->set_state(DRNodeState::kOpen);
  single_task_visited_node_list.push_back(curr_node);
  single_path_visited_node_list.push_back(curr_node);
}

DRNode* DetailedRouter::popFromOpenList(DRBox& dr_box)
{
  DRNode* node = dr_box.get_open_queue().pop();
  if (node != nullptr) {
    node->set_state(DRNodeState::kClose);
  }
  return node;
}

// calculate known

double DetailedRouter::getKnownCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node, Orientation orientation)
{
  double cost = 0;
  cost += start_node->get_known_cost();
  cost += getNodeCost(dr_box, start_node, orientation);
  cost += getNodeCost(dr_box, end_node, RTUTIL.getOppositeOrientation(orientation));
  cost += getKnownWireCost(dr_box, start_node, end_node);
  cost += getKnownViaCost(dr_box, start_node, end_node);
  cost += getKnownBendCost(dr_box, start_node, end_node);
  cost += getKnownSelfCost(dr_box, start_node, end_node);
  return cost;
}

double DetailedRouter::getNodeCost(DRBox& dr_box, DRNode* curr_node, Orientation orientation)
{
  double fixed_rect_unit = dr_box.get_dr_iter_param()->get_fixed_rect_unit();
  double routed_rect_unit = dr_box.get_dr_iter_param()->get_routed_rect_unit();
  double violation_unit = dr_box.get_dr_iter_param()->get_violation_unit();

  int32_t net_idx = dr_box.get_curr_route_task()->get_net_idx();

  double cost = 0;
  cost += curr_node->getFixedRectCost(net_idx, orientation, fixed_rect_unit);
  cost += curr_node->getRoutedRectCost(net_idx, orientation, routed_rect_unit);
  if (!dr_box.get_refine_routing()) {
    cost += curr_node->getViolationCost(orientation, violation_unit);
  }
  return cost;
}

double DetailedRouter::getKnownWireCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  double prefer_wire_unit = dr_box.get_dr_iter_param()->get_prefer_wire_unit();
  double non_prefer_wire_unit = dr_box.get_dr_iter_param()->get_non_prefer_wire_unit();

  double wire_cost = 0;
  if (start_node->get_layer_idx() == end_node->get_layer_idx()) {
    wire_cost += RTUTIL.getManhattanDistance(start_node->get_planar_coord(), end_node->get_planar_coord());

    RoutingLayer& routing_layer = routing_layer_list[start_node->get_layer_idx()];
    if (routing_layer.get_prefer_direction() == RTUTIL.getDirection(*start_node, *end_node)) {
      wire_cost *= prefer_wire_unit;
    } else {
      wire_cost *= non_prefer_wire_unit;
    }
  }
  return wire_cost;
}

double DetailedRouter::getKnownViaCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node)
{
  double via_unit = dr_box.get_dr_iter_param()->get_via_unit();
  double via_cost = (via_unit * std::abs(start_node->get_layer_idx() - end_node->get_layer_idx()));
  return via_cost;
}

double DetailedRouter::getKnownBendCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node)
{
  double bend_unit = dr_box.get_dr_iter_param()->get_bend_unit();

  double bend_cost = 0;
  if (start_node->get_layer_idx() != end_node->get_layer_idx()) {
    return bend_cost;
  }
  Direction curr_direction = RTUTIL.getDirection(*start_node, *end_node);
  DRNode* pre_node = start_node->get_parent_node();
  if (pre_node != nullptr) {
    if (pre_node->get_layer_idx() == start_node->get_layer_idx() && RTUTIL.getDirection(*pre_node, *start_node) != curr_direction) {
      bend_cost += bend_unit;
    }
  } else if (start_node->hasDirection() && !start_node->hasDirection(curr_direction)) {
    bend_cost += bend_unit;
  }
  if (end_node->hasDirection() && !end_node->hasDirection(curr_direction)) {
    bend_cost += bend_unit;
  }
  return bend_cost;
}

double DetailedRouter::getKnownSelfCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  double routed_rect_unit = dr_box.get_dr_iter_param()->get_routed_rect_unit();

  bool nonprefer_and_segment_end = false;
  if (start_node->get_layer_idx() == end_node->get_layer_idx()) {
    RoutingLayer& routing_layer = routing_layer_list[start_node->get_layer_idx()];
    if (routing_layer.get_prefer_direction() != RTUTIL.getDirection(*start_node, *end_node)) {
      for (std::vector<DRNode*>& end_node_list : dr_box.get_end_node_list_list()) {
        if (RTUTIL.exist(end_node_list, end_node)) {
          nonprefer_and_segment_end = true;
          break;
        }
      }
      if (!nonprefer_and_segment_end) {
        return 0;
      }
    }
  }
  RoutingLayer& routing_layer = routing_layer_list[start_node->get_layer_idx()];
  int32_t wire_width = routing_layer.get_min_width();
  int32_t target_wire_length = std::max(routing_layer.getPRLSpacing(wire_width), routing_layer.get_notch_spacing()) + wire_width;

  int32_t non_prefer_wire_length = 0;
  {
    DRNode* curr_node = start_node;
    DRNode* pre_node = curr_node->get_parent_node();
    while (pre_node != nullptr) {
      if (pre_node->get_layer_idx() != curr_node->get_layer_idx()) {
        break;
      }
      if (routing_layer.get_prefer_direction() == RTUTIL.getDirection(*pre_node, *curr_node)) {
        break;
      }
      non_prefer_wire_length += RTUTIL.getManhattanDistance(pre_node->get_planar_coord(), curr_node->get_planar_coord());
      curr_node = pre_node;
      pre_node = curr_node->get_parent_node();
    }
    if (nonprefer_and_segment_end) {
      non_prefer_wire_length += RTUTIL.getManhattanDistance(start_node->get_planar_coord(), end_node->get_planar_coord());
    }
  }
  double self_cost = 0;
  if (0 < non_prefer_wire_length && non_prefer_wire_length < target_wire_length) {
    self_cost += routed_rect_unit;
  }
  return self_cost;
}

// calculate estimate

double DetailedRouter::getEstimateCostToEnd(DRBox& dr_box, DRNode* curr_node)
{
  std::vector<std::vector<DRNode*>>& end_node_list_list = dr_box.get_end_node_list_list();

  double estimate_cost = DBL_MAX;
  for (std::vector<DRNode*>& end_node_list : end_node_list_list) {
    for (DRNode* end_node : end_node_list) {
      if (end_node->isClose()) {
        continue;
      }
      estimate_cost = std::min(estimate_cost, getEstimateCost(dr_box, curr_node, end_node));
    }
  }
  return estimate_cost;
}

double DetailedRouter::getEstimateCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node)
{
  double estimate_cost = 0;
  estimate_cost += getEstimateWireCost(dr_box, start_node, end_node);
  estimate_cost += getEstimateViaCost(dr_box, start_node, end_node);
  return estimate_cost;
}

double DetailedRouter::getEstimateWireCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node)
{
  double prefer_wire_unit = dr_box.get_dr_iter_param()->get_prefer_wire_unit();
  double non_prefer_wire_unit = dr_box.get_dr_iter_param()->get_non_prefer_wire_unit();

  double wire_cost = 0;
  wire_cost += RTUTIL.getManhattanDistance(start_node->get_planar_coord(), end_node->get_planar_coord());
  wire_cost *= std::min(prefer_wire_unit, non_prefer_wire_unit);
  return wire_cost;
}

double DetailedRouter::getEstimateViaCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node)
{
  double via_unit = dr_box.get_dr_iter_param()->get_via_unit();
  double via_cost = (via_unit * std::abs(start_node->get_layer_idx() - end_node->get_layer_idx()));
  return via_cost;
}

void DetailedRouter::patchDRTask(DRBox& dr_box, DRTask* dr_task)
{
  initSinglePatchTask(dr_box, dr_task);
  while (searchViolation(dr_box)) {
    addViolationToShadow(dr_box);
    patchSingleViolation(dr_box);
    resetSingleViolation(dr_box);
    clearViolationShadow(dr_box);
  }
  updateTaskPatch(dr_box);
  resetSinglePatchTask(dr_box);
}

void DetailedRouter::initSinglePatchTask(DRBox& dr_box, DRTask* dr_task)
{
  // single task only checks relevant shapes
  dr_box.set_curr_patch_task(dr_task);
  dr_box.get_routing_patch_list().clear();
  std::vector<LayerRect> check_region_list;
  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  int32_t curr_net_idx = dr_task->get_net_idx();
  for (Segment<LayerCoord>& segment : dr_box.get_net_task_detailed_result_map()[curr_net_idx]) {
    for (NetShape& net_shape : RTDM.getNetDetailedShapeList(curr_net_idx, segment)) {
      if (net_shape.get_is_routing()) {
        check_region_list.emplace_back(RTUTIL.getEnlargedRect(net_shape.get_rect(), detection_distance), net_shape.get_layer_idx());
      }
    }
  }
  for (EXTLayerRect* patch : dr_box.get_net_detailed_patch_map()[curr_net_idx]) {
    check_region_list.emplace_back(RTUTIL.getEnlargedRect(patch->get_real_rect(), detection_distance), patch->get_layer_idx());
  }
  for (EXTLayerRect& patch : dr_box.get_net_task_detailed_patch_map()[curr_net_idx]) {
    check_region_list.emplace_back(RTUTIL.getEnlargedRect(patch.get_real_rect(), detection_distance), patch.get_layer_idx());
  }
  dr_box.set_patch_violation_list(getPatchViolationList(dr_box, {ViolationType::kMinimumArea}, check_region_list));
  dr_box.get_tried_fix_violation_set().clear();
}

std::vector<Violation> DetailedRouter::getPatchViolationList(DRBox& dr_box, const std::set<ViolationType>& check_type_set,
                                                             const std::vector<LayerRect>& check_region_list)
{
  std::string top_name = RTUTIL.getString("dr_box_", dr_box.get_dr_box_id().get_x(), "_", dr_box.get_dr_box_id().get_y());
  std::vector<std::pair<EXTLayerRect*, bool>> env_shape_list;
  std::map<int32_t, std::vector<std::pair<EXTLayerRect*, bool>>> net_pin_shape_map;
  for (auto& [is_routing, layer_net_fixed_rect_map] : dr_box.get_type_layer_net_fixed_rect_map()) {
    for (auto& [layer_idx, net_fixed_rect_map] : layer_net_fixed_rect_map) {
      for (auto& [net_idx, fixed_rect_set] : net_fixed_rect_map) {
        if (net_idx == -1) {
          env_shape_list.reserve(env_shape_list.size() + fixed_rect_set.size());
          for (auto& fixed_rect : fixed_rect_set) {
            if (overlapCheckRegion(layer_idx, fixed_rect->get_real_rect(), check_region_list)) {
              env_shape_list.emplace_back(fixed_rect, is_routing);
            }
          }
        } else {
          std::vector<std::pair<EXTLayerRect*, bool>>& pin_shape_list = net_pin_shape_map[net_idx];
          pin_shape_list.reserve(pin_shape_list.size() + fixed_rect_set.size());
          for (auto& fixed_rect : fixed_rect_set) {
            if (overlapCheckRegion(layer_idx, fixed_rect->get_real_rect(), check_region_list)) {
              pin_shape_list.emplace_back(fixed_rect, is_routing);
            }
          }
        }
      }
    }
  }
  std::map<int32_t, std::vector<Segment<LayerCoord>*>> net_result_map;
  for (auto& [net_idx, segment_list] : dr_box.get_net_detailed_result_map()) {
    std::vector<Segment<LayerCoord>*>& result_list = net_result_map[net_idx];
    result_list.reserve(segment_list.size());
    for (Segment<LayerCoord>* segment : segment_list) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, *segment)) {
        if (overlapCheckRegion(net_shape.get_layer_idx(), net_shape.get_rect(), check_region_list)) {
          result_list.push_back(segment);
          break;
        }
      }
    }
  }
  for (auto& [net_idx, segment_list] : dr_box.get_net_task_detailed_result_map()) {
    std::vector<Segment<LayerCoord>*>& result_list = net_result_map[net_idx];
    result_list.reserve(result_list.size() + segment_list.size());
    for (Segment<LayerCoord>& segment : segment_list) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
        if (overlapCheckRegion(net_shape.get_layer_idx(), net_shape.get_rect(), check_region_list)) {
          result_list.emplace_back(&segment);
          break;
        }
      }
    }
  }
  std::map<int32_t, std::vector<EXTLayerRect*>> net_patch_map;
  for (auto& [net_idx, patch_list] : dr_box.get_net_detailed_patch_map()) {
    std::vector<EXTLayerRect*>& result_patch_list = net_patch_map[net_idx];
    result_patch_list.reserve(patch_list.size());
    for (EXTLayerRect* patch : patch_list) {
      if (overlapCheckRegion(patch->get_layer_idx(), patch->get_real_rect(), check_region_list)) {
        result_patch_list.push_back(patch);
      }
    }
  }
  for (auto& [net_idx, patch_list] : dr_box.get_net_task_detailed_patch_map()) {
    std::vector<EXTLayerRect*>& result_patch_list = net_patch_map[net_idx];
    if (net_idx == dr_box.get_curr_patch_task()->get_net_idx()) {
      result_patch_list.reserve(result_patch_list.size() + dr_box.get_routing_patch_list().size());
      for (EXTLayerRect& patch : dr_box.get_routing_patch_list()) {
        if (overlapCheckRegion(patch.get_layer_idx(), patch.get_real_rect(), check_region_list)) {
          result_patch_list.emplace_back(&patch);
        }
      }
    } else {
      result_patch_list.reserve(result_patch_list.size() + patch_list.size());
      for (EXTLayerRect& patch : patch_list) {
        if (overlapCheckRegion(patch.get_layer_idx(), patch.get_real_rect(), check_region_list)) {
          result_patch_list.emplace_back(&patch);
        }
      }
    }
  }
  std::set<int32_t> need_checked_net_set;
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    need_checked_net_set.insert(dr_task->get_net_idx());
  }
  DETask de_task;
  de_task.set_proc_type(DEProcType::kGet);
  de_task.set_net_type(DENetType::kPatchHybrid);
  de_task.set_top_name(top_name);
  de_task.set_env_shape_list(std::move(env_shape_list));
  de_task.set_net_pin_shape_map(std::move(net_pin_shape_map));
  de_task.set_net_result_map(std::move(net_result_map));
  de_task.set_net_patch_map(std::move(net_patch_map));
  de_task.set_need_checked_net_set(need_checked_net_set);
  de_task.set_check_type_set(check_type_set);
  de_task.set_check_region_list(check_region_list);
  return RTDE.getViolationList(de_task);
}

bool DetailedRouter::searchViolation(DRBox& dr_box)
{
  for (Violation& violation : dr_box.get_patch_violation_list()) {
    if (!isValidPatchViolation(dr_box, violation)) {
      continue;
    }
    if (RTUTIL.exist(dr_box.get_tried_fix_violation_set(), violation)) {
      continue;
    }
    int32_t net_idx = *violation.get_violation_net_set().begin();
    if (dr_box.get_curr_patch_task()->get_net_idx() != net_idx) {
      continue;
    }
    if (getViolationOverlapRect(dr_box, violation).empty()) {
      continue;
    }
    dr_box.set_curr_patch_violation(violation);
    return true;
  }
  return false;
}

bool DetailedRouter::isValidPatchViolation(DRBox& dr_box, Violation& violation)
{
  PlanarRect& box_real_rect = dr_box.get_box_rect().get_real_rect();

  bool is_valid = true;
  if (!RTUTIL.isOpenOverlap(box_real_rect, violation.get_violation_shape().get_real_rect())) {
    is_valid = false;
  }
  if (violation.get_violation_type() != ViolationType::kMinimumArea) {
    is_valid = false;
  }
  return is_valid;
}

std::vector<PlanarRect> DetailedRouter::getViolationOverlapRect(DRBox& dr_box, Violation& violation)
{
  int32_t curr_net_idx = dr_box.get_curr_patch_task()->get_net_idx();
  EXTLayerRect& violation_shape = violation.get_violation_shape();
  PlanarRect violation_real_rect = violation_shape.get_real_rect();
  int32_t violation_layer_idx = violation_shape.get_layer_idx();

  GTLPolySetInt gtl_poly_set;
  {
    for (EXTLayerRect* fixed_rect : dr_box.get_type_layer_net_fixed_rect_map()[true][violation_layer_idx][curr_net_idx]) {
      if (RTUTIL.isClosedOverlap(violation_real_rect, fixed_rect->get_real_rect())) {
        gtl_poly_set += RTUTIL.convertToGTLRectInt(fixed_rect->get_real_rect());
      }
    }
    for (Segment<LayerCoord>* segment : dr_box.get_net_detailed_result_map()[curr_net_idx]) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(curr_net_idx, *segment)) {
        if (!net_shape.get_is_routing()) {
          continue;
        }
        if (violation_layer_idx == net_shape.get_layer_idx() && RTUTIL.isClosedOverlap(violation_real_rect, net_shape.get_rect())) {
          gtl_poly_set += RTUTIL.convertToGTLRectInt(net_shape.get_rect());
        }
      }
    }
    for (Segment<LayerCoord>& segment : dr_box.get_net_task_detailed_result_map()[curr_net_idx]) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(curr_net_idx, segment)) {
        if (!net_shape.get_is_routing()) {
          continue;
        }
        if (violation_layer_idx == net_shape.get_layer_idx() && RTUTIL.isClosedOverlap(violation_real_rect, net_shape.get_rect())) {
          gtl_poly_set += RTUTIL.convertToGTLRectInt(net_shape.get_rect());
        }
      }
    }
    for (EXTLayerRect* patch : dr_box.get_net_detailed_patch_map()[curr_net_idx]) {
      if (violation_layer_idx == patch->get_layer_idx() && RTUTIL.isClosedOverlap(violation_real_rect, patch->get_real_rect())) {
        gtl_poly_set += RTUTIL.convertToGTLRectInt(patch->get_real_rect());
      }
    }
  }
  std::vector<GTLPolyInt> gtl_poly_list;
  gtl_poly_set.get_polygons(gtl_poly_list);
  if (gtl_poly_list.empty()) {
    return {};
  }
  GTLPolyInt best_gtl_poly = gtl_poly_list.front();
  {
    int32_t max_overlap_area = INT32_MIN;
    for (GTLPolyInt& gtl_poly : gtl_poly_list) {
      int32_t overlap_area = static_cast<int32_t>(gtl::area(gtl_poly & RTUTIL.convertToGTLRectInt(violation_real_rect)));
      if (max_overlap_area < overlap_area) {
        max_overlap_area = overlap_area;
        best_gtl_poly = gtl_poly;
      }
    }
  }
  std::vector<GTLRectInt> gtl_rect_list;
  gtl::get_max_rectangles(gtl_rect_list, best_gtl_poly);
  std::vector<PlanarRect> overlap_rect_list;
  for (GTLRectInt& gtl_rect : gtl_rect_list) {
    overlap_rect_list.push_back(RTUTIL.convertToPlanarRect(gtl_rect));
  }
  return overlap_rect_list;
}

void DetailedRouter::addViolationToShadow(DRBox& dr_box)
{
  for (Violation& patch_violation : dr_box.get_patch_violation_list()) {
    if (patch_violation.get_violation_type() == ViolationType::kMinimumArea) {
      continue;
    }
    addPatchViolationToShadow(dr_box, patch_violation);
  }
}

void DetailedRouter::patchSingleViolation(DRBox& dr_box)
{
  std::vector<EXTLayerRect>& routing_patch_list = dr_box.get_routing_patch_list();
  std::set<Violation, CmpViolation>& tried_fix_violation_set = dr_box.get_tried_fix_violation_set();
  LayerRect violation_rect = dr_box.get_curr_patch_violation().get_violation_shape().getRealLayerRect();
  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  LayerRect check_region(RTUTIL.getEnlargedRect(violation_rect.get_rect(), detection_distance), violation_rect.get_layer_idx());

  int32_t max_candidate_patch_num = dr_box.get_dr_iter_param()->get_max_candidate_patch_num();
  std::vector<int32_t> raw_candidate_limit_list = {max_candidate_patch_num, 4 * max_candidate_patch_num, 8 * max_candidate_patch_num, -1};
  std::vector<Violation> origin_patch_violation_list;
  bool curr_is_solved = false;
  bool origin_patch_violation_inited = false;
  for (int32_t raw_candidate_limit : raw_candidate_limit_list) {
    std::vector<DRPatch> dr_patch_list = getCandidatePatchList(dr_box, raw_candidate_limit);
    if (dr_patch_list.empty()) {
      continue;
    }
    if (!origin_patch_violation_inited) {
      origin_patch_violation_list = getPatchViolationList(dr_box, {}, {check_region});
      origin_patch_violation_inited = true;
    }
    for (DRPatch& dr_patch : dr_patch_list) {
      std::vector<Violation> curr_patch_violation_list;
      {
        routing_patch_list.push_back(dr_patch.get_patch());
        curr_patch_violation_list = getPatchViolationList(dr_box, {}, {check_region});
        routing_patch_list.pop_back();
      }
      curr_is_solved = getSolvedStatus(dr_box, origin_patch_violation_list, curr_patch_violation_list);
      if (curr_is_solved) {
        routing_patch_list.push_back(dr_patch.get_patch());
        break;
      }
    }
    if (curr_is_solved) {
      break;
    }
  }
  tried_fix_violation_set.insert(dr_box.get_curr_patch_violation());
}

namespace {

bool enlargePatchToMinArea(const GTLPolyInt& gtl_poly, int32_t min_area, int32_t manufacture_grid, const PlanarRect& die_rect,
                           Orientation orientation, PlanarRect& patch_rect)
{
  int64_t poly_area = static_cast<int64_t>(gtl::area(gtl_poly));
  while (true) {
    int64_t patch_area = static_cast<int64_t>(patch_rect.getXSpan()) * patch_rect.getYSpan();
    int64_t overlap_area = static_cast<int64_t>(gtl::area(gtl_poly & RTUTIL.convertToGTLRectInt(patch_rect)));
    int64_t curr_area = poly_area + patch_area - overlap_area;
    if (min_area <= curr_area) {
      return RTUTIL.isInside(die_rect, patch_rect);
    }

    int32_t cross_span = 0;
    if (orientation == Orientation::kEast || orientation == Orientation::kWest) {
      cross_span = patch_rect.getYSpan();
    } else if (orientation == Orientation::kNorth || orientation == Orientation::kSouth) {
      cross_span = patch_rect.getXSpan();
    } else {
      RTLOG.error(Loc::current(), "The patch orientation is invalid!");
    }
    int64_t missing_area = min_area - curr_area;
    int32_t grow_span = static_cast<int32_t>((missing_area + cross_span - 1) / cross_span);
    while (grow_span % manufacture_grid != 0) {
      grow_span++;
    }
    if (orientation == Orientation::kEast) {
      patch_rect.set_ur_x(patch_rect.get_ur_x() + grow_span);
    } else if (orientation == Orientation::kWest) {
      patch_rect.set_ll_x(patch_rect.get_ll_x() - grow_span);
    } else if (orientation == Orientation::kNorth) {
      patch_rect.set_ur_y(patch_rect.get_ur_y() + grow_span);
    } else if (orientation == Orientation::kSouth) {
      patch_rect.set_ll_y(patch_rect.get_ll_y() - grow_span);
    }
    if (!RTUTIL.isInside(die_rect, patch_rect)) {
      return false;
    }
  }
}

}  // namespace

std::vector<DRPatch> DetailedRouter::getCandidatePatchList(DRBox& dr_box, int32_t raw_candidate_limit)
{
  int32_t manufacture_grid = RTDM.getDatabase().get_manufacture_grid();
  Die& die = RTDM.getDatabase().get_die();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  int32_t max_candidate_patch_num = dr_box.get_dr_iter_param()->get_max_candidate_patch_num();

  int32_t curr_net_idx = dr_box.get_curr_patch_task()->get_net_idx();
  Violation& curr_patch_violation = dr_box.get_curr_patch_violation();
  int32_t violation_layer_idx = curr_patch_violation.get_violation_shape().get_layer_idx();

  RoutingLayer& routing_layer = routing_layer_list[violation_layer_idx];
  Direction layer_direction = routing_layer.get_prefer_direction();
  int32_t min_area = routing_layer.get_min_area();
  int32_t wire_width = routing_layer.get_min_width();

  GTLPolyInt gtl_poly;
  {
    GTLPolySetInt gtl_poly_set;
    for (PlanarRect& overlap_rect : getViolationOverlapRect(dr_box, curr_patch_violation)) {
      gtl_poly_set += RTUTIL.convertToGTLRectInt(overlap_rect);
    }
    std::vector<GTLPolyInt> gtl_poly_list;
    gtl_poly_set.get_polygons(gtl_poly_list);
    gtl_poly = gtl_poly_list.front();
    if (min_area <= static_cast<int32_t>(gtl::area(gtl_poly))) {
      return {};
    }
  }
  std::vector<GTLRectInt> h_gtl_rect_list;
  PlanarRect h_cutting_rect;
  {
    gtl::get_rectangles(h_gtl_rect_list, gtl_poly, gtl::HORIZONTAL);
    GTLRectInt best_gtl_rect;
    int32_t max_x_span = 0;
    for (GTLRectInt& gtl_rect : h_gtl_rect_list) {
      int32_t curr_x_span = std::abs(gtl::xl(gtl_rect) - gtl::xh(gtl_rect));
      if (max_x_span <= curr_x_span) {
        max_x_span = curr_x_span;
        best_gtl_rect = gtl_rect;
      }
    }
    h_cutting_rect = RTUTIL.convertToPlanarRect(best_gtl_rect);
  }
  PlanarRect v_cutting_rect;
  {
    std::vector<GTLRectInt> gtl_rect_list;
    gtl::get_rectangles(gtl_rect_list, gtl_poly, gtl::VERTICAL);
    GTLRectInt best_gtl_rect;
    int32_t max_y_span = 0;
    for (GTLRectInt& gtl_rect : gtl_rect_list) {
      int32_t curr_y_span = std::abs(gtl::yl(gtl_rect) - gtl::yh(gtl_rect));
      if (max_y_span <= curr_y_span) {
        max_y_span = curr_y_span;
        best_gtl_rect = gtl_rect;
      }
    }
    v_cutting_rect = RTUTIL.convertToPlanarRect(best_gtl_rect);
  }
  std::vector<DRPatch> dr_patch_list;
  {
    int32_t h_wire_length = static_cast<int32_t>(std::ceil((min_area - v_cutting_rect.getArea()) / wire_width) + v_cutting_rect.getXSpan());
    while (h_wire_length % manufacture_grid != 0) {
      h_wire_length++;
    }
    int32_t v_wire_length = static_cast<int32_t>(std::ceil((min_area - h_cutting_rect.getArea()) / wire_width) + h_cutting_rect.getYSpan());
    while (v_wire_length % manufacture_grid != 0) {
      v_wire_length++;
    }
    for (int32_t y : {h_cutting_rect.get_ll_y(), v_cutting_rect.get_ll_y(), v_cutting_rect.get_ur_y() - wire_width}) {
      for (int32_t x = v_cutting_rect.get_ur_x() - h_wire_length; x <= v_cutting_rect.get_ll_x(); x += manufacture_grid) {
        PlanarRect h_real_rect = RTUTIL.getEnlargedRect(PlanarCoord(x, y), 0, 0, h_wire_length, wire_width);
        if (RTUTIL.isInside(die.get_real_rect(), h_real_rect)) {
          dr_patch_list.emplace_back(h_real_rect, violation_layer_idx);
        }
      }
    }
    for (int32_t x : {v_cutting_rect.get_ll_x(), h_cutting_rect.get_ll_x(), h_cutting_rect.get_ur_x() - wire_width}) {
      for (int32_t y = h_cutting_rect.get_ur_y() - v_wire_length; y <= h_cutting_rect.get_ll_y(); y += manufacture_grid) {
        PlanarRect v_real_rect = RTUTIL.getEnlargedRect(PlanarCoord(x, y), 0, 0, wire_width, v_wire_length);
        if (RTUTIL.isInside(die.get_real_rect(), v_real_rect)) {
          dr_patch_list.emplace_back(v_real_rect, violation_layer_idx);
        }
      }
    }
    if (raw_candidate_limit == -1) {
      int32_t compact_span = 2 * wire_width;
      int32_t h_start_x = h_cutting_rect.get_ll_x() - compact_span + manufacture_grid;
      int32_t h_end_x = h_cutting_rect.get_ur_x() - manufacture_grid;
      for (int32_t x = h_start_x; x <= h_end_x; x += manufacture_grid) {
        PlanarRect north_rect(x, h_cutting_rect.get_ur_y() - wire_width, x + compact_span, h_cutting_rect.get_ur_y());
        if (enlargePatchToMinArea(gtl_poly, min_area, manufacture_grid, die.get_real_rect(), Orientation::kNorth, north_rect)) {
          dr_patch_list.emplace_back(north_rect, violation_layer_idx);
        }
        PlanarRect south_rect(x, h_cutting_rect.get_ll_y(), x + compact_span, h_cutting_rect.get_ll_y() + wire_width);
        if (enlargePatchToMinArea(gtl_poly, min_area, manufacture_grid, die.get_real_rect(), Orientation::kSouth, south_rect)) {
          dr_patch_list.emplace_back(south_rect, violation_layer_idx);
        }
      }
      int32_t v_start_y = v_cutting_rect.get_ll_y() - compact_span + manufacture_grid;
      int32_t v_end_y = v_cutting_rect.get_ur_y() - manufacture_grid;
      for (int32_t y = v_start_y; y <= v_end_y; y += manufacture_grid) {
        PlanarRect east_rect(v_cutting_rect.get_ur_x() - wire_width, y, v_cutting_rect.get_ur_x(), y + compact_span);
        if (enlargePatchToMinArea(gtl_poly, min_area, manufacture_grid, die.get_real_rect(), Orientation::kEast, east_rect)) {
          dr_patch_list.emplace_back(east_rect, violation_layer_idx);
        }
        PlanarRect west_rect(v_cutting_rect.get_ll_x(), y, v_cutting_rect.get_ll_x() + wire_width, y + compact_span);
        if (enlargePatchToMinArea(gtl_poly, min_area, manufacture_grid, die.get_real_rect(), Orientation::kWest, west_rect)) {
          dr_patch_list.emplace_back(west_rect, violation_layer_idx);
        }
      }
    }
    if (0 < raw_candidate_limit && raw_candidate_limit < static_cast<int32_t>(dr_patch_list.size())) {
      std::vector<DRPatch> sampled_patch_list;
      sampled_patch_list.reserve(raw_candidate_limit);
      if (raw_candidate_limit == 1) {
        sampled_patch_list.push_back(dr_patch_list.front());
      } else {
        int32_t patch_size = static_cast<int32_t>(dr_patch_list.size());
        for (int32_t i = 0; i < raw_candidate_limit; i++) {
          int32_t sampled_idx = (i * (patch_size - 1)) / (raw_candidate_limit - 1);
          sampled_patch_list.push_back(dr_patch_list[sampled_idx]);
        }
      }
      dr_patch_list = std::move(sampled_patch_list);
    }
    for (DRPatch& dr_patch : dr_patch_list) {
      EXTLayerRect& patch = dr_patch.get_patch();
      PlanarRect& patch_rect = patch.get_real_rect();
      patch.set_grid_rect(RTUTIL.getClosedGCellGridRect(patch_rect, gcell_axis));
      dr_patch.set_fixed_rect_cost(getFixedRectCost(dr_box, curr_net_idx, patch));
      dr_patch.set_routed_rect_cost(getRoutedRectCost(dr_box, curr_net_idx, patch));
      dr_patch.set_violation_cost(getViolationCost(dr_box, curr_net_idx, patch));
      dr_patch.set_direction(patch_rect.getRectDirection(layer_direction));
      int64_t overlap_area = 0;
      for (GTLRectInt& gtl_rect : h_gtl_rect_list) {
        int32_t x_span = std::min(gtl::xh(gtl_rect), patch_rect.get_ur_x()) - std::max(gtl::xl(gtl_rect), patch_rect.get_ll_x());
        int32_t y_span = std::min(gtl::yh(gtl_rect), patch_rect.get_ur_y()) - std::max(gtl::yl(gtl_rect), patch_rect.get_ll_y());
        if (x_span > 0 && y_span > 0) {
          overlap_area += static_cast<int64_t>(x_span) * y_span;
        }
      }
      dr_patch.set_overlap_area(static_cast<int32_t>(overlap_area));
    }
    if (dr_patch_list.empty()) {
      RTLOG.error(Loc::current(), "The dr_patch_list is empty!");
    }
  }
  std::vector<DRPatch> candidate_patch_list;
  {
    auto cmp_dr_patch = [&layer_direction](DRPatch& a, DRPatch& b) { return CmpDRPatch()(a, b, layer_direction); };
    if (raw_candidate_limit == -1) {
      std::ranges::sort(dr_patch_list, cmp_dr_patch);
      return dr_patch_list;
    }
    std::vector<DRPatch> dr_patch_list_temp;
    for (DRPatch& dr_patch : dr_patch_list) {
      if (dr_patch.getTotalCost() > 0) {
        continue;
      }
      dr_patch_list_temp.push_back(dr_patch);
    }
    if (dr_patch_list_temp.empty()) {
      dr_patch_list_temp.push_back(*std::ranges::min_element(dr_patch_list, cmp_dr_patch));
    } else {
      std::ranges::sort(dr_patch_list_temp, cmp_dr_patch);
    }
    int32_t patch_size = static_cast<int32_t>(dr_patch_list_temp.size());
    if (patch_size <= max_candidate_patch_num) {
      candidate_patch_list = dr_patch_list_temp;
    } else {
      int32_t candidate_step = (patch_size - 2) / (max_candidate_patch_num - 2);
      candidate_patch_list.push_back(dr_patch_list_temp.front());
      for (int32_t i = candidate_step; i < (patch_size - candidate_step); i += candidate_step) {
        candidate_patch_list.push_back(dr_patch_list_temp[i]);
      }
      candidate_patch_list.push_back(dr_patch_list_temp.back());
    }
  }
  return candidate_patch_list;
}

bool DetailedRouter::getSolvedStatus(DRBox& dr_box, std::vector<Violation>& origin_patch_violation_list,
                                     std::vector<Violation>& curr_patch_violation_list)
{
  if (curr_patch_violation_list.empty()) {
    return true;
  }
  std::map<ViolationType, std::pair<int32_t, int32_t>> valid_type_origin_curr_map;
  std::map<ViolationType, std::pair<int32_t, int32_t>> within_net_map;
  std::set<Violation, CmpViolation> origin_env_violation_set;
  std::set<Violation, CmpViolation> curr_env_violation_set;
  for (Violation& origin_violation : origin_patch_violation_list) {
    if (!isValidPatchViolation(dr_box, origin_violation)) {
      origin_env_violation_set.insert(origin_violation);
    } else {
      valid_type_origin_curr_map[origin_violation.get_violation_type()].first++;
    }
    if (origin_violation.get_violation_net_set().size() > 1) {
      within_net_map[origin_violation.get_violation_type()].first++;
    }
  }
  for (Violation& curr_violation : curr_patch_violation_list) {
    if (!isValidPatchViolation(dr_box, curr_violation)) {
      curr_env_violation_set.insert(curr_violation);
    } else {
      valid_type_origin_curr_map[curr_violation.get_violation_type()].second++;
    }
    if (curr_violation.get_violation_net_set().size() > 1) {
      within_net_map[curr_violation.get_violation_type()].second++;
    }
  }
  bool curr_is_solved = std::includes(origin_env_violation_set.begin(), origin_env_violation_set.end(), curr_env_violation_set.begin(),
                                      curr_env_violation_set.end(), CmpViolation());
  for (auto& [violation_type, origin_curr] : valid_type_origin_curr_map) {
    if (!curr_is_solved) {
      break;
    }
    curr_is_solved = origin_curr.second < origin_curr.first;
  }
  for (auto& [violation_type, origin_curr] : within_net_map) {
    if (!curr_is_solved) {
      break;
    }
    curr_is_solved = origin_curr.second <= origin_curr.first;
  }
  return curr_is_solved;
}

void DetailedRouter::resetSingleViolation(DRBox& dr_box)
{
  dr_box.set_curr_patch_violation(Violation());
}

void DetailedRouter::clearViolationShadow(DRBox& dr_box)
{
  for (DRShadow& dr_shadow : dr_box.get_layer_shadow_map()) {
    dr_shadow.get_violation_set().clear();
  }
}

void DetailedRouter::updateTaskPatch(DRBox& dr_box)
{
  int32_t curr_net_idx = dr_box.get_curr_patch_task()->get_net_idx();
  std::vector<EXTLayerRect>& routing_patch_list = dr_box.get_net_task_detailed_patch_map()[curr_net_idx];
  routing_patch_list.insert(routing_patch_list.end(), dr_box.get_routing_patch_list().begin(), dr_box.get_routing_patch_list().end());
  // 新结果添加到graph
  for (EXTLayerRect& routing_patch : routing_patch_list) {
    updateRoutedRectToGraph(dr_box, ChangeType::kAdd, curr_net_idx, routing_patch, true);
    updateRoutedRectToShadow(dr_box, ChangeType::kAdd, curr_net_idx, routing_patch, true);
  }
}

void DetailedRouter::resetSinglePatchTask(DRBox& dr_box)
{
  dr_box.set_curr_patch_task(nullptr);
  dr_box.get_routing_patch_list().clear();
  dr_box.get_patch_violation_list().clear();
  dr_box.get_tried_fix_violation_set().clear();
}

void DetailedRouter::updateRouteViolationList(DRBox& dr_box)
{
  dr_box.get_route_violation_list().clear();
  for (Violation new_violation : getRouteViolationList(dr_box)) {
    PlanarRect& box_rect = dr_box.get_box_rect().get_real_rect();
    PlanarRect violation_rect = RTUTIL.getEnlargedRect(new_violation.get_violation_shape().get_real_rect(), RTDM.getOnlyPitch());
    if (RTUTIL.isOpenOverlap(box_rect, violation_rect)) {
      dr_box.get_route_violation_list().push_back(new_violation);
    }
  }
  // 新结果添加到graph
  for (Violation& violation : dr_box.get_route_violation_list()) {
    addRouteViolationToGraph(dr_box, violation);
  }
}

std::vector<Violation> DetailedRouter::getRouteViolationList(DRBox& dr_box)
{
  std::string top_name = RTUTIL.getString("dr_box_", dr_box.get_dr_box_id().get_x(), "_", dr_box.get_dr_box_id().get_y());
  std::vector<std::pair<EXTLayerRect*, bool>> env_shape_list;
  std::map<int32_t, std::vector<std::pair<EXTLayerRect*, bool>>> net_pin_shape_map;
  for (auto& [is_routing, layer_net_fixed_rect_map] : dr_box.get_type_layer_net_fixed_rect_map()) {
    for (auto& [layer_idx, net_fixed_rect_map] : layer_net_fixed_rect_map) {
      for (auto& [net_idx, fixed_rect_set] : net_fixed_rect_map) {
        if (net_idx == -1) {
          for (auto& fixed_rect : fixed_rect_set) {
            env_shape_list.emplace_back(fixed_rect, is_routing);
          }
        } else {
          for (auto& fixed_rect : fixed_rect_set) {
            net_pin_shape_map[net_idx].emplace_back(fixed_rect, is_routing);
          }
        }
      }
    }
  }
  std::map<int32_t, std::vector<Segment<LayerCoord>*>> net_result_map;
  for (auto& [net_idx, segment_list] : dr_box.get_net_detailed_result_map()) {
    for (Segment<LayerCoord>* segment : segment_list) {
      net_result_map[net_idx].push_back(segment);
    }
  }
  for (auto& [net_idx, segment_list] : dr_box.get_net_task_detailed_result_map()) {
    for (Segment<LayerCoord>& segment : segment_list) {
      net_result_map[net_idx].emplace_back(&segment);
    }
  }
  std::map<int32_t, std::vector<EXTLayerRect*>> net_patch_map;
  for (auto& [net_idx, patch_list] : dr_box.get_net_detailed_patch_map()) {
    for (EXTLayerRect* patch : patch_list) {
      net_patch_map[net_idx].push_back(patch);
    }
  }
  for (auto& [net_idx, patch_list] : dr_box.get_net_task_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      net_patch_map[net_idx].emplace_back(&patch);
    }
  }
  std::set<int32_t> need_checked_net_set;
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    need_checked_net_set.insert(dr_task->get_net_idx());
  }

  DETask de_task;
  de_task.set_proc_type(DEProcType::kGet);
  de_task.set_net_type(DENetType::kRouteHybrid);
  de_task.set_top_name(top_name);
  de_task.set_env_shape_list(env_shape_list);
  de_task.set_net_pin_shape_map(net_pin_shape_map);
  de_task.set_net_result_map(net_result_map);
  de_task.set_net_patch_map(net_patch_map);
  de_task.set_need_checked_net_set(need_checked_net_set);
  return RTDE.getViolationList(de_task);
}

void DetailedRouter::updateBestResult(DRBox& dr_box)
{
  std::map<int32_t, std::vector<Segment<LayerCoord>>>& best_net_task_detailed_result_map = dr_box.get_best_net_task_detailed_result_map();
  std::map<int32_t, std::vector<EXTLayerRect>>& best_net_task_detailed_patch_map = dr_box.get_best_net_task_detailed_patch_map();
  std::vector<Violation>& best_route_violation_list = dr_box.get_best_route_violation_list();

  int32_t curr_violation_num = static_cast<int32_t>(dr_box.get_route_violation_list().size());
  if (!best_net_task_detailed_result_map.empty()) {
    if (static_cast<int32_t>(best_route_violation_list.size()) < curr_violation_num) {
      return;
    }
  }
  best_net_task_detailed_result_map = dr_box.get_net_task_detailed_result_map();
  best_net_task_detailed_patch_map = dr_box.get_net_task_detailed_patch_map();
  best_route_violation_list = dr_box.get_route_violation_list();
}

void DetailedRouter::updateTaskSchedule(DRBox& dr_box, std::vector<int32_t>& routing_net_list)
{
  int32_t max_routed_times = dr_box.get_dr_iter_param()->get_max_routed_times();
  std::vector<int32_t> task_net_list;
  std::set<int32_t> visited_task_net_set;
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    if (!RTUTIL.exist(visited_task_net_set, dr_task->get_net_idx())) {
      visited_task_net_set.insert(dr_task->get_net_idx());
      task_net_list.push_back(dr_task->get_net_idx());
    }
  }
  std::set<int32_t> routing_net_set;
  routing_net_list.clear();
  for (Violation& violation : dr_box.get_route_violation_list()) {
    EXTLayerRect& violation_shape = violation.get_violation_shape();
    if (!RTUTIL.isOpenOverlap(dr_box.get_box_rect().get_real_rect(), RTUTIL.getEnlargedRect(violation_shape.get_real_rect(), RTDM.getOnlyPitch()))) {
      continue;
    }
    for (int32_t net_idx : task_net_list) {
      if (!RTUTIL.exist(violation.get_violation_net_set(), net_idx) || RTUTIL.exist(routing_net_set, net_idx)) {
        continue;
      }
      if (dr_box.get_net_routed_times_map()[net_idx] < max_routed_times) {
        routing_net_set.insert(net_idx);
        routing_net_list.push_back(net_idx);
      }
    }
  }

  std::vector<DRTask*> new_task_list;
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    if (!RTUTIL.exist(routing_net_set, dr_task->get_net_idx())) {
      new_task_list.push_back(dr_task);
    }
  }
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    if (RTUTIL.exist(routing_net_set, dr_task->get_net_idx())) {
      new_task_list.push_back(dr_task);
    }
  }
  dr_box.set_dr_task_list(new_task_list);
}

void DetailedRouter::selectBestResult(DRBox& dr_box)
{
  updateBestResult(dr_box);
  dr_box.get_net_task_detailed_result_map() = std::move(dr_box.get_best_net_task_detailed_result_map());
  dr_box.get_net_task_detailed_patch_map() = std::move(dr_box.get_best_net_task_detailed_patch_map());
  dr_box.get_route_violation_list() = std::move(dr_box.get_best_route_violation_list());
}

void DetailedRouter::freeDRBox(DRBox& dr_box)
{
  dr_box.get_open_queue().release();
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    delete dr_task;
    dr_task = nullptr;
  }
  std::vector<DRTask*>().swap(dr_box.get_dr_task_list());

  dr_box.get_type_layer_net_fixed_rect_map().clear();
  dr_box.get_net_access_point_map().clear();
  dr_box.get_net_detailed_result_map().clear();
  dr_box.get_net_detailed_patch_map().clear();
  dr_box.get_net_component_result_map().clear();
  dr_box.get_net_routed_times_map().clear();
  dr_box.set_refine_routing(false);
  dr_box.get_refine_net_list().clear();
  std::vector<Violation>().swap(dr_box.get_route_violation_list());
  std::vector<ScaleGrid>().swap(dr_box.get_box_track_axis().get_x_grid_list());
  std::vector<ScaleGrid>().swap(dr_box.get_box_track_axis().get_y_grid_list());
  dr_box.get_layer_node_map().clear();
  dr_box.get_layer_shadow_map().clear();
  dr_box.get_layer_axis_map().clear();
  dr_box.get_best_net_task_detailed_result_map().clear();
  dr_box.get_best_net_task_detailed_patch_map().clear();
  std::vector<Violation>().swap(dr_box.get_best_route_violation_list());

  dr_box.set_curr_route_task(nullptr);
  dr_box.get_start_node_list_list().clear();
  dr_box.get_end_node_list_list().clear();
  std::vector<DRNode*>().swap(dr_box.get_path_node_list());
  std::vector<DRNode*>().swap(dr_box.get_single_task_visited_node_list());
  std::vector<Segment<LayerCoord>>().swap(dr_box.get_routing_segment_list());
  dr_box.get_source_node_access_point_map().clear();
  std::vector<DRNode*>().swap(dr_box.get_single_path_visited_node_list());
  dr_box.set_path_head_node(nullptr);
  dr_box.set_end_node_list_idx(-1);

  dr_box.set_curr_patch_task(nullptr);
  std::vector<EXTLayerRect>().swap(dr_box.get_routing_patch_list());
  std::vector<Violation>().swap(dr_box.get_patch_violation_list());
  dr_box.get_tried_fix_violation_set().clear();
}

void DetailedRouter::updateDRModel(DRModel& dr_model)
{
  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();
  for (int32_t x = 0; x < dr_box_map.get_x_size(); x++) {
    for (int32_t y = 0; y < dr_box_map.get_y_size(); y++) {
      DRBox& dr_box = dr_box_map[x][y];
      for (auto& [net_idx, segment_list] : dr_box.get_net_task_detailed_result_map()) {
        std::vector<Segment<LayerCoord>>& model_segment_list = dr_model.get_net_detailed_result_map()[net_idx];
        model_segment_list.insert(model_segment_list.end(), std::make_move_iterator(segment_list.begin()), std::make_move_iterator(segment_list.end()));
      }
      for (auto& [net_idx, patch_list] : dr_box.get_net_task_detailed_patch_map()) {
        std::vector<EXTLayerRect>& model_patch_list = dr_model.get_net_detailed_patch_map()[net_idx];
        model_patch_list.insert(model_patch_list.end(), std::make_move_iterator(patch_list.begin()), std::make_move_iterator(patch_list.end()));
      }
      dr_box.get_net_task_detailed_result_map().clear();
      dr_box.get_net_task_detailed_patch_map().clear();
    }
  }
}

int32_t DetailedRouter::getRouteViolationNum(DRModel& dr_model)
{
  return static_cast<int32_t>(dr_model.get_route_violation_list().size());
}

void DetailedRouter::updateNetResult(DRModel& dr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<DRNet>& dr_net_list = dr_model.get_dr_net_list();
  std::vector<std::pair<int32_t, std::vector<Segment<LayerCoord>>*>> net_result_list;
  net_result_list.reserve(dr_model.get_net_detailed_result_map().size());
  for (auto& [net_idx, segment_list] : dr_model.get_net_detailed_result_map()) {
    if (!segment_list.empty()) {
      net_result_list.emplace_back(net_idx, &segment_list);
    }
  }
  std::vector<int32_t> legacy_default_via_num_list(net_result_list.size(), 0);
#pragma omp parallel for schedule(dynamic, 1)
  for (int32_t result_idx = 0; result_idx < static_cast<int32_t>(net_result_list.size()); result_idx++) {
    auto& i = net_result_list[result_idx];
    int32_t net_idx = i.first;
    std::vector<Segment<LayerCoord>>& detailed_result_list = *i.second;
    std::vector<Segment<LayerCoord>> via_segment_list;
    for (Segment<LayerCoord>& segment : detailed_result_list) {
      if (segment.get_first().get_planar_coord() == segment.get_second().get_planar_coord()
          && std::abs(segment.get_first().get_layer_idx() - segment.get_second().get_layer_idx()) == 1) {
        int32_t below_layer_idx = std::min(segment.get_first().get_layer_idx(), segment.get_second().get_layer_idx());
        if (segment.hasValidViaMaster()) {
          if (!isViaMasterIdxValid(segment.get_via_master_idx(), below_layer_idx)) {
            RTLOG.error(Loc::current(), "The detailed result has an invalid via master index! net_idx: ", net_idx, ", below_layer_idx: ", below_layer_idx,
                        ", via_idx: ", segment.get_via_master_idx().get_via_idx());
          }
          via_segment_list.push_back(segment);
        }
      }
    }
    std::vector<LayerCoord> candidate_root_coord_list;
    std::map<LayerCoord, std::set<int32_t>, CmpLayerCoordByXASC> key_coord_pin_map;
    std::vector<DRPin>& dr_pin_list = dr_net_list[net_idx].get_dr_pin_list();
    candidate_root_coord_list.reserve(dr_pin_list.size());
    for (size_t i = 0; i < dr_pin_list.size(); i++) {
      LayerCoord coord = dr_pin_list[i].get_access_point().getRealLayerCoord();
      candidate_root_coord_list.push_back(coord);
      key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
    }
    MTree<LayerCoord> coord_tree = RTUTIL.getTreeByFullFlow(candidate_root_coord_list, detailed_result_list, key_coord_pin_map);
    std::vector<Segment<LayerCoord>> new_detailed_result_list;
    int32_t legacy_default_via_num = 0;
    for (Segment<TNode<LayerCoord>*>& coord_segment : RTUTIL.getSegListByTree(coord_tree)) {
      LayerCoord first = coord_segment.get_first()->value();
      LayerCoord second = coord_segment.get_second()->value();
      if (first.get_planar_coord() != second.get_planar_coord() || first.get_layer_idx() == second.get_layer_idx()) {
        new_detailed_result_list.emplace_back(first, second);
        continue;
      }
      int32_t below_layer_idx = std::min(first.get_layer_idx(), second.get_layer_idx());
      int32_t above_layer_idx = std::max(first.get_layer_idx(), second.get_layer_idx());
      for (int32_t layer_idx = below_layer_idx; layer_idx < above_layer_idx; layer_idx++) {
        Segment<LayerCoord> unit_via_segment(LayerCoord(first.get_planar_coord(), layer_idx), LayerCoord(first.get_planar_coord(), layer_idx + 1));
        resolveResultViaMasterIdx(net_idx, dr_pin_list, via_segment_list, unit_via_segment, legacy_default_via_num);
        new_detailed_result_list.push_back(unit_via_segment);
      }
    }
    detailed_result_list = std::move(new_detailed_result_list);
    checkAPViaMasterConstraint(net_idx, dr_pin_list, detailed_result_list);
    legacy_default_via_num_list[result_idx] = legacy_default_via_num;
  }
  int32_t legacy_default_via_num = std::accumulate(legacy_default_via_num_list.begin(), legacy_default_via_num_list.end(), 0);
  if (legacy_default_via_num > 0) {
    RTLOG.warn(Loc::current(), "Materialized ", legacy_default_via_num, " legacy vias with the default via master!");
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DetailedRouter::updateNetPatch(DRModel& dr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::map<int32_t, std::vector<Segment<LayerCoord>>>& net_detailed_result_map = dr_model.get_net_detailed_result_map();
  for (auto& [net_idx, patch_list] : dr_model.get_net_detailed_patch_map()) {
    std::map<int32_t, std::vector<PlanarRect>> layer_rect_map;
    for (Segment<LayerCoord>& segment : net_detailed_result_map[net_idx]) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
        if (!net_shape.get_is_routing()) {
          continue;
        }
        layer_rect_map[net_shape.get_layer_idx()].push_back(net_shape.get_rect());
      }
    }
    std::vector<EXTLayerRect> used_patch_list;
    for (EXTLayerRect& patch : patch_list) {
      bool is_used = false;
      for (PlanarRect& rect : layer_rect_map[patch.get_layer_idx()]) {
        if (RTUTIL.isClosedOverlap(patch.get_real_rect(), rect)) {
          is_used = true;
          break;
        }
      }
      if (is_used) {
        used_patch_list.push_back(patch);
      }
    }
    patch_list = std::move(used_patch_list);
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DetailedRouter::updateViolation(DRModel& dr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();
  if (dr_box_map.empty()) {
    dr_model.get_route_violation_list() = getRouteViolationList(dr_model);
    RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
    return;
  }

  GridMap<bool> dirty_box_map(dr_box_map.get_x_size(), dr_box_map.get_y_size(), false);
  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  size_t routed_box_num = 0;
  for (int32_t x = 0; x < dr_box_map.get_x_size(); x++) {
    for (int32_t y = 0; y < dr_box_map.get_y_size(); y++) {
      DRBox& dr_box = dr_box_map[x][y];
      if (!dr_box.get_dirty()) {
        continue;
      }
      routed_box_num++;
      for (const DRBoxId& dr_box_id : getDRBoxIdSet(dr_model, RTUTIL.getEnlargedRect(dr_box.get_box_rect().get_real_rect(), detection_distance))) {
        dirty_box_map[dr_box_id.get_x()][dr_box_id.get_y()] = true;
      }
    }
  }
  std::vector<DRBoxId> dirty_box_id_list;
  for (int32_t x = 0; x < dr_box_map.get_x_size(); x++) {
    for (int32_t y = 0; y < dr_box_map.get_y_size(); y++) {
      if (dirty_box_map[x][y]) {
        dirty_box_id_list.emplace_back(x, y);
      }
    }
  }
  RTLOG.info(Loc::current(), "Checking ", dirty_box_id_list.size(), " dirty boxes from ", routed_box_num, " routed boxes");

  size_t total_box_num = static_cast<size_t>(dr_box_map.get_x_size()) * dr_box_map.get_y_size();
  if (dirty_box_id_list.size() * 4 >= total_box_num) {
    RTLOG.info(Loc::current(), "Dirty box ratio reached 25%, using full DRC");
    dr_model.get_route_violation_list() = getRouteViolationList(dr_model);
    RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
    return;
  }

  buildDirtyNetEnvironment(dr_model, dirty_box_id_list);
  std::vector<std::vector<Violation>> violation_list_list(dirty_box_id_list.size());
#pragma omp parallel for schedule(dynamic, 1)
  for (size_t i = 0; i < dirty_box_id_list.size(); i++) {
    DRBoxId& dr_box_id = dirty_box_id_list[i];
    violation_list_list[i] = getDirtyRouteViolationList(dr_model, dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()]);
  }
  std::set<Violation, CmpViolation> violation_set;
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  for (Violation& violation : dr_model.get_route_violation_list()) {
    PlanarCoord midpoint = violation.get_violation_shape().get_real_rect().getMidPoint();
    int32_t grid_x = RTUTIL.getGCellGridLB(midpoint.get_x(), gcell_axis.get_x_grid_list());
    int32_t grid_y = RTUTIL.getGCellGridLB(midpoint.get_y(), gcell_axis.get_y_grid_list());
    int32_t owner_x = dr_model.get_gcell_x_box_idx_list()[grid_x];
    int32_t owner_y = dr_model.get_gcell_y_box_idx_list()[grid_y];
    if (!dirty_box_map[owner_x][owner_y]) {
      violation_set.insert(violation);
    }
  }
  for (std::vector<Violation>& violation_list : violation_list_list) {
    violation_set.insert(violation_list.begin(), violation_list.end());
  }
  dr_model.get_route_violation_list().assign(violation_set.begin(), violation_set.end());
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

std::vector<Violation> DetailedRouter::getRouteViolationList(DRModel& dr_model)
{
  DETask de_task;
  {
    std::string top_name = RTUTIL.getString("dr_model");
    std::vector<std::pair<EXTLayerRect*, bool>> env_shape_list;
    std::map<int32_t, std::vector<std::pair<EXTLayerRect*, bool>>> net_pin_shape_map;
    auto& type_layer_fixed_rect_rtree_map = RTDM.getDatabase().get_type_layer_fixed_rect_rtree_map();
    for (bool is_routing : {false, true}) {
      for (auto& [layer_idx, fixed_rect_rtree] : type_layer_fixed_rect_rtree_map[is_routing]) {
        for (const auto& [rect, net_fixed_rect] : fixed_rect_rtree) {
          auto [net_idx, fixed_rect] = net_fixed_rect;
          if (net_idx == -1) {
            env_shape_list.emplace_back(fixed_rect, is_routing);
          } else {
            net_pin_shape_map[net_idx].emplace_back(fixed_rect, is_routing);
          }
        }
      }
    }
    std::map<int32_t, std::vector<Segment<LayerCoord>*>> net_result_map;
    for (auto& [net_idx, segment_list] : dr_model.get_net_detailed_result_map()) {
      for (Segment<LayerCoord>& segment : segment_list) {
        net_result_map[net_idx].push_back(&segment);
      }
    }
    std::map<int32_t, std::vector<EXTLayerRect*>> net_patch_map;
    for (auto& [net_idx, patch_list] : dr_model.get_net_detailed_patch_map()) {
      for (EXTLayerRect& patch : patch_list) {
        net_patch_map[net_idx].emplace_back(&patch);
      }
    }
    std::set<int32_t> need_checked_net_set;
    for (DRNet& dr_net : dr_model.get_dr_net_list()) {
      need_checked_net_set.insert(dr_net.get_net_idx());
    }

    de_task.set_proc_type(DEProcType::kGet);
    de_task.set_net_type(DENetType::kRouteHybrid);
    de_task.set_top_name(top_name);
    de_task.set_env_shape_list(env_shape_list);
    de_task.set_net_pin_shape_map(net_pin_shape_map);
    de_task.set_net_result_map(net_result_map);
    de_task.set_net_patch_map(net_patch_map);
    de_task.set_need_checked_net_set(need_checked_net_set);
  }
  return RTDE.getViolationList(de_task);
}

std::vector<Violation> DetailedRouter::getDirtyRouteViolationList(DRModel& dr_model, DRBox& dr_box)
{
  std::string top_name = RTUTIL.getString("dr_box_", dr_box.get_dr_box_id().get_x(), "_", dr_box.get_dr_box_id().get_y(), "_dirty");
  std::vector<std::pair<EXTLayerRect*, bool>> env_shape_list;
  std::map<int32_t, std::vector<std::pair<EXTLayerRect*, bool>>> net_pin_shape_map;
  auto type_layer_net_fixed_rect_map = RTDM.getTypeLayerNetFixedRectMap(dr_box.get_box_rect());
  for (auto& [is_routing, layer_net_fixed_rect_map] : type_layer_net_fixed_rect_map) {
    for (auto& [layer_idx, net_fixed_rect_map] : layer_net_fixed_rect_map) {
      for (auto& [net_idx, fixed_rect_set] : net_fixed_rect_map) {
        for (EXTLayerRect* fixed_rect : fixed_rect_set) {
          if (net_idx == -1) {
            env_shape_list.emplace_back(fixed_rect, is_routing);
          } else {
            net_pin_shape_map[net_idx].emplace_back(fixed_rect, is_routing);
          }
        }
      }
    }
  }

  std::map<int32_t, std::vector<Segment<LayerCoord>*>> net_result_map;
  std::map<int32_t, std::vector<EXTLayerRect*>> net_patch_map;
  std::set<int32_t> need_checked_net_set;
  for (auto& [net_idx, segment_list] : dr_box.get_net_detailed_result_map()) {
    net_result_map[net_idx] = segment_list;
    need_checked_net_set.insert(net_idx);
  }
  for (auto& [net_idx, patch_list] : dr_box.get_net_detailed_patch_map()) {
    net_patch_map[net_idx] = patch_list;
    need_checked_net_set.insert(net_idx);
  }

  std::vector<LayerRect> check_region_list;
  for (RoutingLayer& routing_layer : RTDM.getDatabase().get_routing_layer_list()) {
    check_region_list.emplace_back(dr_box.get_box_rect().get_real_rect(), routing_layer.get_layer_idx());
  }

  DETask de_task;
  de_task.set_proc_type(DEProcType::kGet);
  de_task.set_net_type(DENetType::kRouteHybrid);
  de_task.set_top_name(top_name);
  de_task.set_env_shape_list(std::move(env_shape_list));
  de_task.set_net_pin_shape_map(std::move(net_pin_shape_map));
  de_task.set_net_result_map(std::move(net_result_map));
  de_task.set_net_patch_map(std::move(net_patch_map));
  de_task.set_need_checked_net_set(need_checked_net_set);
  de_task.set_check_region_list(check_region_list);
  std::vector<Violation> owned_violation_list;
  for (Violation& violation : RTDE.getViolationList(de_task)) {
    PlanarCoord midpoint = violation.get_violation_shape().get_real_rect().getMidPoint();
    ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
    int32_t grid_x = RTUTIL.getGCellGridLB(midpoint.get_x(), gcell_axis.get_x_grid_list());
    int32_t grid_y = RTUTIL.getGCellGridLB(midpoint.get_y(), gcell_axis.get_y_grid_list());
    int32_t owner_x = dr_model.get_gcell_x_box_idx_list()[grid_x];
    int32_t owner_y = dr_model.get_gcell_y_box_idx_list()[grid_y];
    if (owner_x == dr_box.get_dr_box_id().get_x() && owner_y == dr_box.get_dr_box_id().get_y()) {
      owned_violation_list.push_back(std::move(violation));
    }
  }
  return owned_violation_list;
}

void DetailedRouter::updateBestResult(DRModel& dr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::map<int32_t, std::vector<Segment<LayerCoord>>>& best_net_detailed_result_map = dr_model.get_best_net_detailed_result_map();
  std::map<int32_t, std::vector<EXTLayerRect>>& best_net_detailed_patch_map = dr_model.get_best_net_detailed_patch_map();
  std::vector<Violation>& best_route_violation_list = dr_model.get_best_route_violation_list();

  int32_t curr_violation_num = getRouteViolationNum(dr_model);
  if (!best_net_detailed_result_map.empty()) {
    if (static_cast<int32_t>(best_route_violation_list.size()) < curr_violation_num) {
      return;
    }
  }
  best_net_detailed_result_map = dr_model.get_net_detailed_result_map();
  best_net_detailed_patch_map = dr_model.get_net_detailed_patch_map();
  best_route_violation_list = dr_model.get_route_violation_list();

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

bool DetailedRouter::stopIteration(DRModel& dr_model, std::vector<DRIterParam>& dr_iter_param_list)
{
  if (dr_model.get_iter() != static_cast<int32_t>(dr_iter_param_list.size()) && getRouteViolationNum(dr_model) == 0) {
    RTLOG.info(Loc::current(), "***** Iteration stopped early *****");
    return true;
  }
  return false;
}

void DetailedRouter::selectBestResult(DRModel& dr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  dr_model.set_iter(dr_model.get_iter() + 1);
  dr_model.get_net_detailed_result_map() = std::move(dr_model.get_best_net_detailed_result_map());
  dr_model.get_net_detailed_patch_map() = std::move(dr_model.get_best_net_detailed_patch_map());
  dr_model.get_route_violation_list() = std::move(dr_model.get_best_route_violation_list());
  patchFinalMinArea(dr_model);
  updateSummary(dr_model);
  printSummary(dr_model);
  outputNetCSV(dr_model);
  outputViolationCSV(dr_model);
  uploadDRModel(dr_model);

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DetailedRouter::patchFinalMinArea(DRModel& dr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<Violation*> min_area_violation_list;
  for (Violation& violation : dr_model.get_route_violation_list()) {
    if (violation.get_violation_type() == ViolationType::kMinimumArea) {
      min_area_violation_list.push_back(&violation);
    }
  }
  if (min_area_violation_list.empty()) {
    RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
    return;
  }

  initDRBoxMap(dr_model);
  buildBoxSchedule(dr_model);

  GridMap<DRBox>& dr_box_map = dr_model.get_dr_box_map();
  GridMap<std::set<Violation*, CmpViolation>> patch_violation_map(dr_box_map.get_x_size(), dr_box_map.get_y_size());
  for (Violation* violation : min_area_violation_list) {
    for (const DRBoxId& dr_box_id : getDRBoxIdSet(dr_model, violation->get_violation_shape().get_real_rect())) {
      patch_violation_map[dr_box_id.get_x()][dr_box_id.get_y()].insert(violation);
    }
  }

  std::map<int32_t, std::set<LayerRect, CmpLayerRectByXASC>> uploaded_patch_map;
  for (auto& [net_idx, patch_list] : dr_model.get_net_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      uploaded_patch_map[net_idx].insert(patch.getRealLayerRect());
    }
  }

  bool patch_updated = false;
  for (std::vector<DRBoxId>& dr_box_id_list : dr_model.get_dr_box_id_list_list()) {
    std::map<int32_t, std::vector<EXTLayerRect>> new_patch_map;
    std::vector<DRBoxId> patch_box_id_list;
    for (DRBoxId& dr_box_id : dr_box_id_list) {
      if (!patch_violation_map[dr_box_id.get_x()][dr_box_id.get_y()].empty()) {
        patch_box_id_list.push_back(dr_box_id);
        dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()].set_dirty(true);
      }
    }
    buildNetEnvironment(dr_model, patch_box_id_list);
#pragma omp parallel for schedule(dynamic, 1)
    for (int32_t i = 0; i < static_cast<int32_t>(patch_box_id_list.size()); i++) {
      DRBoxId& dr_box_id = patch_box_id_list[i];
      DRBox& dr_box = dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()];
      std::set<Violation*, CmpViolation>& patch_violation_set = patch_violation_map[dr_box_id.get_x()][dr_box_id.get_y()];
      buildFinalPatchBox(dr_model, dr_box, patch_violation_set);
      if (!dr_box.get_dr_task_list().empty()) {
        buildBoxTrackAxis(dr_box);
        buildLayerNodeMap(dr_box);
        buildLayerShadowMap(dr_box);
        buildDRNodeNeighbor(dr_box);
        buildOrientNetMap(dr_box);
        buildNetShadowMap(dr_box);
        for (DRTask* dr_task : dr_box.get_dr_task_list()) {
          patchDRTask(dr_box, dr_task);
        }
      }
      freeDRBox(dr_box);
    }
    for (DRBoxId& dr_box_id : patch_box_id_list) {
      DRBox& dr_box = dr_box_map[dr_box_id.get_x()][dr_box_id.get_y()];
      updateFinalPatch(dr_box, uploaded_patch_map, new_patch_map);
      dr_box.get_net_task_detailed_result_map().clear();
      dr_box.get_net_task_detailed_patch_map().clear();
    }
    for (auto& [net_idx, patch_list] : new_patch_map) {
      patch_updated = patch_updated || !patch_list.empty();
      std::vector<EXTLayerRect>& model_patch_list = dr_model.get_net_detailed_patch_map()[net_idx];
      model_patch_list.insert(model_patch_list.end(), std::make_move_iterator(patch_list.begin()), std::make_move_iterator(patch_list.end()));
    }
  }
  if (patch_updated) {
    updateViolation(dr_model);
  }
  dr_model.get_dr_box_map().free();
  std::vector<std::vector<DRBoxId>>().swap(dr_model.get_dr_box_id_list_list());

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DetailedRouter::buildFinalPatchBox(DRModel& dr_model, DRBox& dr_box, const std::set<Violation*, CmpViolation>& patch_violation_set)
{
  PlanarRect& box_real_rect = dr_box.get_box_rect().get_real_rect();
  std::vector<DRNet>& dr_net_list = dr_model.get_dr_net_list();

  std::set<int32_t> patch_net_set;
  for (Violation* violation : patch_violation_set) {
    if (!RTUTIL.isOpenOverlap(box_real_rect, violation->get_violation_shape().get_real_rect())) {
      continue;
    }
    for (int32_t net_idx : violation->get_violation_net_set()) {
      if (0 <= net_idx && net_idx < static_cast<int32_t>(dr_net_list.size())) {
        patch_net_set.insert(net_idx);
      }
    }
  }
  if (patch_net_set.empty()) {
    return;
  }

  buildFixedRect(dr_box);
  for (int32_t net_idx : patch_net_set) {
    DRTask* dr_task = new DRTask();
    dr_task->set_net_idx(net_idx);
    dr_task->set_connect_type(dr_net_list[net_idx].get_connect_type());
    dr_task->set_bounding_box(box_real_rect);
    dr_box.get_dr_task_list().push_back(dr_task);
  }
}

void DetailedRouter::updateFinalPatch(DRBox& dr_box, std::map<int32_t, std::set<LayerRect, CmpLayerRectByXASC>>& uploaded_patch_map,
                                      std::map<int32_t, std::vector<EXTLayerRect>>& new_patch_map)
{
  for (auto& [net_idx, patch_list] : dr_box.get_net_task_detailed_patch_map()) {
    std::set<LayerRect, CmpLayerRectByXASC>& uploaded_patch_set = uploaded_patch_map[net_idx];
    for (EXTLayerRect& patch : patch_list) {
      LayerRect patch_rect = patch.getRealLayerRect();
      if (uploaded_patch_set.insert(patch_rect).second) {
        new_patch_map[net_idx].push_back(patch);
      }
    }
  }
}

void DetailedRouter::uploadDRModel(DRModel& dr_model)
{
  Die& die = RTDM.getDatabase().get_die();

  for (const Violation& violation : RTDM.getViolationList(die)) {
    RTDM.updateViolationToRTree(ChangeType::kDel, violation);
  }

  RTDM.getDatabase().get_net_detailed_result_map() = std::move(dr_model.get_net_detailed_result_map());
  RTDM.getDatabase().get_net_detailed_patch_map() = std::move(dr_model.get_net_detailed_patch_map());
  for (Violation& violation : dr_model.get_route_violation_list()) {
    RTDM.updateViolationToRTree(ChangeType::kAdd, violation);
  }
}

#if 1  // update env

void DetailedRouter::updateFixedRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, EXTLayerRect* fixed_rect, bool is_routing)
{
  NetShape net_shape(net_idx, fixed_rect->getRealLayerRect(), is_routing);
  updateNetShapeToGraph(dr_box, change_type, net_shape, true);
}

void DetailedRouter::updateFixedRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, LayerRect& real_rect, bool is_routing)
{
  NetShape net_shape(net_idx, real_rect, is_routing);
  updateNetShapeToGraph(dr_box, change_type, net_shape, true);
}

void DetailedRouter::updateFixedRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>* segment)
{
  for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, *segment)) {
    updateNetShapeToGraph(dr_box, change_type, net_shape, true);
  }
}

void DetailedRouter::updateRoutedRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, LayerRect& real_rect, bool is_routing)
{
  NetShape net_shape(net_idx, real_rect, is_routing);
  updateNetShapeToGraph(dr_box, change_type, net_shape, false);
}

void DetailedRouter::updateRoutedRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>& segment)
{
  for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
    updateNetShapeToGraph(dr_box, change_type, net_shape, false);
  }
}

void DetailedRouter::updateRoutedRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, EXTLayerRect& routed_rect, bool is_routing)
{
  NetShape net_shape(net_idx, routed_rect.getRealLayerRect(), is_routing);
  updateNetShapeToGraph(dr_box, change_type, net_shape, false);
}

void DetailedRouter::addRouteViolationToGraph(DRBox& dr_box, Violation& violation)
{
  LayerRect searched_rect = violation.get_violation_shape().get_real_rect();
  std::vector<Segment<LayerCoord>> overlap_segment_list;
  std::set<int32_t> target_net_set;
  std::set<int32_t> found_net_set;
  for (int32_t net_idx : violation.get_violation_net_set()) {
    if (net_idx != -1) {
      target_net_set.insert(net_idx);
    }
  }
  if (target_net_set.empty()) {
    return;
  }
  int32_t searched_times = 0;
  constexpr int32_t max_searched_times = 3;
  while (true) {
    searched_rect.set_rect(RTUTIL.getEnlargedRect(searched_rect, RTDM.getOnlyPitch()));
    searched_times++;
    if (violation.get_is_routing()) {
      searched_rect.set_layer_idx(violation.get_violation_shape().get_layer_idx());
    } else {
      RTLOG.error(Loc::current(), "The violation layer is cut!");
    }
    for (auto& [net_idx, segment_list] : dr_box.get_net_task_detailed_result_map()) {
      if (!RTUTIL.exist(target_net_set, net_idx) || RTUTIL.exist(found_net_set, net_idx)) {
        continue;
      }
      for (Segment<LayerCoord>& segment : segment_list) {
        bool is_overlap = false;
        for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
          if (searched_rect.get_layer_idx() == net_shape.get_layer_idx() && RTUTIL.isClosedOverlap(searched_rect, net_shape.get_rect())) {
            is_overlap = true;
            break;
          }
        }
        if (is_overlap) {
          overlap_segment_list.push_back(segment);
          found_net_set.insert(net_idx);
        }
      }
    }
    if (found_net_set.size() == target_net_set.size()) {
      break;
    }
    if (searched_times >= max_searched_times) {
      break;
    }
    if (!RTUTIL.isInside(dr_box.get_box_rect().get_real_rect(), searched_rect)) {
      break;
    }
  }
  addRouteViolationToGraph(dr_box, searched_rect, overlap_segment_list);
}

void DetailedRouter::addRouteViolationToGraph(DRBox& dr_box, LayerRect& searched_rect, std::vector<Segment<LayerCoord>>& overlap_segment_list)
{
  ScaleAxis& box_track_axis = dr_box.get_box_track_axis();
  std::vector<GridMap<DRNode>>& layer_node_map = dr_box.get_layer_node_map();

  for (Segment<LayerCoord>& overlap_segment : overlap_segment_list) {
    LayerCoord& first_coord = overlap_segment.get_first();
    LayerCoord& second_coord = overlap_segment.get_second();
    if (first_coord == second_coord) {
      continue;
    }
    PlanarRect real_rect = RTUTIL.getRect(first_coord, second_coord);
    if (!RTUTIL.existTrackGrid(real_rect, box_track_axis)) {
      continue;
    }
    PlanarRect grid_rect = RTUTIL.getTrackGrid(real_rect, box_track_axis);
    std::map<int32_t, std::set<DRNode*>> distance_node_map;
    {
      int32_t first_layer_idx = first_coord.get_layer_idx();
      int32_t second_layer_idx = second_coord.get_layer_idx();
      RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
      for (int32_t layer_idx = first_layer_idx; layer_idx <= second_layer_idx; layer_idx++) {
        for (int32_t x = grid_rect.get_ll_x(); x <= grid_rect.get_ur_x(); x++) {
          for (int32_t y = grid_rect.get_ll_y(); y <= grid_rect.get_ur_y(); y++) {
            DRNode* dr_node = &layer_node_map[layer_idx][x][y];
            if (searched_rect.get_layer_idx() != dr_node->get_layer_idx()) {
              continue;
            }
            int32_t distance = 0;
            if (!RTUTIL.isInside(searched_rect.get_rect(), dr_node->get_planar_coord())) {
              distance = RTUTIL.getManhattanDistance(searched_rect.getMidPoint(), dr_node->get_planar_coord());
            }
            distance_node_map[distance].insert(dr_node);
          }
        }
      }
    }
    std::set<DRNode*> valid_node_set;
    if (!distance_node_map[0].empty()) {
      valid_node_set = distance_node_map[0];
    } else {
      for (auto& [distance, node_set] : distance_node_map) {
        valid_node_set.insert(node_set.begin(), node_set.end());
        if (valid_node_set.size() >= 2) {
          break;
        }
      }
    }
    Orientation orientation = RTUTIL.getOrientation(first_coord, second_coord);
    Orientation oppo_orientation = RTUTIL.getOppositeOrientation(orientation);
    for (DRNode* valid_node : valid_node_set) {
      if (LayerCoord(*valid_node) != first_coord) {
        valid_node->addViolationNumber(oppo_orientation);
        if (DRNode* neighbor_node = valid_node->getNeighborNode(oppo_orientation)) {
          neighbor_node->addViolationNumber(orientation);
        }
      }
      if (LayerCoord(*valid_node) != second_coord) {
        valid_node->addViolationNumber(orientation);
        if (DRNode* neighbor_node = valid_node->getNeighborNode(orientation)) {
          neighbor_node->addViolationNumber(oppo_orientation);
        }
      }
    }
  }
}

void DetailedRouter::updateNetShapeToGraph(DRBox& dr_box, ChangeType change_type, NetShape& net_shape, bool is_fixed)
{
  if (net_shape.get_is_routing()) {
    updateRoutingNetShapeToGraph(dr_box, change_type, net_shape, is_fixed);
  } else {
    updateCutNetShapeToGraph(dr_box, change_type, net_shape, is_fixed);
  }
}

void DetailedRouter::updateNodeNetToGraph(DRNode& dr_node, ChangeType change_type, int32_t net_idx, Orientation orientation, bool is_fixed)
{
  if (change_type == ChangeType::kAdd) {
    if (is_fixed) {
      dr_node.addFixedRectNet(orientation, net_idx);
    } else {
      dr_node.addRoutedRectNet(orientation, net_idx);
    }
  } else if (change_type == ChangeType::kDel) {
    if (is_fixed) {
      dr_node.delFixedRectNet(orientation, net_idx);
    } else {
      dr_node.delRoutedRectNet(orientation, net_idx);
    }
  }
}

void DetailedRouter::updateRoutingNetShapeToGraph(DRBox& dr_box, ChangeType change_type, NetShape& net_shape, bool is_fixed)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::map<int32_t, PlanarRect>& layer_enclosure_map = RTDM.getDatabase().get_layer_enclosure_map();
  if (!net_shape.get_is_routing()) {
    RTLOG.error(Loc::current(), "The type of net_shape is cut!");
  }
  int32_t layer_idx = net_shape.get_layer_idx();
  RoutingLayer& routing_layer = routing_layer_list[layer_idx];
  // x_spacing y_spacing
  std::vector<std::pair<int32_t, int32_t>> spacing_pair_list;
  {
    // prl
    int32_t prl_spacing = routing_layer.getPRLSpacing(net_shape.get_rect());
    spacing_pair_list.emplace_back(prl_spacing, prl_spacing);
    // eol
    int32_t max_eol_spacing = std::max(routing_layer.get_eol_spacing(), routing_layer.get_eol_ete());
    if (routing_layer.isPreferH()) {
      spacing_pair_list.emplace_back(max_eol_spacing, routing_layer.get_eol_within());
    } else {
      spacing_pair_list.emplace_back(routing_layer.get_eol_within(), max_eol_spacing);
    }
  }
  int32_t half_wire_width = routing_layer.get_min_width() / 2;
  PlanarRect& enclosure = layer_enclosure_map[layer_idx];
  int32_t enclosure_half_x_span = enclosure.getXSpan() / 2;
  int32_t enclosure_half_y_span = enclosure.getYSpan() / 2;

  GridMap<DRNode>& dr_node_map = dr_box.get_layer_node_map()[layer_idx];
  // wire 与 net_shape
  for (auto& [x_spacing, y_spacing] : spacing_pair_list) {
    // 膨胀size为 half_wire_width + spacing
    int32_t enlarged_x_size = half_wire_width + x_spacing;
    int32_t enlarged_y_size = half_wire_width + y_spacing;
    // 贴合的也不算违例
    enlarged_x_size -= 1;
    enlarged_y_size -= 1;
    PlanarRect planar_enlarged_rect = RTUTIL.getEnlargedRect(net_shape.get_rect(), enlarged_x_size, enlarged_y_size, enlarged_x_size, enlarged_y_size);
    for (const TrackGridOrientation& grid_orientation : RTUTIL.getTrackGridOrientationList(planar_enlarged_rect, dr_box.get_box_track_axis())) {
      if (!grid_orientation.isValid()) {
        continue;
      }
      const PlanarRect& grid_rect = grid_orientation.grid_rect;
      for (int32_t x = grid_rect.get_ll_x(); x <= grid_rect.get_ur_x(); x++) {
        for (int32_t y = grid_rect.get_ll_y(); y <= grid_rect.get_ur_y(); y++) {
          DRNode& node = dr_node_map[x][y];
          for (Orientation orientation : grid_orientation) {
            // Each update covers both endpoints, so only visit one direction of a planar edge.
            if (orientation != Orientation::kEast && orientation != Orientation::kNorth) {
              continue;
            }
            DRNode* neighbor_node = node.getNeighborNode(orientation);
            if (neighbor_node == nullptr) {
              continue;
            }
            updateNodeNetToGraph(node, change_type, net_shape.get_net_idx(), orientation, is_fixed);
            updateNodeNetToGraph(*neighbor_node, change_type, net_shape.get_net_idx(), RTUTIL.getOppositeOrientation(orientation), is_fixed);
          }
        }
      }
    }
  }
  // enclosure 与 net_shape
  for (auto& [x_spacing, y_spacing] : spacing_pair_list) {
    // 膨胀size为 enclosure_half_span + spacing
    int32_t enlarged_x_size = enclosure_half_x_span + x_spacing;
    int32_t enlarged_y_size = enclosure_half_y_span + y_spacing;
    // 贴合的也不算违例
    enlarged_x_size -= 1;
    enlarged_y_size -= 1;
    PlanarRect space_enlarged_rect = RTUTIL.getEnlargedRect(net_shape.get_rect(), enlarged_x_size, enlarged_y_size, enlarged_x_size, enlarged_y_size);
    for (const TrackGridOrientation& grid_orientation : RTUTIL.getTrackGridOrientationList(space_enlarged_rect, dr_box.get_box_track_axis())) {
      if (!grid_orientation.isValid()) {
        continue;
      }
      const PlanarRect& grid_rect = grid_orientation.grid_rect;
      for (int32_t x = grid_rect.get_ll_x(); x <= grid_rect.get_ur_x(); x++) {
        for (int32_t y = grid_rect.get_ll_y(); y <= grid_rect.get_ur_y(); y++) {
          DRNode& node = dr_node_map[x][y];
          for (Orientation orientation : grid_orientation) {
            if (orientation == Orientation::kEast || orientation == Orientation::kWest || orientation == Orientation::kSouth
                || orientation == Orientation::kNorth) {
              continue;
            }
            DRNode* neighbor_node = node.getNeighborNode(orientation);
            if (neighbor_node == nullptr) {
              continue;
            }
            updateNodeNetToGraph(node, change_type, net_shape.get_net_idx(), orientation, is_fixed);
            updateNodeNetToGraph(*neighbor_node, change_type, net_shape.get_net_idx(), RTUTIL.getOppositeOrientation(orientation), is_fixed);
          }
        }
      }
    }
  }
}

void DetailedRouter::updateCutNetShapeToGraph(DRBox& dr_box, ChangeType change_type, NetShape& net_shape, bool is_fixed)
{
  std::vector<CutLayer>& cut_layer_list = RTDM.getDatabase().get_cut_layer_list();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = RTDM.getDatabase().get_cut_to_adjacent_routing_map();
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();
  if (net_shape.get_is_routing()) {
    RTLOG.error(Loc::current(), "The type of net_shape is routing!");
  }
  CutLayer& cut_layer = cut_layer_list[net_shape.get_layer_idx()];
  std::map<int32_t, std::vector<std::pair<int32_t, int32_t>>> cut_spacing_map;
  {
    int32_t curr_cut_layer_idx = net_shape.get_layer_idx();
    if (0 <= curr_cut_layer_idx && curr_cut_layer_idx < static_cast<int32_t>(cut_layer_list.size())) {
      std::vector<int32_t> adjacent_routing_layer_idx_list = cut_to_adjacent_routing_map[curr_cut_layer_idx];
      if (adjacent_routing_layer_idx_list.size() == 2) {
        std::vector<std::pair<int32_t, int32_t>> spacing_pair_list;
        // prl
        spacing_pair_list.emplace_back(0, cut_layer.get_curr_spacing());
        spacing_pair_list.emplace_back(cut_layer.get_curr_spacing(), 0);
        spacing_pair_list.emplace_back(cut_layer.get_curr_spacing() / RT_SQRT_2, cut_layer.get_curr_spacing() / RT_SQRT_2);
        spacing_pair_list.emplace_back(cut_layer.get_curr_prl(), cut_layer.get_curr_prl_spacing());
        spacing_pair_list.emplace_back(cut_layer.get_curr_prl_spacing(), cut_layer.get_curr_prl());
        // eol
        spacing_pair_list.emplace_back(0, cut_layer.get_curr_eol_spacing());
        spacing_pair_list.emplace_back(cut_layer.get_curr_eol_spacing(), 0);
        spacing_pair_list.emplace_back(cut_layer.get_curr_eol_spacing() / RT_SQRT_2, cut_layer.get_curr_eol_spacing() / RT_SQRT_2);
        spacing_pair_list.emplace_back(cut_layer.get_curr_eol_prl(), cut_layer.get_curr_eol_prl_spacing());
        spacing_pair_list.emplace_back(cut_layer.get_curr_eol_prl_spacing(), cut_layer.get_curr_eol_prl());
        cut_spacing_map[curr_cut_layer_idx] = spacing_pair_list;
      }
    }
    int32_t below_cut_layer_idx = net_shape.get_layer_idx() - 1;
    if (0 <= below_cut_layer_idx && below_cut_layer_idx < static_cast<int32_t>(cut_layer_list.size())) {
      std::vector<int32_t> adjacent_routing_layer_idx_list = cut_to_adjacent_routing_map[below_cut_layer_idx];
      if (adjacent_routing_layer_idx_list.size() == 2) {
        std::vector<std::pair<int32_t, int32_t>> spacing_pair_list;
        // prl
        spacing_pair_list.emplace_back(0, cut_layer.get_below_spacing());
        spacing_pair_list.emplace_back(cut_layer.get_below_spacing(), 0);
        spacing_pair_list.emplace_back(cut_layer.get_below_spacing() / RT_SQRT_2, cut_layer.get_below_spacing() / RT_SQRT_2);
        spacing_pair_list.emplace_back(cut_layer.get_below_prl(), cut_layer.get_below_prl_spacing());
        spacing_pair_list.emplace_back(cut_layer.get_below_prl_spacing(), cut_layer.get_below_prl());
        cut_spacing_map[below_cut_layer_idx] = spacing_pair_list;
      }
    }
    int32_t above_cut_layer_idx = net_shape.get_layer_idx() + 1;
    if (0 <= above_cut_layer_idx && above_cut_layer_idx < static_cast<int32_t>(cut_layer_list.size())) {
      std::vector<int32_t> adjacent_routing_layer_idx_list = cut_to_adjacent_routing_map[above_cut_layer_idx];
      if (adjacent_routing_layer_idx_list.size() == 2) {
        std::vector<std::pair<int32_t, int32_t>> spacing_pair_list;
        // prl
        spacing_pair_list.emplace_back(0, cut_layer.get_above_spacing());
        spacing_pair_list.emplace_back(cut_layer.get_above_spacing(), 0);
        spacing_pair_list.emplace_back(cut_layer.get_above_spacing() / RT_SQRT_2, cut_layer.get_above_spacing() / RT_SQRT_2);
        spacing_pair_list.emplace_back(cut_layer.get_above_prl(), cut_layer.get_above_prl_spacing());
        spacing_pair_list.emplace_back(cut_layer.get_above_prl_spacing(), cut_layer.get_above_prl());
        cut_spacing_map[above_cut_layer_idx] = spacing_pair_list;
      }
    }
  }
  for (auto& [cut_layer_idx, spacing_pair_list] : cut_spacing_map) {
    std::vector<int32_t> adjacent_routing_layer_idx_list = cut_to_adjacent_routing_map[cut_layer_idx];
    int32_t below_routing_layer_idx = adjacent_routing_layer_idx_list.front();
    int32_t above_routing_layer_idx = adjacent_routing_layer_idx_list.back();
    RTUTIL.swapByASC(below_routing_layer_idx, above_routing_layer_idx);
    PlanarRect& cut_shape = layer_via_master_list[below_routing_layer_idx].front().get_cut_shape_list().front();
    int32_t cut_shape_half_x_span = cut_shape.getXSpan() / 2;
    int32_t cut_shape_half_y_span = cut_shape.getYSpan() / 2;
    std::vector<GridMap<DRNode>>& layer_node_map = dr_box.get_layer_node_map();
    for (auto& [x_spacing, y_spacing] : spacing_pair_list) {
      // 膨胀size为 cut_shape_half_span + spacing
      int32_t enlarged_x_size = cut_shape_half_x_span + x_spacing;
      int32_t enlarged_y_size = cut_shape_half_y_span + y_spacing;
      // 贴合的也不算违例
      enlarged_x_size -= 1;
      enlarged_y_size -= 1;
      PlanarRect space_enlarged_rect = RTUTIL.getEnlargedRect(net_shape.get_rect(), enlarged_x_size, enlarged_y_size, enlarged_x_size, enlarged_y_size);
      for (const TrackGridOrientation& grid_orientation : RTUTIL.getTrackGridOrientationList(space_enlarged_rect, dr_box.get_box_track_axis())) {
        if (!grid_orientation.isValid() || (!grid_orientation.hasOrientation(Orientation::kAbove) && !grid_orientation.hasOrientation(Orientation::kBelow))) {
          continue;
        }
        const PlanarRect& grid_rect = grid_orientation.grid_rect;
        for (int32_t x = grid_rect.get_ll_x(); x <= grid_rect.get_ur_x(); x++) {
          for (int32_t y = grid_rect.get_ll_y(); y <= grid_rect.get_ur_y(); y++) {
            DRNode& below_node = layer_node_map[below_routing_layer_idx][x][y];
            if (below_node.hasNeighborNode(Orientation::kAbove)) {
              updateNodeNetToGraph(below_node, change_type, net_shape.get_net_idx(), Orientation::kAbove, is_fixed);
            }
            DRNode& above_node = layer_node_map[above_routing_layer_idx][x][y];
            if (above_node.hasNeighborNode(Orientation::kBelow)) {
              updateNodeNetToGraph(above_node, change_type, net_shape.get_net_idx(), Orientation::kBelow, is_fixed);
            }
          }
        }
      }
    }
  }
}

void DetailedRouter::updateFixedRectToShadow(DRBox& dr_box, ChangeType change_type, int32_t net_idx, EXTLayerRect* fixed_rect, bool is_routing)
{
  NetShape net_shape(net_idx, fixed_rect->getRealLayerRect(), is_routing);
  if (!net_shape.get_is_routing()) {
    return;
  }
  for (PlanarRect& shadow_shape : getShadowShape(dr_box, net_shape)) {
    DRShadow& dr_shadow = dr_box.get_layer_shadow_map()[net_shape.get_layer_idx()];
    if (change_type == ChangeType::kAdd) {
      dr_shadow.get_net_fixed_rect_map()[net_idx].insert(shadow_shape);
    } else if (change_type == ChangeType::kDel) {
      dr_shadow.get_net_fixed_rect_map()[net_idx].erase(shadow_shape);
    }
  }
}

void DetailedRouter::updateFixedRectToShadow(DRBox& dr_box, ChangeType change_type, int32_t net_idx, LayerRect& real_rect, bool is_routing)
{
  NetShape net_shape(net_idx, real_rect, is_routing);
  if (!net_shape.get_is_routing()) {
    return;
  }
  for (PlanarRect& shadow_shape : getShadowShape(dr_box, net_shape)) {
    DRShadow& dr_shadow = dr_box.get_layer_shadow_map()[net_shape.get_layer_idx()];
    if (change_type == ChangeType::kAdd) {
      dr_shadow.get_net_fixed_rect_map()[net_idx].insert(shadow_shape);
    } else if (change_type == ChangeType::kDel) {
      dr_shadow.get_net_fixed_rect_map()[net_idx].erase(shadow_shape);
    }
  }
}

void DetailedRouter::updateFixedRectToShadow(DRBox& dr_box, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>* segment)
{
  for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, *segment)) {
    if (!net_shape.get_is_routing()) {
      continue;
    }
    for (PlanarRect& shadow_shape : getShadowShape(dr_box, net_shape)) {
      DRShadow& dr_shadow = dr_box.get_layer_shadow_map()[net_shape.get_layer_idx()];
      if (change_type == ChangeType::kAdd) {
        dr_shadow.get_net_fixed_rect_map()[net_idx].insert(shadow_shape);
      } else if (change_type == ChangeType::kDel) {
        dr_shadow.get_net_fixed_rect_map()[net_idx].erase(shadow_shape);
      }
    }
  }
}

void DetailedRouter::updateRoutedRectToShadow(DRBox& dr_box, ChangeType change_type, int32_t net_idx, LayerRect& real_rect, bool is_routing)
{
  NetShape net_shape(net_idx, real_rect, is_routing);
  if (!net_shape.get_is_routing()) {
    return;
  }
  for (PlanarRect& shadow_shape : getShadowShape(dr_box, net_shape)) {
    DRShadow& dr_shadow = dr_box.get_layer_shadow_map()[net_shape.get_layer_idx()];
    if (change_type == ChangeType::kAdd) {
      dr_shadow.get_net_routed_rect_map()[net_idx].insert(shadow_shape);
    } else if (change_type == ChangeType::kDel) {
      dr_shadow.get_net_routed_rect_map()[net_idx].erase(shadow_shape);
    }
  }
}

void DetailedRouter::updateRoutedRectToShadow(DRBox& dr_box, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>& segment)
{
  for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
    if (!net_shape.get_is_routing()) {
      continue;
    }
    for (PlanarRect& shadow_shape : getShadowShape(dr_box, net_shape)) {
      DRShadow& dr_shadow = dr_box.get_layer_shadow_map()[net_shape.get_layer_idx()];
      if (change_type == ChangeType::kAdd) {
        dr_shadow.get_net_routed_rect_map()[net_idx].insert(shadow_shape);
      } else if (change_type == ChangeType::kDel) {
        dr_shadow.get_net_routed_rect_map()[net_idx].erase(shadow_shape);
      }
    }
  }
}

void DetailedRouter::updateRoutedRectToShadow(DRBox& dr_box, ChangeType change_type, int32_t net_idx, EXTLayerRect& routed_rect, bool is_routing)
{
  NetShape net_shape(net_idx, routed_rect.getRealLayerRect(), is_routing);
  if (!net_shape.get_is_routing()) {
    return;
  }
  for (PlanarRect& shadow_shape : getShadowShape(dr_box, net_shape)) {
    DRShadow& dr_shadow = dr_box.get_layer_shadow_map()[net_shape.get_layer_idx()];
    if (change_type == ChangeType::kAdd) {
      dr_shadow.get_net_routed_rect_map()[net_idx].insert(shadow_shape);
    } else if (change_type == ChangeType::kDel) {
      dr_shadow.get_net_routed_rect_map()[net_idx].erase(shadow_shape);
    }
  }
}

void DetailedRouter::addPatchViolationToShadow(DRBox& dr_box, Violation& violation)
{
  EXTLayerRect& violation_shape = violation.get_violation_shape();

  DRShadow& dr_shadow = dr_box.get_layer_shadow_map()[violation_shape.get_layer_idx()];
  dr_shadow.get_violation_set().insert(violation_shape.get_real_rect());
}

std::vector<PlanarRect> DetailedRouter::getShadowShape(DRBox& dr_box, NetShape& net_shape)
{
  std::vector<PlanarRect> shadow_shape_list;
  if (net_shape.get_is_routing()) {
    shadow_shape_list = getRoutingShadowShapeList(dr_box, net_shape);
  } else {
    RTLOG.error(Loc::current(), "The type of net_shape is cut!");
  }
  return shadow_shape_list;
}

std::vector<PlanarRect> DetailedRouter::getRoutingShadowShapeList(DRBox& dr_box, NetShape& net_shape)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  if (!net_shape.get_is_routing()) {
    RTLOG.error(Loc::current(), "The type of net_shape is cut!");
  }
  int32_t layer_idx = net_shape.get_layer_idx();
  RoutingLayer& routing_layer = routing_layer_list[layer_idx];
  // x_spacing y_spacing
  std::vector<std::pair<int32_t, int32_t>> spacing_pair_list;
  {
    // prl
    int32_t prl_spacing = routing_layer.getPRLSpacing(net_shape.get_rect());
    spacing_pair_list.emplace_back(prl_spacing, prl_spacing);
    // eol
    int32_t max_eol_spacing = std::max(routing_layer.get_eol_spacing(), routing_layer.get_eol_ete());
    if (routing_layer.isPreferH()) {
      spacing_pair_list.emplace_back(max_eol_spacing, routing_layer.get_eol_within());
    } else {
      spacing_pair_list.emplace_back(routing_layer.get_eol_within(), max_eol_spacing);
    }
  }
  std::vector<PlanarRect> shadow_shape_list;
  // wire 与 net_shape
  for (auto& [x_spacing, y_spacing] : spacing_pair_list) {
    // 膨胀size为 spacing
    int32_t enlarged_x_size = x_spacing;
    int32_t enlarged_y_size = y_spacing;
    // 贴合的也不算违例
    enlarged_x_size -= 1;
    enlarged_y_size -= 1;
    shadow_shape_list.push_back(RTUTIL.getEnlargedRect(net_shape.get_rect(), enlarged_x_size, enlarged_y_size, enlarged_x_size, enlarged_y_size));
  }
  // enclosure 与 net_shape
  for (auto& [x_spacing, y_spacing] : spacing_pair_list) {
    // 膨胀size为 spacing
    int32_t enlarged_x_size = x_spacing;
    int32_t enlarged_y_size = y_spacing;
    // 贴合的也不算违例
    enlarged_x_size -= 1;
    enlarged_y_size -= 1;
    shadow_shape_list.push_back(RTUTIL.getEnlargedRect(net_shape.get_rect(), enlarged_x_size, enlarged_y_size, enlarged_x_size, enlarged_y_size));
  }
  return shadow_shape_list;
}

#endif

#if 1  // get env

double DetailedRouter::getFixedRectCost(DRBox& dr_box, int32_t net_idx, EXTLayerRect& patch)
{
  double fixed_rect_unit = dr_box.get_dr_iter_param()->get_fixed_rect_unit();
  std::vector<DRShadow>& layer_shadow_map = dr_box.get_layer_shadow_map();

  double fixed_rect_cost = 0;
  for (auto& [graph_net_idx, fixed_rect_set] : layer_shadow_map[patch.get_layer_idx()].get_net_fixed_rect_map()) {
    if (net_idx == graph_net_idx) {
      continue;
    }
    for (const PlanarRect& fixed_rect : fixed_rect_set) {
      if (RTUTIL.isOpenOverlap(patch.get_real_rect(), fixed_rect)) {
        fixed_rect_cost += fixed_rect_unit;
      }
    }
  }
  return fixed_rect_cost;
}

double DetailedRouter::getRoutedRectCost(DRBox& dr_box, int32_t net_idx, EXTLayerRect& patch)
{
  double routed_rect_unit = dr_box.get_dr_iter_param()->get_routed_rect_unit();
  std::vector<DRShadow>& layer_shadow_map = dr_box.get_layer_shadow_map();

  double routed_rect_cost = 0;
  for (auto& [graph_net_idx, routed_rect_set] : layer_shadow_map[patch.get_layer_idx()].get_net_routed_rect_map()) {
    if (net_idx == graph_net_idx) {
      continue;
    }
    for (const PlanarRect& routed_rect : routed_rect_set) {
      if (RTUTIL.isOpenOverlap(patch.get_real_rect(), routed_rect)) {
        routed_rect_cost += routed_rect_unit;
      }
    }
  }
  return routed_rect_cost;
}

double DetailedRouter::getViolationCost(DRBox& dr_box, int32_t net_idx, EXTLayerRect& patch)
{
  double violation_unit = dr_box.get_dr_iter_param()->get_violation_unit();
  std::vector<DRShadow>& layer_shadow_map = dr_box.get_layer_shadow_map();

  double violation_cost = 0;
  for (const PlanarRect& violation : layer_shadow_map[patch.get_layer_idx()].get_violation_set()) {
    if (RTUTIL.isOpenOverlap(patch.get_real_rect(), violation)) {
      violation_cost += violation_unit;
    }
  }
  return violation_cost;
}

#endif

#if 1  // exhibit

void DetailedRouter::updateSummary(DRModel& dr_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();
  Summary& summary = RTDM.getDatabase().get_summary();
  int32_t enable_timing = RTDM.getConfig().enable_timing;

  std::map<int32_t, double>& routing_wire_length_map = summary.iter_dr_summary_map[dr_model.get_iter()].routing_wire_length_map;
  double& total_wire_length = summary.iter_dr_summary_map[dr_model.get_iter()].total_wire_length;
  std::map<int32_t, int32_t>& cut_via_num_map = summary.iter_dr_summary_map[dr_model.get_iter()].cut_via_num_map;
  int32_t& total_via_num = summary.iter_dr_summary_map[dr_model.get_iter()].total_via_num;
  std::map<int32_t, int32_t>& routing_patch_num_map = summary.iter_dr_summary_map[dr_model.get_iter()].routing_patch_num_map;
  int32_t& total_patch_num = summary.iter_dr_summary_map[dr_model.get_iter()].total_patch_num;
  std::map<int32_t, int32_t>& routing_violation_num_map = summary.iter_dr_summary_map[dr_model.get_iter()].routing_violation_num_map;
  int32_t& total_violation_num = summary.iter_dr_summary_map[dr_model.get_iter()].total_violation_num;
  std::map<std::string, std::map<std::string, double>>& clock_timing_map = summary.iter_dr_summary_map[dr_model.get_iter()].clock_timing_map;

  std::vector<DRNet>& dr_net_list = dr_model.get_dr_net_list();

  routing_wire_length_map.clear();
  total_wire_length = 0;
  cut_via_num_map.clear();
  total_via_num = 0;
  routing_patch_num_map.clear();
  total_patch_num = 0;
  routing_violation_num_map.clear();
  total_violation_num = 0;
  clock_timing_map.clear();

  for (auto& [net_idx, segment_list] : dr_model.get_net_detailed_result_map()) {
    for (Segment<LayerCoord>& segment : segment_list) {
      LayerCoord& first_coord = segment.get_first();
      int32_t first_layer_idx = first_coord.get_layer_idx();
      LayerCoord& second_coord = segment.get_second();
      int32_t second_layer_idx = second_coord.get_layer_idx();

      if (first_layer_idx == second_layer_idx) {
        double wire_length = RTUTIL.getManhattanDistance(first_coord, second_coord) / 1.0 / micron_dbu;
        routing_wire_length_map[first_layer_idx] += wire_length;
        total_wire_length += wire_length;
      } else {
        RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
        for (int32_t layer_idx = first_layer_idx; layer_idx < second_layer_idx; layer_idx++) {
          cut_via_num_map[layer_via_master_list[layer_idx].front().get_cut_layer_idx()]++;
          total_via_num++;
        }
      }
    }
  }
  for (auto& [net_idx, patch_list] : dr_model.get_net_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      routing_patch_num_map[patch.get_layer_idx()]++;
      total_patch_num++;
    }
  }
  for (Violation& violation : dr_model.get_route_violation_list()) {
    routing_violation_num_map[violation.get_violation_shape().get_layer_idx()]++;
    total_violation_num++;
  }
  if (enable_timing) {
    std::vector<std::map<std::string, std::vector<LayerCoord>>> real_pin_coord_map_list;
    real_pin_coord_map_list.resize(dr_net_list.size());
    std::vector<std::vector<Segment<LayerCoord>>> routing_segment_list_list;
    routing_segment_list_list.resize(dr_net_list.size());
    for (DRNet& dr_net : dr_net_list) {
      for (DRPin& dr_pin : dr_net.get_dr_pin_list()) {
        real_pin_coord_map_list[dr_net.get_net_idx()][dr_pin.get_pin_name()].push_back(dr_pin.get_access_point().getRealLayerCoord());
      }
    }
    for (auto& [net_idx, segment_list] : dr_model.get_net_detailed_result_map()) {
      for (Segment<LayerCoord>& segment : segment_list) {
        routing_segment_list_list[net_idx].emplace_back(segment.get_first(), segment.get_second());
      }
    }
    RTI.updateTiming(real_pin_coord_map_list, routing_segment_list_list, clock_timing_map);
  }
}

void DetailedRouter::printSummary(DRModel& dr_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<CutLayer>& cut_layer_list = RTDM.getDatabase().get_cut_layer_list();
  Summary& summary = RTDM.getDatabase().get_summary();
  int32_t enable_timing = RTDM.getConfig().enable_timing;

  std::map<int32_t, double>& routing_wire_length_map = summary.iter_dr_summary_map[dr_model.get_iter()].routing_wire_length_map;
  double& total_wire_length = summary.iter_dr_summary_map[dr_model.get_iter()].total_wire_length;
  std::map<int32_t, int32_t>& cut_via_num_map = summary.iter_dr_summary_map[dr_model.get_iter()].cut_via_num_map;
  int32_t& total_via_num = summary.iter_dr_summary_map[dr_model.get_iter()].total_via_num;
  std::map<int32_t, int32_t>& routing_patch_num_map = summary.iter_dr_summary_map[dr_model.get_iter()].routing_patch_num_map;
  int32_t& total_patch_num = summary.iter_dr_summary_map[dr_model.get_iter()].total_patch_num;
  std::map<int32_t, int32_t>& routing_violation_num_map = summary.iter_dr_summary_map[dr_model.get_iter()].routing_violation_num_map;
  int32_t& total_violation_num = summary.iter_dr_summary_map[dr_model.get_iter()].total_violation_num;
  std::map<std::string, std::map<std::string, double>>& clock_timing_map = summary.iter_dr_summary_map[dr_model.get_iter()].clock_timing_map;

  fort::char_table routing_wire_length_map_table;
  {
    routing_wire_length_map_table.set_cell_text_align(fort::text_align::right);
    routing_wire_length_map_table << fort::header << "routing"
                                  << "wire_length"
                                  << "prop" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_wire_length_map_table << routing_layer.get_layer_name() << routing_wire_length_map[routing_layer.get_layer_idx()]
                                    << RTUTIL.getPercentage(routing_wire_length_map[routing_layer.get_layer_idx()], total_wire_length) << fort::endr;
    }
    routing_wire_length_map_table << fort::header << "Total" << total_wire_length << RTUTIL.getPercentage(total_wire_length, total_wire_length) << fort::endr;
  }
  fort::char_table cut_via_num_map_table;
  {
    cut_via_num_map_table.set_cell_text_align(fort::text_align::right);
    cut_via_num_map_table << fort::header << "cut"
                          << "#via"
                          << "prop" << fort::endr;
    for (CutLayer& cut_layer : cut_layer_list) {
      cut_via_num_map_table << cut_layer.get_layer_name() << cut_via_num_map[cut_layer.get_layer_idx()]
                            << RTUTIL.getPercentage(cut_via_num_map[cut_layer.get_layer_idx()], total_via_num) << fort::endr;
    }
    cut_via_num_map_table << fort::header << "Total" << total_via_num << RTUTIL.getPercentage(total_via_num, total_via_num) << fort::endr;
  }
  fort::char_table routing_patch_num_map_table;
  {
    routing_patch_num_map_table.set_cell_text_align(fort::text_align::right);
    routing_patch_num_map_table << fort::header << "routing"
                                << "#patch"
                                << "prop" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_patch_num_map_table << routing_layer.get_layer_name() << routing_patch_num_map[routing_layer.get_layer_idx()]
                                  << RTUTIL.getPercentage(routing_patch_num_map[routing_layer.get_layer_idx()], total_patch_num) << fort::endr;
    }
    routing_patch_num_map_table << fort::header << "Total" << total_patch_num << RTUTIL.getPercentage(total_patch_num, total_patch_num) << fort::endr;
  }
  fort::char_table routing_violation_num_map_table;
  {
    routing_violation_num_map_table.set_cell_text_align(fort::text_align::right);
    routing_violation_num_map_table << fort::header << "routing"
                                    << "#violation"
                                    << "prop" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_violation_num_map_table << routing_layer.get_layer_name() << routing_violation_num_map[routing_layer.get_layer_idx()]
                                      << RTUTIL.getPercentage(routing_violation_num_map[routing_layer.get_layer_idx()], total_violation_num) << fort::endr;
    }
    routing_violation_num_map_table << fort::header << "Total" << total_violation_num << RTUTIL.getPercentage(total_violation_num, total_violation_num)
                                    << fort::endr;
  }
  fort::char_table timing_table;
  timing_table.set_cell_text_align(fort::text_align::right);
  if (enable_timing) {
    timing_table << fort::header << "clock_name"
                 << "tns"
                 << "wns"
                 << "freq" << fort::endr;
    for (auto& [clock_name, timing_map] : clock_timing_map) {
      timing_table << clock_name << timing_map["TNS"] << timing_map["WNS"] << timing_map["Freq(MHz)"] << fort::endr;
    }
  }
  RTUTIL.printTableList({routing_wire_length_map_table, cut_via_num_map_table, routing_patch_num_map_table});
  RTUTIL.printTableList({routing_violation_num_map_table});
  RTUTIL.printTableList({timing_table});
}

void DetailedRouter::outputNetCSV(DRModel& dr_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  Die& die = RTDM.getDatabase().get_die();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::string& dr_temp_directory_path = RTDM.getConfig().dr_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  std::vector<GridMap<std::set<int32_t>>> layer_net_map(routing_layer_list.size());
  for (GridMap<std::set<int32_t>>& net_map : layer_net_map) {
    net_map.init(gcell_map.get_x_size(), gcell_map.get_y_size());
  }
  for (auto& [net_idx, segment_list] : dr_model.get_net_detailed_result_map()) {
    for (Segment<LayerCoord>& segment : segment_list) {
      int32_t first_layer_idx = segment.get_first().get_layer_idx();
      int32_t second_layer_idx = segment.get_second().get_layer_idx();
      RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
        PlanarRect real_rect = RTUTIL.getEnlargedRect(net_shape, detection_distance);
        if (!RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
          continue;
        }
        PlanarRect grid_rect = RTUTIL.getClosedGCellGridRect(RTUTIL.getRegularRect(real_rect, die.get_real_rect()), gcell_axis);
        for (int32_t x = grid_rect.get_ll_x(); x <= grid_rect.get_ur_x(); x++) {
          for (int32_t y = grid_rect.get_ll_y(); y <= grid_rect.get_ur_y(); y++) {
            for (int32_t layer_idx = first_layer_idx; layer_idx <= second_layer_idx; layer_idx++) {
              layer_net_map[layer_idx][x][y].insert(net_idx);
            }
          }
        }
      }
    }
  }
  for (auto& [net_idx, patch_list] : dr_model.get_net_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      PlanarRect real_rect = RTUTIL.getEnlargedRect(patch.get_real_rect(), detection_distance);
      if (!RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
        continue;
      }
      PlanarRect grid_rect = RTUTIL.getClosedGCellGridRect(RTUTIL.getRegularRect(real_rect, die.get_real_rect()), gcell_axis);
      for (int32_t x = grid_rect.get_ll_x(); x <= grid_rect.get_ur_x(); x++) {
        for (int32_t y = grid_rect.get_ll_y(); y <= grid_rect.get_ur_y(); y++) {
          layer_net_map[patch.get_layer_idx()][x][y].insert(net_idx);
        }
      }
    }
  }
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::ofstream* net_csv_file
        = RTUTIL.getOutputFileStream(RTUTIL.getString(dr_temp_directory_path, "net_map_", routing_layer.get_layer_name(), "_", dr_model.get_iter(), ".csv"));
    GridMap<std::set<int32_t>>& net_map = layer_net_map[routing_layer.get_layer_idx()];
    for (int32_t y = net_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < net_map.get_x_size(); x++) {
        RTUTIL.pushStream(net_csv_file, net_map[x][y].size(), ",");
      }
      RTUTIL.pushStream(net_csv_file, "\n");
    }
    RTUTIL.closeFileStream(net_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DetailedRouter::outputViolationCSV(DRModel& dr_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::string& dr_temp_directory_path = RTDM.getConfig().dr_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<GridMap<int32_t>> layer_violation_map;
  layer_violation_map.resize(routing_layer_list.size());
  for (GridMap<int32_t>& violation_map : layer_violation_map) {
    violation_map.init(gcell_map.get_x_size(), gcell_map.get_y_size());
  }
  for (Violation& violation : dr_model.get_route_violation_list()) {
    EXTLayerRect& violation_shape = violation.get_violation_shape();
    PlanarRect& grid_rect = violation_shape.get_grid_rect();
    for (int32_t x = grid_rect.get_ll_x(); x <= grid_rect.get_ur_x(); x++) {
      for (int32_t y = grid_rect.get_ll_y(); y <= grid_rect.get_ur_y(); y++) {
        layer_violation_map[violation_shape.get_layer_idx()][x][y]++;
      }
    }
  }
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::ofstream* violation_csv_file = RTUTIL.getOutputFileStream(
        RTUTIL.getString(dr_temp_directory_path, "violation_map_", routing_layer.get_layer_name(), "_", dr_model.get_iter(), ".csv"));
    GridMap<int32_t>& violation_map = layer_violation_map[routing_layer.get_layer_idx()];
    for (int32_t y = violation_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < violation_map.get_x_size(); x++) {
        RTUTIL.pushStream(violation_csv_file, violation_map[x][y], ",");
      }
      RTUTIL.pushStream(violation_csv_file, "\n");
    }
    RTUTIL.closeFileStream(violation_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

#endif

#if 1  // debug

void DetailedRouter::debugPlotDRModel(DRModel& dr_model, std::string flag)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& dr_temp_directory_path = RTDM.getConfig().dr_temp_directory_path;

  int32_t point_size = 5;

  GPGDS gp_gds;

  // base_region
  {
    GPStruct base_region_struct("base_region");
    GPBoundary gp_boundary;
    gp_boundary.set_layer_idx(0);
    gp_boundary.set_data_type(0);
    gp_boundary.set_rect(die.get_real_rect());
    base_region_struct.push(gp_boundary);
    gp_gds.addStruct(base_region_struct);
  }

  // gcell_axis
  {
    GPStruct gcell_axis_struct("gcell_axis");
    std::vector<int32_t> gcell_x_list = RTUTIL.getScaleList(die.get_real_ll_x(), die.get_real_ur_x(), gcell_axis.get_x_grid_list());
    std::vector<int32_t> gcell_y_list = RTUTIL.getScaleList(die.get_real_ll_y(), die.get_real_ur_y(), gcell_axis.get_y_grid_list());
    for (int32_t x : gcell_x_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(x, die.get_real_ll_y(), x, die.get_real_ur_y());
      gcell_axis_struct.push(gp_path);
    }
    for (int32_t y : gcell_y_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(die.get_real_ll_x(), y, die.get_real_ur_x(), y);
      gcell_axis_struct.push(gp_path);
    }
    gp_gds.addStruct(gcell_axis_struct);
  }

  // track_axis_struct
  {
    GPStruct track_axis_struct("track_axis_struct");
    for (RoutingLayer& routing_layer : routing_layer_list) {
      std::vector<int32_t> x_list = RTUTIL.getScaleList(die.get_real_ll_x(), die.get_real_ur_x(), routing_layer.getXTrackGridList());
      std::vector<int32_t> y_list = RTUTIL.getScaleList(die.get_real_ll_y(), die.get_real_ur_y(), routing_layer.getYTrackGridList());
      for (int32_t x : x_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(x, die.get_real_ll_y(), x, die.get_real_ur_y());
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
        track_axis_struct.push(gp_path);
      }
      for (int32_t y : y_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(die.get_real_ll_x(), y, die.get_real_ur_x(), y);
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
        track_axis_struct.push(gp_path);
      }
    }
    gp_gds.addStruct(track_axis_struct);
  }

  // fixed_rect
  auto& type_layer_fixed_rect_rtree_map = RTDM.getDatabase().get_type_layer_fixed_rect_rtree_map();
  for (bool is_routing : {false, true}) {
    for (auto& [layer_idx, fixed_rect_rtree] : type_layer_fixed_rect_rtree_map[is_routing]) {
      std::map<int32_t, GPStruct> net_fixed_rect_struct_map;
      for (const auto& [rect, net_fixed_rect] : fixed_rect_rtree) {
        auto [net_idx, fixed_rect] = net_fixed_rect;
        auto struct_iter = net_fixed_rect_struct_map.try_emplace(net_idx, RTUTIL.getString("fixed_rect(net_", net_idx, ")")).first;
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
        gp_boundary.set_rect(fixed_rect->get_real_rect());
        gp_boundary.set_layer_idx(is_routing ? RTGP.getGDSIdxByRouting(layer_idx) : RTGP.getGDSIdxByCut(layer_idx));
        struct_iter->second.push(gp_boundary);
      }
      for (auto& [net_idx, fixed_rect_struct] : net_fixed_rect_struct_map) {
        gp_gds.addStruct(fixed_rect_struct);
      }
    }
  }

  // access_point
  for (auto& [net_idx, access_point_set] : RTDM.getNetAccessPointMap(die)) {
    GPStruct access_point_struct(RTUTIL.getString("access_point(net_", net_idx, ")"));
    for (AccessPoint* access_point : access_point_set) {
      int32_t x = access_point->get_real_x();
      int32_t y = access_point->get_real_y();

      GPBoundary access_point_boundary;
      access_point_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(access_point->get_layer_idx()));
      access_point_boundary.set_data_type(static_cast<int32_t>(GPDataType::kAccessPoint));
      access_point_boundary.set_rect(x - point_size, y - point_size, x + point_size, y + point_size);
      access_point_struct.push(access_point_boundary);
    }
    gp_gds.addStruct(access_point_struct);
  }

  // routing result
  for (auto& [net_idx, segment_set] : RTDM.getNetGlobalResultMap(die)) {
    GPStruct global_result_struct(RTUTIL.getString("global_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      for (NetShape& net_shape : RTDM.getNetGlobalShapeList(net_idx, *segment)) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kGlobalPath));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        global_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(global_result_struct);
  }

  // routing result
  for (auto& [net_idx, segment_list] : dr_model.get_net_detailed_result_map()) {
    GPStruct detailed_result_struct(RTUTIL.getString("detailed_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>& segment : segment_list) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kDetailedPath));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        detailed_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(detailed_result_struct);
  }

  // routing patch
  for (auto& [net_idx, patch_list] : dr_model.get_net_detailed_patch_map()) {
    GPStruct detailed_patch_struct(RTUTIL.getString("detailed_patch(net_", net_idx, ")"));
    for (EXTLayerRect& patch : patch_list) {
      GPBoundary gp_boundary;
      gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kPatch));
      gp_boundary.set_rect(patch.get_real_rect());
      gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(patch.get_layer_idx()));
      detailed_patch_struct.push(gp_boundary);
    }
    gp_gds.addStruct(detailed_patch_struct);
  }

  // violation
  {
    for (Violation& violation : dr_model.get_route_violation_list()) {
      GPStruct violation_struct(RTUTIL.getString("violation_", GetViolationTypeName()(violation.get_violation_type())));
      EXTLayerRect& violation_shape = violation.get_violation_shape();

      GPBoundary gp_boundary;
      gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kRouteViolation));
      gp_boundary.set_rect(violation_shape.get_real_rect());
      if (violation.get_is_routing()) {
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(violation_shape.get_layer_idx()));
      } else {
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(violation_shape.get_layer_idx()));
      }
      violation_struct.push(gp_boundary);
      gp_gds.addStruct(violation_struct);
    }
  }

  std::string gds_file_path = RTUTIL.getString(dr_temp_directory_path, flag, "_dr_model.gds");
  RTGP.plot(gp_gds, gds_file_path);
}

void DetailedRouter::debugCheckDRBox(DRBox& dr_box)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  DRBoxId& dr_box_id = dr_box.get_dr_box_id();
  if (dr_box_id.get_x() < 0 || dr_box_id.get_y() < 0) {
    RTLOG.error(Loc::current(), "The grid coord is illegal!");
  }

  std::vector<GridMap<DRNode>>& layer_node_map = dr_box.get_layer_node_map();
  for (GridMap<DRNode>& dr_node_map : layer_node_map) {
    for (int32_t x = 0; x < dr_node_map.get_x_size(); x++) {
      for (int32_t y = 0; y < dr_node_map.get_y_size(); y++) {
        DRNode& dr_node = dr_node_map[x][y];
        if (!RTUTIL.isInside(dr_box.get_box_rect().get_real_rect(), dr_node.get_planar_coord())) {
          RTLOG.error(Loc::current(), "The dr_node is out of box!");
        }
        for (Orientation orient : DRNode::kOrientationList) {
          DRNode* neighbor = dr_node.getNeighborNode(orient);
          if (neighbor == nullptr) {
            continue;
          }
          Orientation opposite_orient = RTUTIL.getOppositeOrientation(orient);
          if (neighbor->getNeighborNode(opposite_orient) != &dr_node) {
            RTLOG.error(Loc::current(), "The dr_node neighbor is not bidirectional!");
          }
          if (RTUTIL.getOrientation(LayerCoord(dr_node), LayerCoord(*neighbor)) == orient) {
            continue;
          }
          RTLOG.error(Loc::current(), "The neighbor orient is different with real region!");
        }
      }
    }
  }

  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    if (dr_task->get_net_idx() < 0) {
      RTLOG.error(Loc::current(), "The idx of origin net is illegal!");
    }
    for (DRGroup& dr_group : dr_task->get_dr_group_list()) {
      if (dr_group.get_coord_direction_map().empty()) {
        RTLOG.error(Loc::current(), "The coord_direction_map is empty!");
      }
      for (auto& [coord, _] : dr_group.get_coord_direction_map()) {
        int32_t layer_idx = coord.get_layer_idx();
        if (routing_layer_list.back().get_layer_idx() < layer_idx || layer_idx < routing_layer_list.front().get_layer_idx()) {
          RTLOG.error(Loc::current(), "The layer idx of group coord is illegal!");
        }
        if (!RTUTIL.existTrackGrid(coord, dr_box.get_box_track_axis())) {
          RTLOG.error(Loc::current(), "There is no grid coord for real coord(", coord.get_x(), ",", coord.get_y(), ")!");
        }
        PlanarCoord grid_coord = RTUTIL.getTrackGrid(coord, dr_box.get_box_track_axis());
        DRNode& dr_node = layer_node_map[layer_idx][grid_coord.get_x()][grid_coord.get_y()];
        if (dr_node.get_neighbor_node_num() == 0) {
          RTLOG.error(Loc::current(), "The neighbor of group coord (", coord.get_x(), ",", coord.get_y(), ",", layer_idx, ") is empty in box(",
                      dr_box_id.get_x(), ",", dr_box_id.get_y(), ")");
        }
        if (RTUTIL.isInside(dr_box.get_box_rect().get_real_rect(), coord)) {
          continue;
        }
        RTLOG.error(Loc::current(), "The coord (", coord.get_x(), ",", coord.get_y(), ") is out of box!");
      }
    }
  }
}

void DetailedRouter::debugPlotDRBox(DRBox& dr_box, std::string flag)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& dr_temp_directory_path = RTDM.getConfig().dr_temp_directory_path;

  PlanarRect box_real_rect = dr_box.get_box_rect().get_real_rect();

  int32_t point_size = 5;

  GPGDS gp_gds;

  // base_region
  {
    GPStruct base_region_struct("base_region");
    GPBoundary gp_boundary;
    gp_boundary.set_layer_idx(0);
    gp_boundary.set_data_type(0);
    gp_boundary.set_rect(box_real_rect);
    base_region_struct.push(gp_boundary);
    gp_gds.addStruct(base_region_struct);
  }

  // gcell_axis
  {
    GPStruct gcell_axis_struct("gcell_axis");
    for (int32_t x : RTUTIL.getScaleList(box_real_rect.get_ll_x(), box_real_rect.get_ur_x(), gcell_axis.get_x_grid_list())) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(x, box_real_rect.get_ll_y(), x, box_real_rect.get_ur_y());
      gcell_axis_struct.push(gp_path);
    }
    for (int32_t y : RTUTIL.getScaleList(box_real_rect.get_ll_y(), box_real_rect.get_ur_y(), gcell_axis.get_y_grid_list())) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(box_real_rect.get_ll_x(), y, box_real_rect.get_ur_x(), y);
      gcell_axis_struct.push(gp_path);
    }
    gp_gds.addStruct(gcell_axis_struct);
  }

  // box_track_axis
  {
    GPStruct box_track_axis_struct("box_track_axis");
    PlanarCoord& real_ll = box_real_rect.get_ll();
    PlanarCoord& real_ur = box_real_rect.get_ur();
    ScaleAxis& box_track_axis = dr_box.get_box_track_axis();
    std::vector<int32_t> x_list = RTUTIL.getScaleList(real_ll.get_x(), real_ur.get_x(), box_track_axis.get_x_grid_list());
    std::vector<int32_t> y_list = RTUTIL.getScaleList(real_ll.get_y(), real_ur.get_y(), box_track_axis.get_y_grid_list());
    for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(routing_layer_list.size()); layer_idx++) {
      for (int32_t x : x_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(x, real_ll.get_y(), x, real_ur.get_y());
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
        box_track_axis_struct.push(gp_path);
      }
      for (int32_t y : y_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(real_ll.get_x(), y, real_ur.get_x(), y);
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
        box_track_axis_struct.push(gp_path);
      }
    }
    gp_gds.addStruct(box_track_axis_struct);
  }

  // fixed_rect
  for (auto& [is_routing, layer_net_rect_map] : dr_box.get_type_layer_net_fixed_rect_map()) {
    for (auto& [layer_idx, net_rect_map] : layer_net_rect_map) {
      for (auto& [net_idx, rect_set] : net_rect_map) {
        GPStruct fixed_rect_struct(RTUTIL.getString("fixed_rect(net_", net_idx, ")"));
        for (EXTLayerRect* rect : rect_set) {
          GPBoundary gp_boundary;
          gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
          gp_boundary.set_rect(rect->get_real_rect());
          if (is_routing) {
            gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          } else {
            gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(layer_idx));
          }
          fixed_rect_struct.push(gp_boundary);
        }
        gp_gds.addStruct(fixed_rect_struct);
      }
    }
  }

  // access_point
  for (auto& [net_idx, access_point_set] : dr_box.get_net_access_point_map()) {
    GPStruct access_point_struct(RTUTIL.getString("access_point(net_", net_idx, ")"));
    for (AccessPoint* access_point : access_point_set) {
      int32_t x = access_point->get_real_x();
      int32_t y = access_point->get_real_y();

      GPBoundary access_point_boundary;
      access_point_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(access_point->get_layer_idx()));
      access_point_boundary.set_data_type(static_cast<int32_t>(GPDataType::kAccessPoint));
      access_point_boundary.set_rect(x - point_size, y - point_size, x + point_size, y + point_size);
      access_point_struct.push(access_point_boundary);
    }
    gp_gds.addStruct(access_point_struct);
  }

  // net_detailed_result
  for (auto& [net_idx, segment_list] : dr_box.get_net_detailed_result_map()) {
    GPStruct detailed_result_struct(RTUTIL.getString("detailed_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>* segment : segment_list) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, *segment)) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        detailed_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(detailed_result_struct);
  }

  // net_detailed_patch
  for (auto& [net_idx, patch_list] : dr_box.get_net_detailed_patch_map()) {
    GPStruct detailed_patch_struct(RTUTIL.getString("detailed_patch(net_", net_idx, ")"));
    for (EXTLayerRect* patch : patch_list) {
      GPBoundary gp_boundary;
      gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
      gp_boundary.set_rect(patch->get_real_rect());
      gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(patch->get_layer_idx()));
      detailed_patch_struct.push(gp_boundary);
    }
    gp_gds.addStruct(detailed_patch_struct);
  }

  // layer_node_map
  {
    std::vector<GridMap<DRNode>>& layer_node_map = dr_box.get_layer_node_map();
    // dr_node_map
    {
      GPStruct dr_node_map_struct("dr_node_map");
      for (GridMap<DRNode>& dr_node_map : layer_node_map) {
        for (int32_t grid_x = 0; grid_x < dr_node_map.get_x_size(); grid_x++) {
          for (int32_t grid_y = 0; grid_y < dr_node_map.get_y_size(); grid_y++) {
            DRNode& dr_node = dr_node_map[grid_x][grid_y];
            PlanarRect real_rect = RTUTIL.getEnlargedRect(dr_node.get_planar_coord(), point_size);
            int32_t y_reduced_span = std::max(1, real_rect.getYSpan() / 12);
            int32_t y = real_rect.get_ur_y();

            GPBoundary gp_boundary;
            switch (dr_node.get_state()) {
              case DRNodeState::kNone:
                gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kNone));
                break;
              case DRNodeState::kOpen:
                gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kOpen));
                break;
              case DRNodeState::kClose:
                gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kClose));
                break;
              default:
                RTLOG.error(Loc::current(), "The type is error!");
                break;
            }
            gp_boundary.set_rect(real_rect);
            gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
            dr_node_map_struct.push(gp_boundary);

            y -= y_reduced_span;
            GPText gp_text_node_real_coord;
            gp_text_node_real_coord.set_coord(real_rect.get_ll_x(), y);
            gp_text_node_real_coord.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            gp_text_node_real_coord.set_message(RTUTIL.getString("(", dr_node.get_x(), " , ", dr_node.get_y(), " , ", dr_node.get_layer_idx(), ")"));
            gp_text_node_real_coord.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
            gp_text_node_real_coord.set_presentation(GPTextPresentation::kLeftMiddle);
            dr_node_map_struct.push(gp_text_node_real_coord);

            y -= y_reduced_span;
            GPText gp_text_node_grid_coord;
            gp_text_node_grid_coord.set_coord(real_rect.get_ll_x(), y);
            gp_text_node_grid_coord.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            gp_text_node_grid_coord.set_message(RTUTIL.getString("(", grid_x, " , ", grid_y, " , ", dr_node.get_layer_idx(), ")"));
            gp_text_node_grid_coord.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
            gp_text_node_grid_coord.set_presentation(GPTextPresentation::kLeftMiddle);
            dr_node_map_struct.push(gp_text_node_grid_coord);

            y -= y_reduced_span;
            GPText gp_text_orient_fixed_rect_map;
            gp_text_orient_fixed_rect_map.set_coord(real_rect.get_ll_x(), y);
            gp_text_orient_fixed_rect_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            gp_text_orient_fixed_rect_map.set_message("orient_fixed_rect_map: ");
            gp_text_orient_fixed_rect_map.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
            gp_text_orient_fixed_rect_map.set_presentation(GPTextPresentation::kLeftMiddle);
            dr_node_map_struct.push(gp_text_orient_fixed_rect_map);

            if (!dr_node.get_orient_fixed_rect_list().empty()) {
              y -= y_reduced_span;
              GPText gp_text_orient_fixed_rect_map_info;
              gp_text_orient_fixed_rect_map_info.set_coord(real_rect.get_ll_x(), y);
              gp_text_orient_fixed_rect_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
              std::string orient_fixed_rect_map_info_message = "--";
              for (auto& [orient, net_idx] : dr_node.get_orient_fixed_rect_list()) {
                orient_fixed_rect_map_info_message += RTUTIL.getString("(", GetOrientationName()(orient), ",", net_idx, ")");
              }
              gp_text_orient_fixed_rect_map_info.set_message(orient_fixed_rect_map_info_message);
              gp_text_orient_fixed_rect_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
              gp_text_orient_fixed_rect_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
              dr_node_map_struct.push(gp_text_orient_fixed_rect_map_info);
            }

            y -= y_reduced_span;
            GPText gp_text_orient_routed_rect_map;
            gp_text_orient_routed_rect_map.set_coord(real_rect.get_ll_x(), y);
            gp_text_orient_routed_rect_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            gp_text_orient_routed_rect_map.set_message("orient_routed_rect_map: ");
            gp_text_orient_routed_rect_map.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
            gp_text_orient_routed_rect_map.set_presentation(GPTextPresentation::kLeftMiddle);
            dr_node_map_struct.push(gp_text_orient_routed_rect_map);

            if (!dr_node.get_orient_routed_rect_list().empty()) {
              y -= y_reduced_span;
              GPText gp_text_orient_routed_rect_map_info;
              gp_text_orient_routed_rect_map_info.set_coord(real_rect.get_ll_x(), y);
              gp_text_orient_routed_rect_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
              std::string orient_routed_rect_map_info_message = "--";
              for (auto& [orient, net_idx] : dr_node.get_orient_routed_rect_list()) {
                orient_routed_rect_map_info_message += RTUTIL.getString("(", GetOrientationName()(orient), ",", net_idx, ")");
              }
              gp_text_orient_routed_rect_map_info.set_message(orient_routed_rect_map_info_message);
              gp_text_orient_routed_rect_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
              gp_text_orient_routed_rect_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
              dr_node_map_struct.push(gp_text_orient_routed_rect_map_info);
            }

            y -= y_reduced_span;
            GPText gp_text_orient_violation_number_map;
            gp_text_orient_violation_number_map.set_coord(real_rect.get_ll_x(), y);
            gp_text_orient_violation_number_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            gp_text_orient_violation_number_map.set_message("orient_violation_number_map: ");
            gp_text_orient_violation_number_map.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
            gp_text_orient_violation_number_map.set_presentation(GPTextPresentation::kLeftMiddle);
            dr_node_map_struct.push(gp_text_orient_violation_number_map);

            if (dr_node.hasViolation()) {
              y -= y_reduced_span;
              GPText gp_text_orient_violation_number_map_info;
              gp_text_orient_violation_number_map_info.set_coord(real_rect.get_ll_x(), y);
              gp_text_orient_violation_number_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
              std::string orient_violation_number_map_info_message = "--";
              for (Orientation orient : DRNode::kOrientationList) {
                int32_t violation_number = dr_node.getViolationNumber(orient);
                if (violation_number > 0) {
                  orient_violation_number_map_info_message += RTUTIL.getString("(", GetOrientationName()(orient), ",", violation_number != 0, ")");
                }
              }
              gp_text_orient_violation_number_map_info.set_message(orient_violation_number_map_info_message);
              gp_text_orient_violation_number_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
              gp_text_orient_violation_number_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
              dr_node_map_struct.push(gp_text_orient_violation_number_map_info);
            }

            y -= y_reduced_span;
            GPText gp_text_direction_set;
            gp_text_direction_set.set_coord(real_rect.get_ll_x(), y);
            gp_text_direction_set.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            gp_text_direction_set.set_message("direction_set: ");
            gp_text_direction_set.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
            gp_text_direction_set.set_presentation(GPTextPresentation::kLeftMiddle);
            dr_node_map_struct.push(gp_text_direction_set);

            if (dr_node.hasDirection()) {
              y -= y_reduced_span;
              GPText gp_text_direction_set_info;
              gp_text_direction_set_info.set_coord(real_rect.get_ll_x(), y);
              gp_text_direction_set_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
              std::string direction_set_info_message = "--";
              for (Direction direction : {Direction::kNone, Direction::kHorizontal, Direction::kVertical, Direction::kOblique, Direction::kProximal}) {
                if (dr_node.hasDirection(direction)) {
                  direction_set_info_message += RTUTIL.getString("(", GetDirectionName()(direction), ")");
                }
              }
              gp_text_direction_set_info.set_message(direction_set_info_message);
              gp_text_direction_set_info.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
              gp_text_direction_set_info.set_presentation(GPTextPresentation::kLeftMiddle);
              dr_node_map_struct.push(gp_text_direction_set_info);
            }
          }
        }
      }
      gp_gds.addStruct(dr_node_map_struct);
    }

    // neighbor_map
    {
      GPStruct neighbor_map_struct("neighbor_map");
      for (GridMap<DRNode>& dr_node_map : layer_node_map) {
        for (int32_t grid_x = 0; grid_x < dr_node_map.get_x_size(); grid_x++) {
          for (int32_t grid_y = 0; grid_y < dr_node_map.get_y_size(); grid_y++) {
            DRNode& dr_node = dr_node_map[grid_x][grid_y];
            PlanarRect real_rect = RTUTIL.getEnlargedRect(dr_node.get_planar_coord(), point_size);

            int32_t ll_x = real_rect.get_ll_x();
            int32_t ll_y = real_rect.get_ll_y();
            int32_t ur_x = real_rect.get_ur_x();
            int32_t ur_y = real_rect.get_ur_y();
            int32_t mid_x = (ll_x + ur_x) / 2;
            int32_t mid_y = (ll_y + ur_y) / 2;
            int32_t x_reduced_span = (ur_x - ll_x) / 4;
            int32_t y_reduced_span = (ur_y - ll_y) / 4;

            for (Orientation orientation : DRNode::kOrientationList) {
              DRNode* neighbor_node = dr_node.getNeighborNode(orientation);
              if (neighbor_node == nullptr) {
                continue;
              }
              GPPath gp_path;
              switch (orientation) {
                case Orientation::kEast:
                  gp_path.set_segment(ur_x - x_reduced_span, mid_y, ur_x, mid_y);
                  break;
                case Orientation::kSouth:
                  gp_path.set_segment(mid_x, ll_y, mid_x, ll_y + y_reduced_span);
                  break;
                case Orientation::kWest:
                  gp_path.set_segment(ll_x, mid_y, ll_x + x_reduced_span, mid_y);
                  break;
                case Orientation::kNorth:
                  gp_path.set_segment(mid_x, ur_y - y_reduced_span, mid_x, ur_y);
                  break;
                case Orientation::kAbove:
                  gp_path.set_segment(ur_x - x_reduced_span, ur_y - y_reduced_span, ur_x, ur_y);
                  break;
                case Orientation::kBelow:
                  gp_path.set_segment(ll_x, ll_y, ll_x + x_reduced_span, ll_y + y_reduced_span);
                  break;
                default:
                  RTLOG.error(Loc::current(), "The orientation is oblique!");
                  break;
              }
              gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(dr_node.get_layer_idx()));
              gp_path.set_width(std::min(x_reduced_span, y_reduced_span) / 2);
              gp_path.set_data_type(static_cast<int32_t>(GPDataType::kNeighbor));
              neighbor_map_struct.push(gp_path);
            }
          }
        }
      }
      gp_gds.addStruct(neighbor_map_struct);
    }
  }

  // layer_shadow_map
  {
    std::vector<DRShadow>& layer_shadow_map = dr_box.get_layer_shadow_map();
    for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(routing_layer_list.size()); layer_idx++) {
      DRShadow& dr_shadow = layer_shadow_map[layer_idx];

      for (auto& [net_idx, rect_set] : dr_shadow.get_net_fixed_rect_map()) {
        GPStruct fixed_rect_struct(RTUTIL.getString("shadow_fixed_rect(net_", net_idx, ")"));
        for (const PlanarRect& rect : rect_set) {
          GPBoundary gp_boundary;
          gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShadow));
          gp_boundary.set_rect(rect);
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          fixed_rect_struct.push(gp_boundary);
        }
        gp_gds.addStruct(fixed_rect_struct);
      }

      for (auto& [net_idx, rect_set] : dr_shadow.get_net_routed_rect_map()) {
        GPStruct routed_rect_struct(RTUTIL.getString("shadow_routed_rect(net_", net_idx, ")"));
        for (const PlanarRect& rect : rect_set) {
          GPBoundary gp_boundary;
          gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShadow));
          gp_boundary.set_rect(rect);
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          routed_rect_struct.push(gp_boundary);
        }
        gp_gds.addStruct(routed_rect_struct);
      }

      GPStruct violation_struct(RTUTIL.getString("shadow_violation"));
      for (const PlanarRect& rect : dr_shadow.get_violation_set()) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShadow));
        gp_boundary.set_rect(rect);
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
        violation_struct.push(gp_boundary);
      }
      gp_gds.addStruct(violation_struct);
    }
  }

  // task
  for (DRTask* dr_task : dr_box.get_dr_task_list()) {
    GPStruct task_struct(RTUTIL.getString("task(net_", dr_task->get_net_idx(), ")"));

    for (DRGroup& dr_group : dr_task->get_dr_group_list()) {
      for (auto& [coord, _] : dr_group.get_coord_direction_map()) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kKey));
        gp_boundary.set_rect(RTUTIL.getEnlargedRect(coord, point_size));
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(coord.get_layer_idx()));
        task_struct.push(gp_boundary);
      }
    }
    {
      // bounding_box
      GPBoundary gp_boundary;
      gp_boundary.set_layer_idx(0);
      gp_boundary.set_data_type(2);
      gp_boundary.set_rect(dr_task->get_bounding_box());
      task_struct.push(gp_boundary);
    }
    for (Segment<LayerCoord>& segment : dr_box.get_net_task_detailed_result_map()[dr_task->get_net_idx()]) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(dr_task->get_net_idx(), segment)) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kDetailedPath));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        task_struct.push(gp_boundary);
      }
    }
    for (EXTLayerRect& patch : dr_box.get_net_task_detailed_patch_map()[dr_task->get_net_idx()]) {
      GPBoundary gp_boundary;
      gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kPatch));
      gp_boundary.set_rect(patch.get_real_rect());
      gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(patch.get_layer_idx()));
      task_struct.push(gp_boundary);
    }
    gp_gds.addStruct(task_struct);
  }

  // violation
  {
    for (Violation& violation : dr_box.get_route_violation_list()) {
      GPStruct violation_struct(RTUTIL.getString("violation_", GetViolationTypeName()(violation.get_violation_type())));
      EXTLayerRect& violation_shape = violation.get_violation_shape();

      GPBoundary gp_boundary;
      gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kRouteViolation));
      gp_boundary.set_rect(violation_shape.get_real_rect());
      if (violation.get_is_routing()) {
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(violation_shape.get_layer_idx()));
      } else {
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(violation_shape.get_layer_idx()));
      }
      violation_struct.push(gp_boundary);
      gp_gds.addStruct(violation_struct);
    }
    for (Violation& violation : dr_box.get_patch_violation_list()) {
      GPStruct violation_struct(RTUTIL.getString("violation_", GetViolationTypeName()(violation.get_violation_type())));
      EXTLayerRect& violation_shape = violation.get_violation_shape();

      GPBoundary gp_boundary;
      gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kPatchViolation));
      gp_boundary.set_rect(violation_shape.get_real_rect());
      if (violation.get_is_routing()) {
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(violation_shape.get_layer_idx()));
      } else {
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(violation_shape.get_layer_idx()));
      }
      violation_struct.push(gp_boundary);
      gp_gds.addStruct(violation_struct);
    }
  }

  std::string gds_file_path
      = RTUTIL.getString(dr_temp_directory_path, flag, "_dr_box_", dr_box.get_dr_box_id().get_x(), "_", dr_box.get_dr_box_id().get_y(), ".gds");
  RTGP.plot(gp_gds, gds_file_path);
}

#endif

}  // namespace irt
