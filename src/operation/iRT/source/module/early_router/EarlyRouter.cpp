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
#include "EarlyRouter.hpp"

#include "ERStage.hpp"
#include "GDSPlotter.hpp"
#include "Monitor.hpp"
#include "RTInterface.hpp"
#include "TBTask.hpp"
#include "TOPOBuilder.hpp"
#include "Utility.hpp"

namespace irt {

// public

void EarlyRouter::initInst()
{
  if (_er_instance == nullptr) {
    _er_instance = new EarlyRouter();
  }
}

EarlyRouter& EarlyRouter::getInst()
{
  if (_er_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_er_instance;
}

void EarlyRouter::destroyInst()
{
  if (_er_instance != nullptr) {
    delete _er_instance;
    _er_instance = nullptr;
  }
}

// function

void EarlyRouter::route(std::map<std::string, std::any> config_map)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  ERModel er_model = initERModel();
  setERComParam(er_model, config_map);
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (output_inter_result) {
    outputGCellCSV(er_model);
  }
  // debugPlotERModel(er_model, "dm");
  initAccessPointList(er_model);
  buildConflictList(er_model);
  eliminateConflict(er_model);
  uploadAccessPoint(er_model);
  uploadAccessPatch(er_model);
  printAccessSummary(er_model);
  // debugPlotERModel(er_model, "pa");
  initLayerEdgeMap(er_model);
  buildSupplySchedule(er_model);
  analyzeSupply(er_model);
  printSupplySummary(er_model);
  // debugPlotERModel(er_model, "sa");
  if (er_model.get_er_com_param().get_stage() >= ERStage::kEgr2D) {
    buildPlanarEdgeMap(er_model);
    generateTopology(er_model);
    checkEdgeDemand(er_model, true);
    if (output_inter_result) {
      outputPlanarSupplyCSV(er_model);
      outputPlanarGuide(er_model);
      outputPlanarOverflowCSV(er_model);
    }
    printPlanarSummary(er_model);
    // debugPlotERModel(er_model, "tg");
  }
  if (er_model.get_er_com_param().get_stage() >= ERStage::kEgr3D) {
    clearLayerEdgeDemand(er_model);
    buildPlaneTree(er_model);
    assignLayer(er_model);
    checkEdgeDemand(er_model, false);
    if (output_inter_result) {
      outputLayerSupplyCSV(er_model);
      outputLayerGuide(er_model);
      outputLayerOverflowCSV(er_model);
    }
    printLayerSummary(er_model);
    // debugPlotERModel(er_model, "la");
  }
  if (er_model.get_er_com_param().get_stage() >= ERStage::kEdr) {
    std::vector<ERModel::GlobalResultRTree::value_type> value_list;
    for (auto& [net_idx, segment_list] : er_model.get_net_global_result_map()) {
      for (int32_t segment_idx = 0; segment_idx < static_cast<int32_t>(segment_list.size()); segment_idx++) {
        Segment<LayerCoord>& segment = segment_list[segment_idx];
        PlanarRect segment_rect(
            std::min(segment.get_first().get_x(), segment.get_second().get_x()), std::min(segment.get_first().get_y(), segment.get_second().get_y()),
            std::max(segment.get_first().get_x(), segment.get_second().get_x()), std::max(segment.get_first().get_y(), segment.get_second().get_y()));
        value_list.emplace_back(RTUTIL.convertToBGRectInt(segment_rect), std::make_pair(net_idx, segment_idx));
      }
    }
    er_model.get_global_result_rtree() = ERModel::GlobalResultRTree(value_list);
    initERPanelMap(er_model);
    buildPanelSchedule(er_model);
    assignTrack(er_model);
    printTrackSummary(er_model);
    // debugPlotERModel(er_model, "ta");
    initERBoxMap(er_model);
    buildBoxSchedule(er_model);
    routeDetailed(er_model);
    printDetailedSummary(er_model);
    updateNetResult(er_model);
    updateNetPatch(er_model);
    // debugPlotERModel(er_model, "dr");
  } else {
    cleanTempResult(er_model);
  }
  uploadERModel(er_model);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

EarlyRouter* EarlyRouter::_er_instance = nullptr;

ERModel EarlyRouter::initERModel()
{
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();

  ERModel er_model;
  er_model.set_er_net_list(convertToERNetList(net_list));
  er_model.get_net_global_result_map() = RTDM.getDatabase().get_net_global_result_map();
  er_model.get_net_detailed_result_map() = RTDM.getDatabase().get_net_detailed_result_map();
  er_model.get_net_detailed_patch_map() = RTDM.getDatabase().get_net_detailed_patch_map();
  return er_model;
}

void EarlyRouter::initLayerEdgeMap(ERModel& er_model)
{
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<GridMap<EREdge>>& layer_h_edge_map = er_model.get_layer_h_edge_map();
  std::vector<GridMap<EREdge>>& layer_v_edge_map = er_model.get_layer_v_edge_map();

  layer_h_edge_map.resize(routing_layer_list.size());
  layer_v_edge_map.resize(routing_layer_list.size());
#pragma omp parallel for
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(routing_layer_list.size()); layer_idx++) {
    layer_h_edge_map[layer_idx].init(std::max(0, gcell_map.get_x_size() - 1), gcell_map.get_y_size());
    layer_v_edge_map[layer_idx].init(gcell_map.get_x_size(), std::max(0, gcell_map.get_y_size() - 1));
  }
}

std::vector<ERNet> EarlyRouter::convertToERNetList(std::vector<Net>& net_list)
{
  std::vector<ERNet> er_net_list;
  er_net_list.reserve(net_list.size());
  for (size_t i = 0; i < net_list.size(); i++) {
    er_net_list.emplace_back(convertToERNet(net_list[i]));
  }
  return er_net_list;
}

ERNet EarlyRouter::convertToERNet(Net& net)
{
  ERNet er_net;
  er_net.set_origin_net(&net);
  er_net.set_net_idx(net.get_net_idx());
  er_net.set_connect_type(net.get_connect_type());
  for (Pin& pin : net.get_pin_list()) {
    er_net.get_er_pin_list().push_back(ERPin(pin));
  }
  er_net.set_bounding_box(net.get_bounding_box());
  return er_net;
}

void EarlyRouter::setERComParam(ERModel& er_model, std::map<std::string, std::any> config_map)
{
  // egr2D egr3D edr
  std::string stage_string = RTUTIL.getConfigValue<std::string>(config_map, "-stage", "null");
  if (stage_string != "egr2D" && stage_string != "egr3D" && stage_string != "edr") {
    RTLOG.error(Loc::current(), "Invalid value for '-stage': '" + stage_string + "'. Valid options are: 'egr2D', 'egr3D', 'edr'.");
  }
  // low high
  std::string resolve_congestion = RTUTIL.getConfigValue<std::string>(config_map, "-resolve_congestion", "null");
  if (resolve_congestion != "low" && resolve_congestion != "high") {
    RTLOG.error(Loc::current(), "Invalid value for '-resolve_congestion': '" + resolve_congestion + "'. Valid options are: 'low', 'high'.");
  }
  int32_t max_candidate_point_num = 10;
  int32_t topo_spilt_length = 10;
  int32_t expand_step_num = 5;
  int32_t expand_step_length = 2;
  double prefer_wire_unit = 1;
  double non_prefer_wire_unit = 2.5 * prefer_wire_unit;
  double via_unit = 2 * non_prefer_wire_unit;
  double overflow_unit = 4 * non_prefer_wire_unit;
  int32_t schedule_interval = 3;

  /**
   * stage, resolve_congestion, max_candidate_point_num, topo_spilt_length, expand_step_num, expand_step_length, via_unit, overflow_unit, schedule_interval
   */
  ERComParam er_com_param(GetERStageByName()(stage_string), resolve_congestion, max_candidate_point_num, topo_spilt_length, expand_step_num, expand_step_length,
                          via_unit, overflow_unit, schedule_interval);
  RTLOG.info(Loc::current(), "stage: ", GetERStageName()(er_com_param.get_stage()));
  RTLOG.info(Loc::current(), "resolve_congestion: ", er_com_param.get_resolve_congestion());
  RTLOG.info(Loc::current(), "max_candidate_point_num: ", er_com_param.get_max_candidate_point_num());
  RTLOG.info(Loc::current(), "topo_spilt_length: ", er_com_param.get_topo_spilt_length());
  RTLOG.info(Loc::current(), "expand_step_num: ", er_com_param.get_expand_step_num());
  RTLOG.info(Loc::current(), "expand_step_length: ", er_com_param.get_expand_step_length());
  RTLOG.info(Loc::current(), "via_unit: ", er_com_param.get_via_unit());
  RTLOG.info(Loc::current(), "overflow_unit: ", er_com_param.get_overflow_unit());
  RTLOG.info(Loc::current(), "schedule_interval: ", er_com_param.get_schedule_interval());

  er_model.set_er_com_param(er_com_param);
}

void EarlyRouter::initAccessPointList(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();

  std::vector<ERNet>& er_net_list = er_model.get_er_net_list();
  std::vector<ERModel::AccessPointRTree::value_type> value_list;

  for (ERNet& er_net : er_net_list) {
    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      std::vector<std::pair<int32_t, std::vector<EXTLayerRect>>> routing_pin_shape_list;
      {
        std::map<int32_t, std::vector<EXTLayerRect>> routing_pin_shape_map;
        for (EXTLayerRect& routing_shape : er_pin.get_routing_shape_list()) {
          routing_pin_shape_map[routing_shape.get_layer_idx()].emplace_back(routing_shape);
        }
        for (auto& [routing_layer_idx, pin_shape_list] : routing_pin_shape_map) {
          routing_pin_shape_list.emplace_back(routing_layer_idx, pin_shape_list);
        }
        if (er_pin.get_is_core()) {
          std::sort(
              routing_pin_shape_list.begin(), routing_pin_shape_list.end(),
              [](const std::pair<int32_t, std::vector<EXTLayerRect>>& a, const std::pair<int32_t, std::vector<EXTLayerRect>>& b) { return a.first > b.first; });
        } else {
          std::sort(routing_pin_shape_list.begin(), routing_pin_shape_list.end(),
                    [](const std::pair<int32_t, std::vector<EXTLayerRect>>& a, const std::pair<int32_t, std::vector<EXTLayerRect>>& b) {
                      return (a.first % 2 != 0 && b.first % 2 == 0) || (a.first % 2 == b.first % 2 && a.first > b.first);
                    });
        }
      }
      if (routing_pin_shape_list.empty()) {
        RTLOG.error(Loc::current(), "The routing_pin_shape_list is empty!");
      }
      for (LayerCoord access_coord : getAccessCoordList(er_model, routing_pin_shape_list.front().second)) {
        er_pin.get_access_point_list().emplace_back(er_pin.get_pin_idx(), access_coord);
      }
    }

    std::vector<PlanarCoord> coord_list;
    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      for (AccessPoint& access_point : er_pin.get_access_point_list()) {
        coord_list.push_back(access_point.get_real_coord());
      }
    }
    BoundingBox& bounding_box = er_net.get_bounding_box();
    bounding_box.set_real_rect(RTUTIL.getBoundingBox(coord_list));
    bounding_box.set_grid_rect(RTUTIL.getOpenGCellGridRect(bounding_box.get_real_rect(), gcell_axis));
    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      for (AccessPoint& access_point : er_pin.get_access_point_list()) {
        access_point.set_grid_coord(RTUTIL.getGCellGridCoordByBBox(access_point.get_real_coord(), gcell_axis, bounding_box));
        value_list.emplace_back(RTUTIL.convertToBGRectInt(PlanarRect(access_point.get_real_coord(), access_point.get_real_coord())),
                                std::make_pair(er_net.get_net_idx(), &access_point));
      }
    }
  }
  er_model.get_access_point_rtree() = ERModel::AccessPointRTree(value_list);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

std::vector<LayerCoord> EarlyRouter::getAccessCoordList(ERModel& er_model, std::vector<EXTLayerRect>& pin_shape_list)
{
  Die& die = RTDM.getDatabase().get_die();
  int32_t manufacture_grid = RTDM.getDatabase().get_manufacture_grid();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::map<int32_t, PlanarRect>& layer_enclosure_map = RTDM.getDatabase().get_layer_enclosure_map();

  int32_t curr_layer_idx;
  {
    for (EXTLayerRect& pin_shape : pin_shape_list) {
      if (pin_shape_list.front().get_layer_idx() != pin_shape.get_layer_idx()) {
        RTLOG.error(Loc::current(), "The pin_shape_list is not on the same layer!");
      }
    }
    curr_layer_idx = pin_shape_list.front().get_layer_idx();
  }
  std::vector<PlanarRect> legal_rect_list;
  {
    std::vector<PlanarRect> origin_pin_shape_list;
    for (EXTLayerRect& pin_shape : pin_shape_list) {
      origin_pin_shape_list.push_back(pin_shape.get_real_rect());
    }
    std::vector<PlanarRect> shrinked_rect_list;
    {
      PlanarRect& enclosure = layer_enclosure_map[curr_layer_idx];
      int32_t enclosure_half_x_span = enclosure.getXSpan() / 2;
      int32_t enclosure_half_y_span = enclosure.getYSpan() / 2;
      int32_t half_min_width = routing_layer_list[curr_layer_idx].get_min_width() / 2;
      int32_t shrinked_x_size = std::max(half_min_width, enclosure_half_x_span);
      int32_t shrinked_y_size = std::max(half_min_width, enclosure_half_y_span);
      for (PlanarRect& real_rect :
           RTUTIL.getClosedShrinkedRectListByBoost(origin_pin_shape_list, shrinked_x_size, shrinked_y_size, shrinked_x_size, shrinked_y_size)) {
        shrinked_rect_list.push_back(real_rect);
      }
    }
    if (shrinked_rect_list.empty()) {
      legal_rect_list = origin_pin_shape_list;
    } else {
      legal_rect_list = shrinked_rect_list;
    }
  }
  std::vector<LayerCoord> layer_coord_list;
  for (PlanarRect& legal_shape : legal_rect_list) {
    int32_t ll_x = legal_shape.get_ll_x();
    int32_t ll_y = legal_shape.get_ll_y();
    int32_t ur_x = legal_shape.get_ur_x();
    int32_t ur_y = legal_shape.get_ur_y();
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
    RoutingLayer& curr_routing_layer = routing_layer_list[curr_layer_idx];
    std::vector<int32_t> x_track_list = RTUTIL.getScaleList(ll_x, ur_x, curr_routing_layer.getXTrackGridList());
    std::vector<int32_t> y_track_list = RTUTIL.getScaleList(ll_y, ur_y, curr_routing_layer.getYTrackGridList());
    std::vector<int32_t> x_shape_list;
    {
      x_shape_list.emplace_back(ll_x);
      if ((ur_x - ll_x) / manufacture_grid % 2 == 0) {
        x_shape_list.emplace_back((ll_x + ur_x) / 2);
      } else {
        x_shape_list.emplace_back((ll_x + ur_x - manufacture_grid) / 2);
        x_shape_list.emplace_back((ll_x + ur_x + manufacture_grid) / 2);
      }
      x_shape_list.emplace_back(ur_x);
    }
    std::vector<int32_t> y_shape_list;
    {
      y_shape_list.emplace_back(ll_y);
      if ((ur_y - ll_y) / manufacture_grid % 2 == 0) {
        y_shape_list.emplace_back((ll_y + ur_y) / 2);
      } else {
        y_shape_list.emplace_back((ll_y + ur_y - manufacture_grid) / 2);
        y_shape_list.emplace_back((ll_y + ur_y + manufacture_grid) / 2);
      }
      y_shape_list.emplace_back(ur_y);
    }
    // track grid
    for (int32_t x : x_track_list) {
      for (int32_t y : y_track_list) {
        layer_coord_list.emplace_back(x, y, curr_layer_idx);
      }
    }
    // on track
    {
      for (int32_t x : x_shape_list) {
        for (int32_t y : y_track_list) {
          layer_coord_list.emplace_back(x, y, curr_layer_idx);
        }
      }
      for (int32_t x : x_track_list) {
        for (int32_t y : y_shape_list) {
          layer_coord_list.emplace_back(x, y, curr_layer_idx);
        }
      }
    }
    // on shape
    for (int32_t x : x_shape_list) {
      for (int32_t y : y_shape_list) {
        layer_coord_list.emplace_back(x, y, curr_layer_idx);
      }
    }
  }
  {
    PlanarRect die_valid_rect = die.get_real_rect();
    int32_t shrinked_size = RTDM.getOnlyPitch();
    if (RTUTIL.hasShrinkedRect(die_valid_rect, shrinked_size)) {
      die_valid_rect = RTUTIL.getShrinkedRect(die_valid_rect, shrinked_size);
    }
    std::vector<LayerCoord> new_layer_coord_list;
    for (LayerCoord& layer_coord : layer_coord_list) {
      if (RTUTIL.isInside(die_valid_rect, layer_coord)) {
        new_layer_coord_list.push_back(layer_coord);
      }
    }
    layer_coord_list = new_layer_coord_list;
  }
  {
    for (LayerCoord& layer_coord : layer_coord_list) {
      if (layer_coord.get_x() % manufacture_grid != 0) {
        RTLOG.error(Loc::current(), "The coord is off_grid!");
      }
      if (layer_coord.get_y() % manufacture_grid != 0) {
        RTLOG.error(Loc::current(), "The coord is off_grid!");
      }
    }
  }
  std::sort(layer_coord_list.begin(), layer_coord_list.end(), CmpLayerCoordByXASC());
  layer_coord_list.erase(std::unique(layer_coord_list.begin(), layer_coord_list.end()), layer_coord_list.end());
  uniformSampleCoordList(er_model, layer_coord_list);
  if (layer_coord_list.empty()) {
    RTLOG.error(Loc::current(), "The layer_coord_list is empty!");
  }
  return layer_coord_list;
}

void EarlyRouter::uniformSampleCoordList(ERModel& er_model, std::vector<LayerCoord>& layer_coord_list)
{
  int32_t max_candidate_point_num = er_model.get_er_com_param().get_max_candidate_point_num();

  PlanarRect bounding_box = RTUTIL.getBoundingBox(layer_coord_list);
  int32_t grid_num = static_cast<int32_t>(std::sqrt(max_candidate_point_num));
  double grid_x_span = bounding_box.getXSpan() / grid_num;
  double grid_y_span = bounding_box.getYSpan() / grid_num;

  std::set<PlanarCoord, CmpPlanarCoordByXASC> visited_set;
  std::vector<LayerCoord> new_layer_coord_list;
  for (LayerCoord& layer_coord : layer_coord_list) {
    PlanarCoord grid_coord(static_cast<int32_t>((layer_coord.get_x() - bounding_box.get_ll_x()) / grid_x_span),
                           static_cast<int32_t>((layer_coord.get_y() - bounding_box.get_ll_y()) / grid_y_span));
    if (!RTUTIL.exist(visited_set, grid_coord)) {
      new_layer_coord_list.push_back(layer_coord);
      visited_set.insert(grid_coord);
      if (static_cast<int32_t>(new_layer_coord_list.size()) >= max_candidate_point_num) {
        break;
      }
    }
  }
  layer_coord_list = new_layer_coord_list;
}

void EarlyRouter::buildConflictList(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  if (er_model.get_er_com_param().get_resolve_congestion() == "high") {
    std::map<ERPin*, std::set<ERPin*>> pin_conflict_map;
    for (auto& [curr_pin, conflict_pin_set] : getPinConlictMap(er_model)) {
      pin_conflict_map[curr_pin] = conflict_pin_set;
    }
    for (auto& [curr_pin, conflict_pin_set] : pin_conflict_map) {
      if (conflict_pin_set.empty()) {
        continue;
      }
      std::vector<std::pair<ERPin*, ERPin*>> conflict_list;
      std::map<ERPin*, int32_t> pin_idx_map;
      std::queue<ERPin*> pin_queue = RTUTIL.initQueue(curr_pin);
      while (!pin_queue.empty()) {
        ERPin* er_pin = RTUTIL.getFrontAndPop(pin_queue);
        if (!RTUTIL.exist(pin_idx_map, er_pin)) {
          pin_idx_map[er_pin] = static_cast<int32_t>(pin_idx_map.size());
        }
        if (!RTUTIL.exist(pin_conflict_map, er_pin)) {
          continue;
        }
        std::set<ERPin*>& conflict_pin_set = pin_conflict_map[er_pin];
        for (ERPin* conflict_pin : conflict_pin_set) {
          conflict_list.emplace_back(er_pin, conflict_pin);
          pin_queue.push(conflict_pin);
        }
        conflict_pin_set.clear();
      }
      ERConflictGroup er_conflict_group;
      std::vector<std::vector<ERConflictPoint>>& conflict_point_list_list = er_conflict_group.get_conflict_point_list_list();
      conflict_point_list_list.resize(pin_idx_map.size());
      for (auto& [er_pin, conflict_point_list_idx] : pin_idx_map) {
        std::vector<ERConflictPoint> conflict_point_list;
        for (AccessPoint& access_point : er_pin->get_access_point_list()) {
          ERConflictPoint conflict_point;
          conflict_point.set_er_pin(er_pin);
          conflict_point.set_access_point(&access_point);
          conflict_point.set_coord(access_point.get_real_coord());
          conflict_point.set_layer_idx(access_point.get_layer_idx());
          conflict_point_list.push_back(conflict_point);
        }
        conflict_point_list_list[conflict_point_list_idx] = conflict_point_list;
      }
      std::map<int32_t, std::vector<int32_t>>& conflict_map = er_conflict_group.get_conflict_map();
      for (std::pair<ERPin*, ERPin*>& conflict_pair : conflict_list) {
        conflict_map[pin_idx_map[conflict_pair.first]].push_back(pin_idx_map[conflict_pair.second]);
      }
      er_model.get_er_conflict_group_list().push_back(er_conflict_group);
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

std::vector<std::pair<ERPin*, std::set<ERPin*>>> EarlyRouter::getPinConlictMap(ERModel& er_model)
{
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  Die& die = RTDM.getDatabase().get_die();

  std::vector<ERNet>& er_net_list = er_model.get_er_net_list();

  std::vector<std::pair<ERPin*, std::set<ERPin*>>> pin_conflict_list;
  for (ERNet& er_net : er_net_list) {
    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      pin_conflict_list.emplace_back(&er_pin, std::set<ERPin*>{});
    }
  }
#pragma omp parallel for
  for (std::pair<ERPin*, std::set<ERPin*>>& pin_conflict_pair : pin_conflict_list) {
    ERPin* er_pin = pin_conflict_pair.first;
    std::set<ERPin*>& conflict_pin_set = pin_conflict_pair.second;

    for (AccessPoint& access_point : er_pin->get_access_point_list()) {
      PlanarCoord& grid_coord = access_point.get_grid_coord();
      int32_t ll_x = std::max(die.get_grid_ll_x(), grid_coord.get_x() - 1);
      int32_t ll_y = std::max(die.get_grid_ll_y(), grid_coord.get_y() - 1);
      int32_t ur_x = std::min(die.get_grid_ur_x(), grid_coord.get_x() + 1);
      int32_t ur_y = std::min(die.get_grid_ur_y(), grid_coord.get_y() + 1);
      PlanarRect query_rect(gcell_map[ll_x][ll_y].get_ll(), gcell_map[ur_x][ur_y].get_ur());
      query_rect = RTUTIL.getEnlargedRect(query_rect, RTDM.getDatabase().get_detection_distance());
      std::vector<ERModel::AccessPointRTree::value_type> value_list;
      er_model.get_access_point_rtree().query(bgi::intersects(RTUTIL.convertToBGRectInt(query_rect)), std::back_inserter(value_list));
      for (auto& [rect, net_access_point] : value_list) {
        AccessPoint* nearby_access_point = net_access_point.second;
        ERPin* nearby_pin = &er_net_list[net_access_point.first].get_er_pin_list()[nearby_access_point->get_pin_idx()];
        if (nearby_pin != er_pin && hasConflict(access_point, *nearby_access_point)) {
          conflict_pin_set.insert(nearby_pin);
        }
      }
    }
  }
  return pin_conflict_list;
}

bool EarlyRouter::hasConflict(AccessPoint& curr_access_point, AccessPoint& gcell_access_point)
{
  return hasConflict(curr_access_point.getRealLayerCoord(), gcell_access_point.getRealLayerCoord());
}

bool EarlyRouter::hasConflict(LayerCoord layer_coord1, LayerCoord layer_coord2)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  std::set<int32_t> conflict_layer_idx_set;
  {
    int32_t start_layer_idx = layer_coord1.get_layer_idx();
    int32_t end_layer_idx = layer_coord2.get_layer_idx();
    RTUTIL.swapByASC(start_layer_idx, end_layer_idx);
    for (int32_t layer_idx = start_layer_idx; layer_idx <= end_layer_idx; layer_idx++) {
      if (layer_idx < (static_cast<int32_t>(routing_layer_list.size()) - 1)) {
        conflict_layer_idx_set.insert(layer_idx);
        conflict_layer_idx_set.insert(layer_idx + 1);
      } else {
        conflict_layer_idx_set.insert(layer_idx);
        conflict_layer_idx_set.insert(layer_idx - 1);
      }
    }
  }
  PlanarCoord& planar_coord1 = layer_coord1.get_planar_coord();
  PlanarCoord& planar_coord2 = layer_coord2.get_planar_coord();
  for (int32_t conflict_layer_idx : conflict_layer_idx_set) {
    RoutingLayer& routing_layer = routing_layer_list[conflict_layer_idx];
    int32_t min_width = routing_layer.get_min_width();
    int32_t min_length = routing_layer.get_min_area() / min_width;

    int32_t x_distance = 0;
    int32_t y_distance = 0;
    if (routing_layer.isPreferH()) {
      x_distance = min_length;
      y_distance = min_width;
    } else {
      x_distance = min_width;
      y_distance = min_length;
    }
    PlanarRect searched_rect = RTUTIL.getEnlargedRect(planar_coord1, x_distance, y_distance, x_distance, y_distance);
    if (RTUTIL.isInside(searched_rect, planar_coord2, false)) {
      return true;
    }
  }
  return false;
}

void EarlyRouter::eliminateConflict(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  // 给有conflict的pin设置
  for (ERConflictGroup& er_conflict_group : er_model.get_er_conflict_group_list()) {
    for (ERConflictPoint& best_point : getBestPointList(er_conflict_group)) {
      best_point.get_er_pin()->set_access_point(*best_point.get_access_point());
    }
  }
  // 给没有conflict的pin设置
  for (ERNet& er_net : er_model.get_er_net_list()) {
    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      if (er_pin.get_access_point().get_layer_idx() < 0) {
        er_pin.set_access_point(er_pin.get_access_point_list().front());
      }
    }
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

std::vector<ERConflictPoint> EarlyRouter::getBestPointList(ERConflictGroup& er_conflict_group)
{
  std::vector<std::vector<ERConflictPoint>>& conflict_point_list_list = er_conflict_group.get_conflict_point_list_list();
  std::map<int32_t, std::vector<int32_t>>& conflict_map = er_conflict_group.get_conflict_map();

  std::vector<ERConflictPoint> curr_ap_list;
  for (std::vector<ERConflictPoint>& conflict_point_list : conflict_point_list_list) {
    curr_ap_list.push_back(conflict_point_list.front());
  }
  bool improved = true;
  int32_t iter_num = static_cast<int32_t>(conflict_point_list_list.size() * 2);
  while (improved && iter_num--) {
    improved = false;
    for (int32_t i = 0; i < static_cast<int32_t>(conflict_point_list_list.size()); ++i) {
      std::vector<int32_t> conflict_j_list;
      if (RTUTIL.exist(conflict_map, i)) {
        conflict_j_list = conflict_map[i];
      } else {
        RTLOG.error(Loc::current(), "The conflict_map is not exist i!");
      }
      int32_t best_conflict_count = INT32_MAX;
      int32_t best_min_distance = INT32_MAX;
      ERConflictPoint best_ap = curr_ap_list[i];
      for (ERConflictPoint& conflict_point : conflict_point_list_list[i]) {
        int32_t conflict_count = 0;
        int32_t min_distance = INT32_MAX;
        for (int32_t j : conflict_j_list) {
          if (hasConflict(conflict_point, curr_ap_list[j])) {
            ++conflict_count;
          }
          min_distance = std::min(min_distance, RTUTIL.getManhattanDistance(conflict_point, curr_ap_list[j]));
        }
        // 优先比较冲突数，其次比较最小曼哈顿距离（越小越好）
        if (conflict_count < best_conflict_count || (conflict_count == best_conflict_count && min_distance < best_min_distance)) {
          best_conflict_count = conflict_count;
          best_min_distance = min_distance;
          best_ap = conflict_point;
        }
      }
      if (best_ap.get_access_point() != curr_ap_list[i].get_access_point()) {
        curr_ap_list[i] = best_ap;
        improved = true;
      }
    }
  }
  return curr_ap_list;
}

void EarlyRouter::uploadAccessPoint(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<ERModel::AccessPointRTree::value_type> value_list;
  for (ERNet& er_net : er_model.get_er_net_list()) {
    std::vector<PlanarCoord> coord_list;
    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      coord_list.push_back(er_pin.get_access_point().get_real_coord());
    }
    BoundingBox& bounding_box = er_net.get_bounding_box();
    bounding_box.set_real_rect(RTUTIL.getBoundingBox(coord_list));
    bounding_box.set_grid_rect(RTUTIL.getOpenGCellGridRect(bounding_box.get_real_rect(), gcell_axis));
    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      AccessPoint& access_point = er_pin.get_access_point();
      access_point.set_grid_coord(RTUTIL.getGCellGridCoordByBBox(access_point.get_real_coord(), gcell_axis, bounding_box));
      value_list.emplace_back(RTUTIL.convertToBGRectInt(PlanarRect(access_point.get_real_coord(), access_point.get_real_coord())),
                              std::make_pair(er_net.get_net_idx(), &access_point));
    }
  }
  er_model.get_access_point_rtree() = ERModel::AccessPointRTree(value_list);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::uploadAccessPatch(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::map<int32_t, PlanarRect>& layer_enclosure_map = RTDM.getDatabase().get_layer_enclosure_map();
  int32_t bottom_routing_layer_idx = RTDM.getConfig().bottom_routing_layer_idx;
  int32_t top_routing_layer_idx = RTDM.getConfig().top_routing_layer_idx;
  int32_t only_pitch = RTDM.getOnlyPitch();

  for (ERNet& er_net : er_model.get_er_net_list()) {
    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      PlanarCoord access_coord = er_pin.get_access_point().get_real_coord();
      int32_t curr_layer_idx = er_pin.get_access_point().get_layer_idx();
      {
        RoutingLayer& routing_layer = routing_layer_list[curr_layer_idx];
        std::vector<int32_t> x_track_list
            = RTUTIL.getScaleList(access_coord.get_x() - only_pitch, access_coord.get_x() + only_pitch, routing_layer.getXTrackGridList());
        std::vector<int32_t> y_track_list
            = RTUTIL.getScaleList(access_coord.get_y() - only_pitch, access_coord.get_y() + only_pitch, routing_layer.getYTrackGridList());

        int32_t min_distance = INT_MAX;
        PlanarCoord best_coord = access_coord;
        for (int32_t x : x_track_list) {
          for (int32_t y : y_track_list) {
            PlanarCoord track_coord(x, y);
            int32_t distance = RTUTIL.getManhattanDistance(access_coord, track_coord);
            if (distance < min_distance) {
              min_distance = distance;
              best_coord = track_coord;
            }
          }
        }
        access_coord = best_coord;
      }
      int32_t min_layer_idx = curr_layer_idx;
      int32_t max_layer_idx = curr_layer_idx;
      {
        if (er_pin.get_is_core()) {
          if (curr_layer_idx < bottom_routing_layer_idx) {
            max_layer_idx = bottom_routing_layer_idx + 1;
          } else if (top_routing_layer_idx < curr_layer_idx) {
            max_layer_idx = top_routing_layer_idx - 1;
          } else if (curr_layer_idx < top_routing_layer_idx) {
            max_layer_idx = curr_layer_idx + 1;
          } else {
            max_layer_idx = curr_layer_idx - 1;
          }
        } else {
          if (curr_layer_idx < bottom_routing_layer_idx) {
            max_layer_idx = bottom_routing_layer_idx;
          } else if (top_routing_layer_idx < curr_layer_idx) {
            max_layer_idx = top_routing_layer_idx;
          } else if (curr_layer_idx < top_routing_layer_idx) {
            max_layer_idx = curr_layer_idx;
          } else {
            max_layer_idx = curr_layer_idx;
          }
        }
        RTUTIL.swapByASC(min_layer_idx, max_layer_idx);
      }
      for (int32_t layer_idx = min_layer_idx; layer_idx <= max_layer_idx; layer_idx++) {
        if (layer_idx == curr_layer_idx) {
          continue;
        }
        RoutingLayer& routing_layer = routing_layer_list[layer_idx];
        int32_t half_width = routing_layer.get_min_width() / 2;
        int32_t min_length = routing_layer.get_min_area() / routing_layer.get_min_width();
        PlanarRect& enclosure = layer_enclosure_map[layer_idx];
        int32_t half_x_span = enclosure.getXSpan() / 2;
        int32_t half_y_span = enclosure.getYSpan() / 2;

        EXTLayerRect patch;
        if (routing_layer.isPreferH()) {
          patch.set_real_rect(RTUTIL.getEnlargedRect(access_coord, min_length - half_x_span, half_width, half_x_span, half_width));
        } else {
          patch.set_real_rect(RTUTIL.getEnlargedRect(access_coord, half_width, min_length - half_y_span, half_width, half_y_span));
        }
        patch.set_grid_rect(RTUTIL.getClosedGCellGridRect(patch.get_real_rect(), gcell_axis));
        patch.set_layer_idx(layer_idx);
        er_model.get_net_detailed_patch_map()[er_net.get_net_idx()].push_back(std::move(patch));
      }
    }
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::buildSupplySchedule(ERModel& er_model)
{
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  int32_t bottom_routing_layer_idx = RTDM.getConfig().bottom_routing_layer_idx;
  int32_t top_routing_layer_idx = RTDM.getConfig().top_routing_layer_idx;

  for (RoutingLayer& routing_layer : routing_layer_list) {
    if (routing_layer.get_layer_idx() < bottom_routing_layer_idx || top_routing_layer_idx < routing_layer.get_layer_idx()) {
      continue;
    }
    if (routing_layer.isPreferH()) {
      for (int32_t begin_x = 1; begin_x <= 2; begin_x++) {
        std::vector<std::pair<LayerCoord, LayerCoord>> grid_pair_list;
        for (int32_t y = 0; y < die.getYSize(); y++) {
          for (int32_t x = begin_x; x < die.getXSize(); x += 2) {
            grid_pair_list.emplace_back(LayerCoord(x - 1, y, routing_layer.get_layer_idx()), LayerCoord(x, y, routing_layer.get_layer_idx()));
          }
        }
        er_model.get_grid_pair_list_list().push_back(grid_pair_list);
      }
    } else {
      for (int32_t begin_y = 1; begin_y <= 2; begin_y++) {
        std::vector<std::pair<LayerCoord, LayerCoord>> grid_pair_list;
        for (int32_t x = 0; x < die.getXSize(); x++) {
          for (int32_t y = begin_y; y < die.getYSize(); y += 2) {
            grid_pair_list.emplace_back(LayerCoord(x, y - 1, routing_layer.get_layer_idx()), LayerCoord(x, y, routing_layer.get_layer_idx()));
          }
        }
        er_model.get_grid_pair_list_list().push_back(grid_pair_list);
      }
    }
  }
}

void EarlyRouter::analyzeSupply(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<GridMap<EREdge>>& layer_h_edge_map = er_model.get_layer_h_edge_map();
  std::vector<GridMap<EREdge>>& layer_v_edge_map = er_model.get_layer_v_edge_map();

  using DetailedRTree = bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>;
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<std::vector<std::pair<int32_t, PlanarRect>>> layer_detailed_shape_list(routing_layer_list.size());
  std::vector<std::vector<std::pair<BGRectInt, int32_t>>> layer_rtree_value_list(routing_layer_list.size());
  Die& die = RTDM.getDatabase().get_die();
  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  for (auto& [net_idx, segment_list] : er_model.get_net_detailed_result_map()) {
    for (Segment<LayerCoord>& segment : segment_list) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
        if (!net_shape.get_is_routing()) {
          continue;
        }
        PlanarRect real_rect = net_shape;
        if (!RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
          continue;
        }
        int32_t layer_idx = net_shape.get_layer_idx();
        std::vector<std::pair<int32_t, PlanarRect>>& detailed_shape_list = layer_detailed_shape_list[layer_idx];
        layer_rtree_value_list[layer_idx].emplace_back(RTUTIL.convertToBGRectInt(real_rect), static_cast<int32_t>(detailed_shape_list.size()));
        detailed_shape_list.emplace_back(net_idx, real_rect);
      }
    }
  }
  for (auto& [net_idx, patch_list] : er_model.get_net_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      PlanarRect real_rect = patch.get_real_rect();
      if (!RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
        continue;
      }
      int32_t layer_idx = patch.get_layer_idx();
      std::vector<std::pair<int32_t, PlanarRect>>& detailed_shape_list = layer_detailed_shape_list[layer_idx];
      layer_rtree_value_list[layer_idx].emplace_back(RTUTIL.convertToBGRectInt(real_rect), static_cast<int32_t>(detailed_shape_list.size()));
      detailed_shape_list.emplace_back(net_idx, real_rect);
    }
  }
  std::vector<DetailedRTree> layer_rtree_list(routing_layer_list.size());
#pragma omp parallel for
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(routing_layer_list.size()); layer_idx++) {
    layer_rtree_list[layer_idx] = DetailedRTree(layer_rtree_value_list[layer_idx].begin(), layer_rtree_value_list[layer_idx].end());
  }
  std::vector<std::vector<std::pair<BGRectInt, int32_t>>>().swap(layer_rtree_value_list);

  size_t total_pair_num = 0;
  for (std::vector<std::pair<LayerCoord, LayerCoord>>& grid_pair_list : er_model.get_grid_pair_list_list()) {
    total_pair_num += grid_pair_list.size();
  }

  size_t analyzed_pair_num = 0;
  for (std::vector<std::pair<LayerCoord, LayerCoord>>& grid_pair_list : er_model.get_grid_pair_list_list()) {
    Monitor stage_monitor;
#pragma omp parallel for
    for (std::pair<LayerCoord, LayerCoord>& grid_pair : grid_pair_list) {
      LayerCoord first_coord = grid_pair.first;
      LayerCoord second_coord = grid_pair.second;
      EXTLayerRect search_rect = getSearchRect(first_coord, second_coord);

      bool is_horizontal = RTUTIL.isHorizontal(first_coord, second_coord);
      int32_t edge_x = std::min(first_coord.get_x(), second_coord.get_x());
      int32_t edge_y = std::min(first_coord.get_y(), second_coord.get_y());
      GridMap<EREdge>& edge_map = is_horizontal ? layer_h_edge_map[first_coord.get_layer_idx()] : layer_v_edge_map[first_coord.get_layer_idx()];
      if (!edge_map.isInside(edge_x, edge_y)) {
        RTLOG.error(Loc::current(), "The routing edge is outside the map!");
      }
      EREdge& routing_edge = edge_map[edge_x][edge_y];
      std::set<int32_t>& ignore_net_set = routing_edge.get_ignore_net_set();

      std::vector<PlanarRect> obs_rect_list;
      {
        for (auto& [net_idx, fixed_rect_set] : RTDM.getNetFixedRectMap(true, search_rect)) {
          for (EXTLayerRect* fixed_rect : fixed_rect_set) {
            obs_rect_list.push_back(fixed_rect->get_real_rect());
          }
        }

        PlanarRect query_real_rect = RTUTIL.getEnlargedRect(search_rect.get_real_rect(), detection_distance);
        std::vector<std::pair<BGRectInt, int32_t>> rtree_value_list;
        layer_rtree_list[search_rect.get_layer_idx()].query(bgi::intersects(RTUTIL.convertToBGRectInt(query_real_rect)), std::back_inserter(rtree_value_list));
        for (auto& [rect, shape_idx] : rtree_value_list) {
          auto& [net_idx, detailed_shape] = layer_detailed_shape_list[search_rect.get_layer_idx()][shape_idx];
          obs_rect_list.push_back(detailed_shape);
          if (RTUTIL.isOpenOverlap(search_rect.get_real_rect(), detailed_shape)) {
            ignore_net_set.insert(net_idx);
          }
        }
      }
      std::vector<LayerRect> wire_list = getCrossingWireList(search_rect);
      int32_t supply = 0;
      for (LayerRect& wire : wire_list) {
        if (isAccess(wire, obs_rect_list)) {
          supply++;
        }
      }
      supply = std::min(supply, static_cast<int32_t>(wire_list.size()) - static_cast<int32_t>(ignore_net_set.size()));
      supply = std::max(0, supply);
      routing_edge.set_supply(static_cast<int32_t>(supply * 0.9));
    }
    analyzed_pair_num += grid_pair_list.size();
    RTLOG.info(Loc::current(), "Analyzed ", analyzed_pair_num, "/", total_pair_num, "(", RTUTIL.getPercentage(analyzed_pair_num, total_pair_num),
               ") grid pairs", stage_monitor.getStatsInfo());
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::buildPlanarEdgeMap(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  GridMap<EREdge>& planar_h_edge_map = er_model.get_planar_h_edge_map();
  GridMap<EREdge>& planar_v_edge_map = er_model.get_planar_v_edge_map();
  std::vector<GridMap<EREdge>>& layer_h_edge_map = er_model.get_layer_h_edge_map();
  std::vector<GridMap<EREdge>>& layer_v_edge_map = er_model.get_layer_v_edge_map();

  planar_h_edge_map.init(std::max(0, gcell_map.get_x_size() - 1), gcell_map.get_y_size());
  planar_v_edge_map.init(gcell_map.get_x_size(), std::max(0, gcell_map.get_y_size() - 1));
  for (GridMap<EREdge>* planar_edge_map : {&planar_h_edge_map, &planar_v_edge_map}) {
    std::vector<GridMap<EREdge>>& layer_edge_map_list = planar_edge_map == &planar_h_edge_map ? layer_h_edge_map : layer_v_edge_map;
    for (GridMap<EREdge>& layer_edge_map : layer_edge_map_list) {
#pragma omp parallel for
      for (int32_t x = 0; x < layer_edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < layer_edge_map.get_y_size(); y++) {
          EREdge& planar_edge = (*planar_edge_map)[x][y];
          EREdge& layer_edge = layer_edge_map[x][y];
          planar_edge.set_supply(planar_edge.get_supply() + layer_edge.get_supply());
          planar_edge.get_ignore_net_set().insert(layer_edge.get_ignore_net_set().begin(), layer_edge.get_ignore_net_set().end());
        }
      }
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::clearLayerEdgeDemand(ERModel& er_model)
{
  for (std::vector<GridMap<EREdge>>* layer_edge_map_list : {&er_model.get_layer_h_edge_map(), &er_model.get_layer_v_edge_map()}) {
#pragma omp parallel for
    for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(layer_edge_map_list->size()); layer_idx++) {
      GridMap<EREdge>& edge_map = (*layer_edge_map_list)[layer_idx];
      for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
          edge_map[x][y].set_demand(0);
          edge_map[x][y].get_demand_net_idx_list().clear();
        }
      }
    }
  }
}

void EarlyRouter::checkEdgeDemand(ERModel& er_model, bool is_planar)
{
  std::vector<GridMap<EREdge>*> edge_map_list;
  if (is_planar) {
    edge_map_list = {&er_model.get_planar_h_edge_map(), &er_model.get_planar_v_edge_map()};
  } else {
    for (GridMap<EREdge>& edge_map : er_model.get_layer_h_edge_map()) {
      edge_map_list.push_back(&edge_map);
    }
    for (GridMap<EREdge>& edge_map : er_model.get_layer_v_edge_map()) {
      edge_map_list.push_back(&edge_map);
    }
  }

  int32_t net_num = static_cast<int32_t>(er_model.get_er_net_list().size());
  for (GridMap<EREdge>* edge_map : edge_map_list) {
    for (int32_t x = 0; x < edge_map->get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map->get_y_size(); y++) {
        EREdge& edge = (*edge_map)[x][y];
        std::vector<int32_t>& demand_net_idx_list = edge.get_demand_net_idx_list();
        std::set<int32_t> demand_net_idx_set(demand_net_idx_list.begin(), demand_net_idx_list.end());
        if (edge.get_supply() < 0) {
          RTLOG.error(Loc::current(), "The Edge supply is negative!");
        }
        if (edge.get_demand() < 0 || edge.get_demand() != static_cast<int32_t>(demand_net_idx_list.size())) {
          RTLOG.error(Loc::current(), "The Edge demand is inconsistent!");
        }
        if (demand_net_idx_set.size() != demand_net_idx_list.size()) {
          RTLOG.error(Loc::current(), "The Edge demand net is duplicated!");
        }
        for (int32_t net_idx : demand_net_idx_list) {
          if (net_idx < 0 || net_num <= net_idx) {
            RTLOG.error(Loc::current(), "The Edge demand net index is invalid!");
          }
          if (RTUTIL.exist(edge.get_ignore_net_set(), net_idx)) {
            RTLOG.error(Loc::current(), "The ignored net consumes Edge demand!");
          }
        }
      }
    }
  }
}

EXTLayerRect EarlyRouter::getSearchRect(LayerCoord& first_coord, LayerCoord& second_coord)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();

  if (first_coord.get_layer_idx() != second_coord.get_layer_idx()) {
    RTLOG.error(Loc::current(), "The grid_pair layer_idx is not equal!");
  }
  PlanarRect search_real_rect;
  {
    PlanarRect first_real_rect = RTUTIL.getRealRectByGCell(first_coord, gcell_axis);
    PlanarCoord first_mid_coord = first_real_rect.getMidPoint();
    PlanarRect second_real_rect = RTUTIL.getRealRectByGCell(second_coord, gcell_axis);
    PlanarCoord second_mid_coord = second_real_rect.getMidPoint();
    if (RTUTIL.isHorizontal(first_coord, second_coord)) {
      std::vector<PlanarCoord> coord_list;
      coord_list.emplace_back(first_mid_coord.get_x(), first_real_rect.get_ll_y());
      coord_list.emplace_back(first_mid_coord.get_x(), first_real_rect.get_ur_y());
      coord_list.emplace_back(second_mid_coord.get_x(), second_real_rect.get_ll_y());
      coord_list.emplace_back(second_mid_coord.get_x(), second_real_rect.get_ur_y());
      search_real_rect = RTUTIL.getBoundingBox(coord_list);
    } else if (RTUTIL.isVertical(first_coord, second_coord)) {
      std::vector<PlanarCoord> coord_list;
      coord_list.emplace_back(first_real_rect.get_ll_x(), first_mid_coord.get_y());
      coord_list.emplace_back(first_real_rect.get_ur_x(), first_mid_coord.get_y());
      coord_list.emplace_back(second_real_rect.get_ll_x(), second_mid_coord.get_y());
      coord_list.emplace_back(second_real_rect.get_ur_x(), second_mid_coord.get_y());
      search_real_rect = RTUTIL.getBoundingBox(coord_list);
    }
  }
  EXTLayerRect search_rect;
  search_rect.set_real_rect(search_real_rect);
  search_rect.set_grid_rect(RTUTIL.getClosedGCellGridRect(search_rect.get_real_rect(), gcell_axis));
  search_rect.set_layer_idx(first_coord.get_layer_idx());
  return search_rect;
}

std::vector<LayerRect> EarlyRouter::getCrossingWireList(EXTLayerRect& search_rect)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  RoutingLayer& routing_layer = routing_layer_list[search_rect.get_layer_idx()];
  int32_t half_wire_width = routing_layer.get_min_width() / 2;

  int32_t real_ll_x = search_rect.get_real_ll_x();
  int32_t real_ll_y = search_rect.get_real_ll_y();
  int32_t real_ur_x = search_rect.get_real_ur_x();
  int32_t real_ur_y = search_rect.get_real_ur_y();

  std::vector<LayerRect> wire_list;
  if (routing_layer.isPreferH()) {
    for (int32_t y : RTUTIL.getScaleList(real_ll_y, real_ur_y, routing_layer.getYTrackGridList())) {
      wire_list.emplace_back(real_ll_x, y - half_wire_width, real_ur_x, y + half_wire_width, search_rect.get_layer_idx());
    }
  } else {
    for (int32_t x : RTUTIL.getScaleList(real_ll_x, real_ur_x, routing_layer.getXTrackGridList())) {
      wire_list.emplace_back(x - half_wire_width, real_ll_y, x + half_wire_width, real_ur_y, search_rect.get_layer_idx());
    }
  }
  return wire_list;
}

bool EarlyRouter::isAccess(LayerRect& wire, std::vector<PlanarRect>& obs_rect_list)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  RoutingLayer& routing_layer = routing_layer_list[wire.get_layer_idx()];

  for (PlanarRect& obs_rect : obs_rect_list) {
    int32_t enlarged_size = routing_layer.getPRLSpacing(obs_rect);
    PlanarRect enlarged_rect = RTUTIL.getEnlargedRect(obs_rect, enlarged_size);
    if (RTUTIL.isOpenOverlap(enlarged_rect, wire)) {
      // 阻塞
      return false;
    }
  }
  return true;
}

void EarlyRouter::generateTopology(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<ERNet*> er_task_list;
  {
    std::vector<ERNet>& er_net_list = er_model.get_er_net_list();
    er_task_list.reserve(er_net_list.size());
    for (ERNet& er_net : er_net_list) {
      er_task_list.push_back(&er_net);
    }
    std::sort(er_task_list.begin(), er_task_list.end(), CmpERNet());
  }

  int32_t batch_size = RTUTIL.getBatchSize(er_task_list.size());

  Monitor stage_monitor;
  for (size_t i = 0; i < er_task_list.size(); i++) {
    generateERTask(er_model, er_task_list[i]);
    if ((i + 1) % batch_size == 0 || (i + 1) == er_task_list.size()) {
      RTLOG.info(Loc::current(), "Routed ", (i + 1), "/", er_task_list.size(), "(", RTUTIL.getPercentage(i + 1, er_task_list.size()), ") nets",
                 stage_monitor.getStatsInfo());
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::generateERTask(ERModel& er_model, ERNet* er_task)
{
  initSinglePlanarTask(er_model, er_task);
  std::vector<Segment<PlanarCoord>> routing_segment_list = getPlanarRoutingSegmentList(er_model);
  MTree<PlanarCoord> coord_tree = getCoordTree(er_model, routing_segment_list);
  updateDemandToGraph(er_model, ChangeType::kAdd, coord_tree);
  uploadPlanarNetResult(er_model, coord_tree);
  resetSinglePlanarTask(er_model);
}

void EarlyRouter::initSinglePlanarTask(ERModel& er_model, ERNet* er_task)
{
  er_model.set_curr_er_task(er_task);
}

std::vector<Segment<PlanarCoord>> EarlyRouter::getPlanarRoutingSegmentList(ERModel& er_model)
{
  std::vector<ERCandidate> er_candidate_list = getERCandidateList(er_model);
#pragma omp parallel for
  for (ERCandidate& er_candidate : er_candidate_list) {
    updateERCandidate(er_model, er_candidate);
  }
  std::map<int32_t, ERCandidate*> topo_candidate_map;
  for (ERCandidate& er_candidate : er_candidate_list) {
    int32_t topo_idx = er_candidate.get_topo_idx();
    if (!RTUTIL.exist(topo_candidate_map, topo_idx)) {
      topo_candidate_map[topo_idx] = &er_candidate;
      continue;
    }
    ERCandidate* current_best = topo_candidate_map[topo_idx];
    if (!er_candidate.get_is_path_blocked() && current_best->get_is_path_blocked()) {
      topo_candidate_map[topo_idx] = &er_candidate;
    } else if (!er_candidate.get_is_path_blocked() && !current_best->get_is_path_blocked()) {
      if (er_candidate.get_total_wire_length() < current_best->get_total_wire_length()) {
        topo_candidate_map[topo_idx] = &er_candidate;
      } else if (er_candidate.get_total_wire_length() == current_best->get_total_wire_length()) {
        if (er_candidate.get_total_corner_num() < current_best->get_total_corner_num()) {
          topo_candidate_map[topo_idx] = &er_candidate;
        }
      }
    } else if (er_candidate.get_is_path_blocked() && current_best->get_is_path_blocked()) {
      if (er_candidate.get_total_overflow_cost() < current_best->get_total_overflow_cost()) {
        topo_candidate_map[topo_idx] = &er_candidate;
      }
    }
  }
  std::vector<Segment<PlanarCoord>> routing_segment_list;
  for (auto& [topo_idx, min_candidate] : topo_candidate_map) {
    for (Segment<PlanarCoord>& routing_segment : min_candidate->get_routing_segment_list()) {
      routing_segment_list.push_back(routing_segment);
    }
  }
  return routing_segment_list;
}

std::vector<ERCandidate> EarlyRouter::getERCandidateList(ERModel& er_model)
{
  std::vector<Segment<PlanarCoord>> planar_topo_list = getPlanarTopoList(er_model);
  std::vector<std::pair<int32_t, std::vector<std::vector<Segment<PlanarCoord>>> (EarlyRouter::*)(ERModel&, Segment<PlanarCoord>&)>> strategy_list;
  strategy_list.emplace_back(0, &EarlyRouter::getRoutingSegmentListByStraight);
  strategy_list.emplace_back(1, &EarlyRouter::getRoutingSegmentListByLPattern);
  if (er_model.get_er_com_param().get_resolve_congestion() == "high") {
    strategy_list.emplace_back(2, &EarlyRouter::getRoutingSegmentListByZPattern);
    strategy_list.emplace_back(2, &EarlyRouter::getRoutingSegmentListByUPattern);
    strategy_list.emplace_back(3, &EarlyRouter::getRoutingSegmentListByInner3Bends);
    strategy_list.emplace_back(3, &EarlyRouter::getRoutingSegmentListByOuter3Bends);
  }
  std::vector<ERCandidate> er_candidate_list;
  for (size_t i = 0; i < planar_topo_list.size(); i++) {
    for (const auto& [corner_num, getRoutingSegmentList] : strategy_list) {
      for (const std::vector<Segment<PlanarCoord>>& routing_segment_list : (this->*getRoutingSegmentList)(er_model, planar_topo_list[i])) {
        er_candidate_list.emplace_back(i, routing_segment_list, corner_num, 0, false, 0);
      }
    }
  }
  return er_candidate_list;
}

std::vector<Segment<PlanarCoord>> EarlyRouter::getPlanarTopoList(ERModel& er_model)
{
  int32_t topo_spilt_length = er_model.get_er_com_param().get_topo_spilt_length();

  std::vector<PlanarCoord> planar_coord_list;
  {
    for (ERPin& er_pin : er_model.get_curr_er_task()->get_er_pin_list()) {
      planar_coord_list.push_back(er_pin.get_access_point().get_grid_coord());
    }
    std::sort(planar_coord_list.begin(), planar_coord_list.end(), CmpPlanarCoordByXASC());
    planar_coord_list.erase(std::unique(planar_coord_list.begin(), planar_coord_list.end()), planar_coord_list.end());
  }
  std::vector<Segment<PlanarCoord>> planar_topo_list;
  TBTask tb_task;
  tb_task.set_planar_coord_list(planar_coord_list);
  for (Segment<PlanarCoord>& planar_topo : RTTB.getPlanarTopoList(tb_task)) {
    PlanarCoord& first_coord = planar_topo.get_first();
    PlanarCoord& second_coord = planar_topo.get_second();
    int32_t span_x = std::abs(first_coord.get_x() - second_coord.get_x());
    int32_t span_y = std::abs(first_coord.get_y() - second_coord.get_y());
    if (span_x > 1 && span_y > 1 && (span_x > topo_spilt_length || span_y > topo_spilt_length)) {
      int32_t stick_num_x;
      if (span_x % topo_spilt_length == 0) {
        stick_num_x = (span_x / topo_spilt_length - 1);
      } else {
        stick_num_x = (span_x < topo_spilt_length) ? (span_x - 1) : (span_x / topo_spilt_length);
      }
      int32_t stick_num_y;
      if (span_y % topo_spilt_length == 0) {
        stick_num_y = (span_y / topo_spilt_length - 1);
      } else {
        stick_num_y = (span_y < topo_spilt_length) ? (span_y - 1) : (span_y / topo_spilt_length);
      }
      int32_t stick_num = std::min(stick_num_x, stick_num_y);

      std::vector<PlanarCoord> coord_list;
      coord_list.push_back(first_coord);
      double delta_x = static_cast<double>(second_coord.get_x() - first_coord.get_x()) / (stick_num + 1);
      double delta_y = static_cast<double>(second_coord.get_y() - first_coord.get_y()) / (stick_num + 1);
      for (int32_t i = 1; i <= stick_num; i++) {
        coord_list.emplace_back(std::round(first_coord.get_x() + i * delta_x), std::round(first_coord.get_y() + i * delta_y));
      }
      coord_list.push_back(second_coord);
      for (size_t i = 1; i < coord_list.size(); i++) {
        planar_topo_list.emplace_back(coord_list[i - 1], coord_list[i]);
      }
    } else {
      planar_topo_list.emplace_back(first_coord, second_coord);
    }
  }
  return planar_topo_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> EarlyRouter::getRoutingSegmentListByStraight(ERModel& er_model, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isOblique(first_coord, second_coord)) {
    return {};
  }
  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(first_coord, second_coord);
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> EarlyRouter::getRoutingSegmentListByLPattern(ERModel& er_model, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return {};
  }
  std::vector<std::vector<PlanarCoord>> inflection_list_list;
  PlanarCoord inflection_coord1(first_coord.get_x(), second_coord.get_y());
  inflection_list_list.push_back({inflection_coord1});
  PlanarCoord inflection_coord2(second_coord.get_x(), first_coord.get_y());
  inflection_list_list.push_back({inflection_coord2});

  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (std::vector<PlanarCoord>& inflection_list : inflection_list_list) {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(planar_topo.get_first(), inflection_list.front());
    for (size_t i = 1; i < inflection_list.size(); i++) {
      routing_segment_list.emplace_back(inflection_list[i - 1], inflection_list[i]);
    }
    routing_segment_list.emplace_back(inflection_list.back(), planar_topo.get_second());
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> EarlyRouter::getRoutingSegmentListByZPattern(ERModel& er_model, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return {};
  }
  std::vector<int32_t> x_mid_index_list = getMidIndexList(first_coord.get_x(), second_coord.get_x());
  std::vector<int32_t> y_mid_index_list = getMidIndexList(first_coord.get_y(), second_coord.get_y());
  if (x_mid_index_list.empty() && y_mid_index_list.empty()) {
    return {};
  }
  std::vector<std::vector<PlanarCoord>> inflection_list_list;
  for (size_t i = 0; i < x_mid_index_list.size(); i++) {
    PlanarCoord inflection_coord1(x_mid_index_list[i], first_coord.get_y());
    PlanarCoord inflection_coord2(x_mid_index_list[i], second_coord.get_y());
    inflection_list_list.push_back({inflection_coord1, inflection_coord2});
  }
  for (size_t i = 0; i < y_mid_index_list.size(); i++) {
    PlanarCoord inflection_coord1(first_coord.get_x(), y_mid_index_list[i]);
    PlanarCoord inflection_coord2(second_coord.get_x(), y_mid_index_list[i]);
    inflection_list_list.push_back({inflection_coord1, inflection_coord2});
  }
  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (std::vector<PlanarCoord>& inflection_list : inflection_list_list) {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(planar_topo.get_first(), inflection_list.front());
    for (size_t i = 1; i < inflection_list.size(); i++) {
      routing_segment_list.emplace_back(inflection_list[i - 1], inflection_list[i]);
    }
    routing_segment_list.emplace_back(inflection_list.back(), planar_topo.get_second());
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<int32_t> EarlyRouter::getMidIndexList(int32_t first_idx, int32_t second_idx)
{
  std::vector<int32_t> mid_index_list;
  RTUTIL.swapByASC(first_idx, second_idx);
  mid_index_list.reserve(second_idx - first_idx - 1);
  for (int32_t i = (first_idx + 1); i <= (second_idx - 1); i++) {
    mid_index_list.push_back(i);
  }
  return mid_index_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> EarlyRouter::getRoutingSegmentListByUPattern(ERModel& er_model, Segment<PlanarCoord>& planar_topo)
{
  Die& die = RTDM.getDatabase().get_die();
  int32_t expand_step_num = er_model.get_er_com_param().get_expand_step_num();
  int32_t expand_step_length = er_model.get_er_com_param().get_expand_step_length();

  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.getManhattanDistance(first_coord, second_coord) <= 1) {
    return {};
  }
  int32_t first_x = first_coord.get_x();
  int32_t second_x = second_coord.get_x();
  int32_t first_y = first_coord.get_y();
  int32_t second_y = second_coord.get_y();
  RTUTIL.swapByASC(first_x, second_x);
  RTUTIL.swapByASC(first_y, second_y);

  std::vector<std::vector<PlanarCoord>> inflection_list_list;
  if (!RTUTIL.isHorizontal(first_coord, second_coord)) {
    for (int32_t i = 0; i < expand_step_num; i++) {
      first_x -= expand_step_length;
      if (first_x >= die.get_grid_ll_x()) {
        PlanarCoord inflection_coord1(first_x, first_coord.get_y());
        PlanarCoord inflection_coord2(first_x, second_coord.get_y());
        inflection_list_list.push_back({inflection_coord1, inflection_coord2});
      }
      second_x += expand_step_length;
      if (second_x <= die.get_grid_ur_x()) {
        PlanarCoord inflection_coord1(second_x, first_coord.get_y());
        PlanarCoord inflection_coord2(second_x, second_coord.get_y());
        inflection_list_list.push_back({inflection_coord1, inflection_coord2});
      }
    }
  }
  if (!RTUTIL.isVertical(first_coord, second_coord)) {
    for (int32_t i = 0; i < expand_step_num; i++) {
      first_y -= expand_step_length;
      if (first_y >= die.get_grid_ll_y()) {
        PlanarCoord inflection_coord1(first_coord.get_x(), first_y);
        PlanarCoord inflection_coord2(second_coord.get_x(), first_y);
        inflection_list_list.push_back({inflection_coord1, inflection_coord2});
      }
      second_y += expand_step_length;
      if (second_y <= die.get_grid_ur_y()) {
        PlanarCoord inflection_coord1(first_coord.get_x(), second_y);
        PlanarCoord inflection_coord2(second_coord.get_x(), second_y);
        inflection_list_list.push_back({inflection_coord1, inflection_coord2});
      }
    }
  }
  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (std::vector<PlanarCoord>& inflection_list : inflection_list_list) {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(planar_topo.get_first(), inflection_list.front());
    for (size_t i = 1; i < inflection_list.size(); i++) {
      routing_segment_list.emplace_back(inflection_list[i - 1], inflection_list[i]);
    }
    routing_segment_list.emplace_back(inflection_list.back(), planar_topo.get_second());
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> EarlyRouter::getRoutingSegmentListByInner3Bends(ERModel& er_model, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return {};
  }
  std::vector<int32_t> x_mid_index_list = getMidIndexList(first_coord.get_x(), second_coord.get_x());
  std::vector<int32_t> y_mid_index_list = getMidIndexList(first_coord.get_y(), second_coord.get_y());
  if (x_mid_index_list.empty() || y_mid_index_list.empty()) {
    return {};
  }
  std::vector<std::vector<PlanarCoord>> inflection_list_list;
  for (size_t i = 0; i < x_mid_index_list.size(); i++) {
    for (size_t j = 0; j < y_mid_index_list.size(); j++) {
      PlanarCoord inflection_coord1(x_mid_index_list[i], first_coord.get_y());
      PlanarCoord inflection_coord2(x_mid_index_list[i], y_mid_index_list[j]);
      PlanarCoord inflection_coord3(second_coord.get_x(), y_mid_index_list[j]);
      inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
    }
  }

  for (size_t i = 0; i < x_mid_index_list.size(); i++) {
    for (size_t j = 0; j < y_mid_index_list.size(); j++) {
      PlanarCoord inflection_coord1(first_coord.get_x(), y_mid_index_list[j]);
      PlanarCoord inflection_coord2(x_mid_index_list[i], y_mid_index_list[j]);
      PlanarCoord inflection_coord3(x_mid_index_list[i], second_coord.get_y());
      inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
    }
  }
  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (std::vector<PlanarCoord>& inflection_list : inflection_list_list) {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(planar_topo.get_first(), inflection_list.front());
    for (size_t i = 1; i < inflection_list.size(); i++) {
      routing_segment_list.emplace_back(inflection_list[i - 1], inflection_list[i]);
    }
    routing_segment_list.emplace_back(inflection_list.back(), planar_topo.get_second());
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> EarlyRouter::getRoutingSegmentListByOuter3Bends(ERModel& er_model, Segment<PlanarCoord>& planar_topo)
{
  Die& die = RTDM.getDatabase().get_die();
  int32_t expand_step_num = er_model.get_er_com_param().get_expand_step_num();
  int32_t expand_step_length = er_model.get_er_com_param().get_expand_step_length();

  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return {};
  }
  int32_t start_x = first_coord.get_x();
  int32_t end_x = second_coord.get_x();
  int32_t start_y = first_coord.get_y();
  int32_t end_y = second_coord.get_y();

  int32_t box_lb_x = std::min(start_x, end_x);
  int32_t box_rt_x = std::max(start_x, end_x);
  int32_t box_lb_y = std::min(start_y, end_y);
  int32_t box_rt_y = std::max(start_y, end_y);

  std::vector<std::vector<PlanarCoord>> inflection_list_list;
  for (int32_t i = 0; i < expand_step_num; i++) {
    box_lb_x -= expand_step_length;
    box_rt_x += expand_step_length;
    box_lb_y -= expand_step_length;
    box_rt_y += expand_step_length;
    if (start_x < end_x) {
      if (start_y < end_y) {
        /**
         *    line style
         *
         *            x(e)
         *          x
         *        x
         *      x(s)
         *
         */
        if (die.get_grid_ll_y() <= box_lb_y && box_rt_x <= die.get_grid_ur_x()) {
          PlanarCoord inflection_coord1(start_x, box_lb_y);
          PlanarCoord inflection_coord2(box_rt_x, box_lb_y);
          PlanarCoord inflection_coord3(box_rt_x, end_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
        if (die.get_grid_ll_x() <= box_lb_x && box_rt_y <= die.get_grid_ur_y()) {
          PlanarCoord inflection_coord1(box_lb_x, start_y);
          PlanarCoord inflection_coord2(box_lb_x, box_rt_y);
          PlanarCoord inflection_coord3(end_x, box_rt_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
      } else {
        /**
         *    line style
         *
         *   x(s)
         *     x
         *       x
         *         x(e)
         *
         */
        if (box_rt_x <= die.get_grid_ur_x() && box_rt_y <= die.get_grid_ur_y()) {
          PlanarCoord inflection_coord1(start_x, box_rt_y);
          PlanarCoord inflection_coord2(box_rt_x, box_rt_y);
          PlanarCoord inflection_coord3(box_rt_x, end_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
        if (die.get_grid_ll_x() <= box_lb_x && die.get_grid_ll_y() <= box_lb_y) {
          PlanarCoord inflection_coord1(box_lb_x, start_y);
          PlanarCoord inflection_coord2(box_lb_x, box_lb_y);
          PlanarCoord inflection_coord3(end_x, box_lb_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
      }

    } else {
      if (start_y < end_y) {
        /**
         *    line style
         *
         *   x(e)
         *     x
         *       x
         *         x(s)
         *
         */
        if (box_rt_x <= die.get_grid_ur_x() && box_rt_y <= die.get_grid_ur_y()) {
          PlanarCoord inflection_coord1(box_rt_x, start_y);
          PlanarCoord inflection_coord2(box_rt_x, box_rt_y);
          PlanarCoord inflection_coord3(end_x, box_rt_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
        if (die.get_grid_ll_x() <= box_lb_x && die.get_grid_ll_y() <= box_lb_y) {
          PlanarCoord inflection_coord1(start_x, box_lb_y);
          PlanarCoord inflection_coord2(box_lb_x, box_lb_y);
          PlanarCoord inflection_coord3(box_lb_x, end_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
      } else {
        /**
         *    line style
         *
         *            x(s)
         *          x
         *        x
         *      x(e)
         *
         */
        if (die.get_grid_ll_y() <= box_lb_y && box_rt_x <= die.get_grid_ur_x()) {
          PlanarCoord inflection_coord1(box_rt_x, start_y);
          PlanarCoord inflection_coord2(box_rt_x, box_lb_y);
          PlanarCoord inflection_coord3(end_x, box_lb_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
        if (die.get_grid_ll_x() <= box_lb_x && box_rt_y <= die.get_grid_ur_y()) {
          PlanarCoord inflection_coord1(start_x, box_rt_y);
          PlanarCoord inflection_coord2(box_lb_x, box_rt_y);
          PlanarCoord inflection_coord3(box_lb_x, end_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
      }
    }
  }
  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (std::vector<PlanarCoord>& inflection_list : inflection_list_list) {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(planar_topo.get_first(), inflection_list.front());
    for (size_t i = 1; i < inflection_list.size(); i++) {
      routing_segment_list.emplace_back(inflection_list[i - 1], inflection_list[i]);
    }
    routing_segment_list.emplace_back(inflection_list.back(), planar_topo.get_second());
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<EREdge*> EarlyRouter::getPlanarEdgeList(ERModel& er_model, const PlanarCoord& first_coord, const PlanarCoord& second_coord)
{
  if (first_coord == second_coord) {
    return {};
  }
  if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
    RTLOG.error(Loc::current(), "The planar segment is oblique!");
  }

  bool is_horizontal = RTUTIL.isHorizontal(first_coord, second_coord);
  int32_t first_x = std::min(first_coord.get_x(), second_coord.get_x());
  int32_t second_x = std::max(first_coord.get_x(), second_coord.get_x());
  int32_t first_y = std::min(first_coord.get_y(), second_coord.get_y());
  int32_t second_y = std::max(first_coord.get_y(), second_coord.get_y());
  int32_t first_idx = is_horizontal ? first_x : first_y;
  int32_t second_idx = is_horizontal ? second_x : second_y;
  GridMap<EREdge>& edge_map = is_horizontal ? er_model.get_planar_h_edge_map() : er_model.get_planar_v_edge_map();

  std::vector<EREdge*> edge_list;
  edge_list.reserve(second_idx - first_idx);
  for (int32_t idx = first_idx; idx < second_idx; idx++) {
    int32_t edge_x = is_horizontal ? idx : first_x;
    int32_t edge_y = is_horizontal ? first_y : idx;
    if (!edge_map.isInside(edge_x, edge_y)) {
      RTLOG.error(Loc::current(), "The planar routing edge is outside the map!");
    }
    edge_list.push_back(&edge_map[edge_x][edge_y]);
  }
  return edge_list;
}

double EarlyRouter::getEdgeCost(EREdge& edge, int32_t net_idx, double overflow_unit, bool& is_blocked)
{
  if (edge.get_ignore_net_set().contains(net_idx)) {
    return 0;
  }
  if (edge.get_supply() <= 0) {
    is_blocked = true;
    return 1e12;
  }

  int32_t demand = edge.get_demand() + 1;
  if (demand > edge.get_supply()) {
    is_blocked = true;
    return overflow_unit * std::pow(demand - edge.get_supply() + 1, 4);
  }
  return overflow_unit * std::pow(demand / 1.0 / edge.get_supply(), 4);
}

void EarlyRouter::updateERCandidate(ERModel& er_model, ERCandidate& er_candidate)
{
  double overflow_unit = er_model.get_er_com_param().get_overflow_unit();
  int32_t curr_net_idx = er_model.get_curr_er_task()->get_net_idx();

  int32_t total_wire_length = 0;
  bool is_path_blocked = false;
  double total_overflow_cost = 0;
  std::unordered_set<EREdge*> candidate_edge_set;
  for (Segment<PlanarCoord>& coord_segment : er_candidate.get_routing_segment_list()) {
    PlanarCoord& first_coord = coord_segment.get_first();
    PlanarCoord& second_coord = coord_segment.get_second();
    total_wire_length += RTUTIL.getManhattanDistance(first_coord, second_coord);
    for (EREdge* edge : getPlanarEdgeList(er_model, first_coord, second_coord)) {
      candidate_edge_set.insert(edge);
    }
  }
  for (EREdge* edge : candidate_edge_set) {
    total_overflow_cost += getEdgeCost(*edge, curr_net_idx, overflow_unit, is_path_blocked);
  }
  er_candidate.set_total_wire_length(total_wire_length);
  er_candidate.set_is_path_blocked(is_path_blocked);
  er_candidate.set_total_overflow_cost(total_overflow_cost);
}

MTree<PlanarCoord> EarlyRouter::getCoordTree(ERModel& er_model, std::vector<Segment<PlanarCoord>>& routing_segment_list)
{
  std::vector<PlanarCoord> candidate_root_coord_list;
  std::map<PlanarCoord, std::set<int32_t>, CmpPlanarCoordByXASC> key_coord_pin_map;
  std::vector<ERPin>& er_pin_list = er_model.get_curr_er_task()->get_er_pin_list();
  for (size_t i = 0; i < er_pin_list.size(); i++) {
    PlanarCoord coord = er_pin_list[i].get_access_point().get_grid_coord();
    candidate_root_coord_list.push_back(coord);
    key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
  }
  return RTUTIL.getTreeByFullFlow(candidate_root_coord_list, routing_segment_list, key_coord_pin_map);
}

void EarlyRouter::uploadPlanarNetResult(ERModel& er_model, MTree<PlanarCoord>& coord_tree)
{
  for (Segment<TNode<PlanarCoord>*>& coord_segment : RTUTIL.getSegListByTree(coord_tree)) {
    er_model.get_net_global_result_map()[er_model.get_curr_er_task()->get_net_idx()].emplace_back(LayerCoord(coord_segment.get_first()->value(), 0),
                                                                                                  LayerCoord(coord_segment.get_second()->value(), 0));
  }
}

void EarlyRouter::resetSinglePlanarTask(ERModel& er_model)
{
  er_model.set_curr_er_task(nullptr);
}

void EarlyRouter::buildPlaneTree(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<ERNet>& er_net_list = er_model.get_er_net_list();

  for (auto& [net_idx, segment_set] : er_model.get_net_global_result_map()) {
    ERNet& er_net = er_net_list[net_idx];

    std::vector<Segment<LayerCoord>> routing_segment_list;
    for (Segment<LayerCoord>& segment : segment_set) {
      routing_segment_list.push_back(segment);
    }
    std::vector<LayerCoord> candidate_root_coord_list;
    std::map<LayerCoord, std::set<int32_t>, CmpLayerCoordByXASC> key_coord_pin_map;
    std::vector<ERPin>& er_pin_list = er_net.get_er_pin_list();
    for (size_t i = 0; i < er_pin_list.size(); i++) {
      LayerCoord coord(er_pin_list[i].get_access_point().get_grid_coord(), 0);
      candidate_root_coord_list.push_back(coord);
      key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
    }
    er_net.set_planar_tree(RTUTIL.getTreeByFullFlow(candidate_root_coord_list, routing_segment_list, key_coord_pin_map));
  }
  er_model.get_net_global_result_map().clear();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::assignLayer(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<ERNet*> er_task_list;
  {
    std::vector<ERNet>& er_net_list = er_model.get_er_net_list();
    er_task_list.reserve(er_net_list.size());
    for (ERNet& er_net : er_net_list) {
      er_task_list.push_back(&er_net);
    }
    std::sort(er_task_list.begin(), er_task_list.end(), CmpERNet());
  }

  int32_t batch_size = RTUTIL.getBatchSize(er_task_list.size());

  Monitor stage_monitor;
  for (size_t i = 0; i < er_task_list.size(); i++) {
    assignERTask(er_model, er_task_list[i]);
    if ((i + 1) % batch_size == 0 || (i + 1) == er_task_list.size()) {
      RTLOG.info(Loc::current(), "Routed ", (i + 1), "/", er_task_list.size(), "(", RTUTIL.getPercentage(i + 1, er_task_list.size()), ") nets",
                 stage_monitor.getStatsInfo());
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::assignERTask(ERModel& er_model, ERNet* er_task)
{
  initSingleTask(er_model, er_task);
  if (needRouting(er_model)) {
    buildPillarTree(er_model);
    assignPillarTree(er_model);
    buildLayerTree(er_model, er_task);
  }
  resetSingleLayerTask(er_model);
}

void EarlyRouter::initSingleTask(ERModel& er_model, ERNet* er_task)
{
  er_model.set_curr_er_task(er_task);
}

bool EarlyRouter::needRouting(ERModel& er_model)
{
  return (er_model.get_curr_er_task()->get_planar_tree().get_root() != nullptr);
}

void EarlyRouter::buildPillarTree(ERModel& er_model)
{
  ERNet* curr_er_task = er_model.get_curr_er_task();

  std::map<PlanarCoord, std::set<int32_t>, CmpPlanarCoordByXASC> coord_pin_layer_map;
  for (ERPin& er_pin : curr_er_task->get_er_pin_list()) {
    AccessPoint& access_point = er_pin.get_access_point();
    coord_pin_layer_map[access_point.get_grid_coord()].insert(access_point.get_layer_idx());
  }
  std::function<ERPillar(LayerCoord&, std::map<PlanarCoord, std::set<int32_t>, CmpPlanarCoordByXASC>&)> convert;
  convert = std::bind(&EarlyRouter::convertERPillar, this, std::placeholders::_1, std::placeholders::_2);
  curr_er_task->set_pillar_tree(RTUTIL.convertTree(curr_er_task->get_planar_tree(), convert, coord_pin_layer_map));
}

ERPillar EarlyRouter::convertERPillar(PlanarCoord& planar_coord, std::map<PlanarCoord, std::set<int32_t>, CmpPlanarCoordByXASC>& coord_pin_layer_map)
{
  ERPillar er_pillar;
  er_pillar.set_planar_coord(planar_coord);
  er_pillar.set_pin_layer_idx_set(coord_pin_layer_map[planar_coord]);
  return er_pillar;
}

void EarlyRouter::assignPillarTree(ERModel& er_model)
{
  buildSubtreeCost(er_model);
  selectPillarLayer(er_model);
}

void EarlyRouter::buildSubtreeCost(ERModel& er_model)
{
  TNode<ERPillar>* pillar_tree_root = er_model.get_curr_er_task()->get_pillar_tree().get_root();

  std::map<TNode<ERPillar>*, std::vector<int32_t>> candidate_layer_idx_list_map;
  std::queue<TNode<ERPillar>*> pillar_node_queue = RTUTIL.initQueue(pillar_tree_root);
  while (!pillar_node_queue.empty()) {
    TNode<ERPillar>* parent_pillar_node = RTUTIL.getFrontAndPop(pillar_node_queue);
    for (TNode<ERPillar>* child_node : parent_pillar_node->get_child_list()) {
      ERPackage er_package(parent_pillar_node, child_node);
      std::vector<int32_t> candidate_layer_idx_list = getCandidateLayerList(er_model, er_package);
      if (candidate_layer_idx_list.empty()) {
        RTLOG.error(Loc::current(), "The candidate layer list is empty!");
      }
      candidate_layer_idx_list_map.emplace(child_node, std::move(candidate_layer_idx_list));
    }
    RTUTIL.addListToQueue(pillar_node_queue, parent_pillar_node->get_child_list());
  }

  std::vector<std::vector<TNode<ERPillar>*>> level_list = RTUTIL.getLevelOrder(er_model.get_curr_er_task()->get_pillar_tree());
  const std::vector<int32_t> root_incoming_layer_idx_list{-1};
  for (int32_t i = static_cast<int32_t>(level_list.size()) - 1; i >= 0; i--) {
    for (TNode<ERPillar>* pillar_node : level_list[i]) {
      ERPillar& pillar = pillar_node->value();
      std::vector<ERLayerCost>& layer_cost_list = pillar.get_layer_cost_list();
      layer_cost_list.clear();

      const std::vector<int32_t>& incoming_layer_idx_list
          = pillar_node == pillar_tree_root ? root_incoming_layer_idx_list : candidate_layer_idx_list_map.at(pillar_node);
      layer_cost_list.reserve(incoming_layer_idx_list.size());

      std::vector<TNode<ERPillar>*>& child_list = pillar_node->get_child_list();
      std::vector<std::vector<double>> child_base_cost_list_list;
      child_base_cost_list_list.reserve(child_list.size());
      std::set<int32_t> child_boundary_layer_idx_set;
      for (TNode<ERPillar>* child_node : child_list) {
        ERPackage er_package(pillar_node, child_node);
        const std::vector<int32_t>& child_candidate_layer_idx_list = candidate_layer_idx_list_map.at(child_node);
        std::vector<ERLayerCost>& child_layer_cost_list = child_node->value().get_layer_cost_list();
        std::vector<double> child_base_cost_list;
        child_base_cost_list.reserve(child_candidate_layer_idx_list.size());
        for (size_t candidate_idx = 0; candidate_idx < child_candidate_layer_idx_list.size(); candidate_idx++) {
          int32_t layer_idx = child_candidate_layer_idx_list[candidate_idx];
          child_boundary_layer_idx_set.insert(layer_idx);
          if (candidate_idx >= child_layer_cost_list.size() || child_layer_cost_list[candidate_idx].get_layer_idx() != layer_idx) {
            RTLOG.error(Loc::current(), "The child layer cost is not found!");
          }
          double subtree_cost = child_layer_cost_list[candidate_idx].get_subtree_cost();
          child_base_cost_list.push_back(getSegmentCost(er_model, er_package, layer_idx) + subtree_cost);
        }
        child_base_cost_list_list.push_back(std::move(child_base_cost_list));
      }

      for (int32_t incoming_layer_idx : incoming_layer_idx_list) {
        std::set<int32_t> base_layer_idx_set = pillar.get_pin_layer_idx_set();
        std::set<int32_t> boundary_layer_idx_set = child_boundary_layer_idx_set;
        if (incoming_layer_idx != -1) {
          base_layer_idx_set.insert(incoming_layer_idx);
          boundary_layer_idx_set.insert(incoming_layer_idx);
        }

        double min_cost = DBL_MAX;
        std::vector<int32_t> best_child_layer_idx_list;
        if (pillar_node->isLeafNode()) {
          min_cost = getPillarViaCost(er_model, base_layer_idx_set);
        } else {
          boundary_layer_idx_set.insert(base_layer_idx_set.begin(), base_layer_idx_set.end());
          std::vector<int32_t> boundary_layer_idx_list(boundary_layer_idx_set.begin(), boundary_layer_idx_set.end());
          for (size_t low_idx = 0; low_idx < boundary_layer_idx_list.size(); low_idx++) {
            for (size_t high_idx = low_idx; high_idx < boundary_layer_idx_list.size(); high_idx++) {
              int32_t low_layer_idx = boundary_layer_idx_list[low_idx];
              int32_t high_layer_idx = boundary_layer_idx_list[high_idx];
              if (!base_layer_idx_set.empty() && (low_layer_idx > *base_layer_idx_set.begin() || high_layer_idx < *base_layer_idx_set.rbegin())) {
                continue;
              }

              double curr_cost = er_model.get_er_com_param().get_via_unit() * (high_layer_idx - low_layer_idx);
              std::vector<int32_t> curr_child_layer_idx_list;
              curr_child_layer_idx_list.reserve(child_list.size());
              bool is_valid = true;
              for (size_t child_idx = 0; child_idx < child_list.size(); child_idx++) {
                const std::vector<int32_t>& child_candidate_layer_idx_list = candidate_layer_idx_list_map.at(child_list[child_idx]);
                double child_min_cost = DBL_MAX;
                int32_t best_child_layer_idx = -1;
                for (size_t candidate_idx = 0; candidate_idx < child_candidate_layer_idx_list.size(); candidate_idx++) {
                  int32_t child_layer_idx = child_candidate_layer_idx_list[candidate_idx];
                  if (child_layer_idx < low_layer_idx || high_layer_idx < child_layer_idx) {
                    continue;
                  }
                  double child_cost = child_base_cost_list_list[child_idx][candidate_idx];
                  if (child_cost < child_min_cost || (child_cost == child_min_cost && child_layer_idx < best_child_layer_idx)) {
                    child_min_cost = child_cost;
                    best_child_layer_idx = child_layer_idx;
                  }
                }
                if (child_min_cost == DBL_MAX) {
                  is_valid = false;
                  break;
                }
                curr_cost += child_min_cost;
                curr_child_layer_idx_list.push_back(best_child_layer_idx);
              }
              if (!is_valid) {
                continue;
              }
              if (curr_cost < min_cost || (curr_cost == min_cost && curr_child_layer_idx_list < best_child_layer_idx_list)) {
                min_cost = curr_cost;
                best_child_layer_idx_list = std::move(curr_child_layer_idx_list);
              }
            }
          }
        }
        if (min_cost == DBL_MAX) {
          RTLOG.error(Loc::current(), "The min subtree cost is wrong!");
        }

        ERLayerCost layer_cost;
        layer_cost.set_layer_idx(incoming_layer_idx);
        layer_cost.set_subtree_cost(min_cost);
        layer_cost.set_child_layer_idx_list(best_child_layer_idx_list);
        layer_cost_list.push_back(std::move(layer_cost));
      }
    }
  }
}

double EarlyRouter::getPillarViaCost(ERModel& er_model, const std::set<int32_t>& layer_idx_set)
{
  if (layer_idx_set.empty()) {
    return 0;
  }
  return er_model.get_er_com_param().get_via_unit() * (*layer_idx_set.rbegin() - *layer_idx_set.begin());
}

void EarlyRouter::selectPillarLayer(ERModel& er_model)
{
  TNode<ERPillar>* pillar_tree_root = er_model.get_curr_er_task()->get_pillar_tree().get_root();
  pillar_tree_root->value().set_layer_idx(-1);

  std::queue<TNode<ERPillar>*> pillar_node_queue = RTUTIL.initQueue(pillar_tree_root);
  while (!pillar_node_queue.empty()) {
    TNode<ERPillar>* pillar_node = RTUTIL.getFrontAndPop(pillar_node_queue);
    ERLayerCost* selected_layer_cost = nullptr;
    for (ERLayerCost& layer_cost : pillar_node->value().get_layer_cost_list()) {
      if (layer_cost.get_layer_idx() == pillar_node->value().get_layer_idx()) {
        selected_layer_cost = &layer_cost;
        break;
      }
    }
    if (selected_layer_cost == nullptr) {
      RTLOG.error(Loc::current(), "The selected layer cost is not found!");
    }

    std::vector<TNode<ERPillar>*>& child_list = pillar_node->get_child_list();
    std::vector<int32_t>& child_layer_idx_list = selected_layer_cost->get_child_layer_idx_list();
    if (child_list.size() != child_layer_idx_list.size()) {
      RTLOG.error(Loc::current(), "The child layer count is wrong!");
    }
    for (size_t i = 0; i < child_list.size(); i++) {
      child_list[i]->value().set_layer_idx(child_layer_idx_list[i]);
    }
    RTUTIL.addListToQueue(pillar_node_queue, child_list);
  }
}

std::vector<int32_t> EarlyRouter::getCandidateLayerList(ERModel& er_model, ERPackage& er_package)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  int32_t bottom_routing_layer_idx = RTDM.getConfig().bottom_routing_layer_idx;
  int32_t top_routing_layer_idx = RTDM.getConfig().top_routing_layer_idx;

  Direction direction = RTUTIL.getDirection(er_package.getParentPillar().get_planar_coord(), er_package.getChildPillar().get_planar_coord());

  std::vector<int32_t> candidate_layer_idx_list;
  for (RoutingLayer& routing_layer : routing_layer_list) {
    if (routing_layer.get_layer_idx() < bottom_routing_layer_idx || top_routing_layer_idx < routing_layer.get_layer_idx()) {
      continue;
    }
    if (direction == Direction::kProximal) {
      candidate_layer_idx_list.push_back(routing_layer.get_layer_idx());
    } else if (direction == routing_layer.get_prefer_direction()) {
      candidate_layer_idx_list.push_back(routing_layer.get_layer_idx());
    }
  }
  return candidate_layer_idx_list;
}

std::vector<EREdge*> EarlyRouter::getLayerEdgeList(ERModel& er_model, int32_t layer_idx, const PlanarCoord& first_coord, const PlanarCoord& second_coord)
{
  if (first_coord == second_coord) {
    return {};
  }
  if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
    RTLOG.error(Loc::current(), "The layer segment is oblique!");
  }
  if (layer_idx < 0 || static_cast<int32_t>(er_model.get_layer_h_edge_map().size()) <= layer_idx) {
    RTLOG.error(Loc::current(), "The routing layer is outside the Edge map!");
  }

  bool is_horizontal = RTUTIL.isHorizontal(first_coord, second_coord);
  int32_t first_x = std::min(first_coord.get_x(), second_coord.get_x());
  int32_t second_x = std::max(first_coord.get_x(), second_coord.get_x());
  int32_t first_y = std::min(first_coord.get_y(), second_coord.get_y());
  int32_t second_y = std::max(first_coord.get_y(), second_coord.get_y());
  int32_t first_idx = is_horizontal ? first_x : first_y;
  int32_t second_idx = is_horizontal ? second_x : second_y;
  GridMap<EREdge>& edge_map = is_horizontal ? er_model.get_layer_h_edge_map()[layer_idx] : er_model.get_layer_v_edge_map()[layer_idx];

  std::vector<EREdge*> edge_list;
  edge_list.reserve(second_idx - first_idx);
  for (int32_t idx = first_idx; idx < second_idx; idx++) {
    int32_t edge_x = is_horizontal ? idx : first_x;
    int32_t edge_y = is_horizontal ? first_y : idx;
    if (!edge_map.isInside(edge_x, edge_y)) {
      RTLOG.error(Loc::current(), "The layer routing Edge is outside the map!");
    }
    edge_list.push_back(&edge_map[edge_x][edge_y]);
  }
  return edge_list;
}

double EarlyRouter::getSegmentCost(ERModel& er_model, ERPackage& er_package, int32_t candidate_layer_idx)
{
  double overflow_unit = er_model.get_er_com_param().get_overflow_unit();
  int32_t net_idx = er_model.get_curr_er_task()->get_net_idx();
  PlanarCoord first_coord = er_package.getParentPillar().get_planar_coord();
  PlanarCoord second_coord = er_package.getChildPillar().get_planar_coord();

  double edge_cost = 0;
  bool is_blocked = false;
  for (EREdge* edge : getLayerEdgeList(er_model, candidate_layer_idx, first_coord, second_coord)) {
    edge_cost += getEdgeCost(*edge, net_idx, overflow_unit, is_blocked);
  }
  return edge_cost;
}

void EarlyRouter::buildLayerTree(ERModel& er_model, ERNet* er_task)
{
  std::vector<Segment<LayerCoord>> routing_segment_list = getLayerRoutingSegmentList(er_model);
  MTree<LayerCoord> coord_tree = getCoordTree(er_model, routing_segment_list);
  updateDemandToGraph(er_model, ChangeType::kAdd, coord_tree);
  uploadLayerNetResult(er_model, coord_tree);
}

std::vector<Segment<LayerCoord>> EarlyRouter::getLayerRoutingSegmentList(ERModel& er_model)
{
  std::vector<Segment<LayerCoord>> routing_segment_list;

  TNode<ERPillar>* pillar_tree_root = er_model.get_curr_er_task()->get_pillar_tree().get_root();
  std::queue<TNode<ERPillar>*> pillar_node_queue = RTUTIL.initQueue(pillar_tree_root);
  while (!pillar_node_queue.empty()) {
    TNode<ERPillar>* parent_pillar_node = RTUTIL.getFrontAndPop(pillar_node_queue);
    std::vector<TNode<ERPillar>*>& child_list = parent_pillar_node->get_child_list();
    {
      int32_t bottom_layer_idx = std::numeric_limits<int32_t>::max();
      int32_t top_layer_idx = std::numeric_limits<int32_t>::min();
      for (int32_t layer_idx : parent_pillar_node->value().get_pin_layer_idx_set()) {
        bottom_layer_idx = std::min(bottom_layer_idx, layer_idx);
        top_layer_idx = std::max(top_layer_idx, layer_idx);
      }
      if (parent_pillar_node != pillar_tree_root) {
        int32_t layer_idx = parent_pillar_node->value().get_layer_idx();
        bottom_layer_idx = std::min(bottom_layer_idx, layer_idx);
        top_layer_idx = std::max(top_layer_idx, layer_idx);
      }
      for (TNode<ERPillar>* child_node : child_list) {
        int32_t layer_idx = child_node->value().get_layer_idx();
        bottom_layer_idx = std::min(bottom_layer_idx, layer_idx);
        top_layer_idx = std::max(top_layer_idx, layer_idx);
      }
      if (bottom_layer_idx <= top_layer_idx) {
        routing_segment_list.emplace_back(LayerCoord(parent_pillar_node->value().get_planar_coord(), bottom_layer_idx),
                                          LayerCoord(parent_pillar_node->value().get_planar_coord(), top_layer_idx));
      }
    }
    for (TNode<ERPillar>* child_node : child_list) {
      routing_segment_list.emplace_back(LayerCoord(parent_pillar_node->value().get_planar_coord(), child_node->value().get_layer_idx()),
                                        LayerCoord(child_node->value().get_planar_coord(), child_node->value().get_layer_idx()));
    }
    RTUTIL.addListToQueue(pillar_node_queue, child_list);
  }
  return routing_segment_list;
}

MTree<LayerCoord> EarlyRouter::getCoordTree(ERModel& er_model, std::vector<Segment<LayerCoord>>& routing_segment_list)
{
  std::vector<LayerCoord> candidate_root_coord_list;
  std::map<LayerCoord, std::set<int32_t>, CmpLayerCoordByXASC> key_coord_pin_map;
  std::vector<ERPin>& er_pin_list = er_model.get_curr_er_task()->get_er_pin_list();
  for (size_t i = 0; i < er_pin_list.size(); i++) {
    LayerCoord coord = er_pin_list[i].get_access_point().getGridLayerCoord();
    candidate_root_coord_list.push_back(coord);
    key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
  }
  return RTUTIL.getTreeByFullFlow(candidate_root_coord_list, routing_segment_list, key_coord_pin_map);
}

void EarlyRouter::uploadLayerNetResult(ERModel& er_model, MTree<LayerCoord>& coord_tree)
{
  for (Segment<TNode<LayerCoord>*>& coord_segment : RTUTIL.getSegListByTree(coord_tree)) {
    er_model.get_net_global_result_map()[er_model.get_curr_er_task()->get_net_idx()].emplace_back(coord_segment.get_first()->value(),
                                                                                                  coord_segment.get_second()->value());
  }
}

void EarlyRouter::resetSingleLayerTask(ERModel& er_model)
{
  er_model.set_curr_er_task(nullptr);
}

void EarlyRouter::initERPanelMap(ERModel& er_model)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  std::vector<std::vector<ERPanel>>& layer_panel_list = er_model.get_layer_panel_list();
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::vector<ERPanel> er_panel_list;
    if (routing_layer.isPreferH()) {
      for (ScaleGrid& gcell_grid : gcell_axis.get_y_grid_list()) {
        for (int32_t line = gcell_grid.get_start_line(); line < gcell_grid.get_end_line(); line += gcell_grid.get_step_length()) {
          ERPanel er_panel;
          EXTLayerRect er_panel_rect;
          er_panel_rect.set_real_ll(die.get_real_ll_x(), line);
          er_panel_rect.set_real_ur(die.get_real_ur_x(), line + gcell_grid.get_step_length());
          er_panel_rect.set_grid_rect(RTUTIL.getOpenGCellGridRect(er_panel_rect.get_real_rect(), gcell_axis));
          er_panel_rect.set_layer_idx(routing_layer.get_layer_idx());
          er_panel.set_panel_rect(er_panel_rect);
          ERPanelId er_panel_id;
          er_panel_id.set_layer_idx(routing_layer.get_layer_idx());
          er_panel_id.set_panel_idx(static_cast<int32_t>(er_panel_list.size()));
          er_panel.set_er_panel_id(er_panel_id);
          er_panel_list.push_back(er_panel);
        }
      }
    } else {
      for (ScaleGrid& gcell_grid : gcell_axis.get_x_grid_list()) {
        for (int32_t line = gcell_grid.get_start_line(); line < gcell_grid.get_end_line(); line += gcell_grid.get_step_length()) {
          ERPanel er_panel;
          EXTLayerRect er_panel_rect;
          er_panel_rect.set_real_ll(line, die.get_real_ll_y());
          er_panel_rect.set_real_ur(line + gcell_grid.get_step_length(), die.get_real_ur_y());
          er_panel_rect.set_grid_rect(RTUTIL.getOpenGCellGridRect(er_panel_rect.get_real_rect(), gcell_axis));
          er_panel_rect.set_layer_idx(routing_layer.get_layer_idx());
          er_panel.set_panel_rect(er_panel_rect);
          ERPanelId er_panel_id;
          er_panel_id.set_layer_idx(routing_layer.get_layer_idx());
          er_panel_id.set_panel_idx(static_cast<int32_t>(er_panel_list.size()));
          er_panel.set_er_panel_id(er_panel_id);
          er_panel_list.push_back(er_panel);
        }
      }
    }
    layer_panel_list.push_back(er_panel_list);
  }
}

void EarlyRouter::buildPanelSchedule(ERModel& er_model)
{
  std::vector<std::vector<ERPanel>>& layer_panel_list = er_model.get_layer_panel_list();
  int32_t schedule_interval = er_model.get_er_com_param().get_schedule_interval();

  std::vector<std::vector<ERPanelId>> er_panel_id_list_list;
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(layer_panel_list.size()); layer_idx++) {
    for (int32_t start_i = 0; start_i < schedule_interval; start_i++) {
      std::vector<ERPanelId> er_panel_id_list;
      for (int32_t i = start_i; i < static_cast<int32_t>(layer_panel_list[layer_idx].size()); i += schedule_interval) {
        er_panel_id_list.emplace_back(layer_idx, i);
      }
      er_panel_id_list_list.push_back(er_panel_id_list);
    }
  }
  er_model.set_er_panel_id_list_list(er_panel_id_list_list);
}

void EarlyRouter::assignTrack(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<std::vector<ERPanel>>& layer_panel_list = er_model.get_layer_panel_list();

  size_t total_panel_num = 0;
  for (std::vector<ERPanelId>& er_panel_id_list : er_model.get_er_panel_id_list_list()) {
    total_panel_num += er_panel_id_list.size();
  }

  size_t assigned_panel_num = 0;
  for (std::vector<ERPanelId>& er_panel_id_list : er_model.get_er_panel_id_list_list()) {
    Monitor stage_monitor;
#pragma omp parallel for
    for (ERPanelId& er_panel_id : er_panel_id_list) {
      ERPanel& er_panel = layer_panel_list[er_panel_id.get_layer_idx()][er_panel_id.get_panel_idx()];
      routeERPanel(er_model, er_panel);
    }
    for (ERPanelId& er_panel_id : er_panel_id_list) {
      ERPanel& er_panel = layer_panel_list[er_panel_id.get_layer_idx()][er_panel_id.get_panel_idx()];
      for (auto& [net_idx, segment_list] : er_panel.get_net_detailed_result_map()) {
        std::vector<Segment<LayerCoord>>& model_segment_list = er_model.get_net_detailed_result_map()[net_idx];
        model_segment_list.insert(model_segment_list.end(), std::make_move_iterator(segment_list.begin()), std::make_move_iterator(segment_list.end()));
      }
      er_panel.get_net_detailed_result_map().clear();
    }
    assigned_panel_num += er_panel_id_list.size();
    RTLOG.info(Loc::current(), "Assigned ", assigned_panel_num, "/", total_panel_num, "(", RTUTIL.getPercentage(assigned_panel_num, total_panel_num),
               ") panels", stage_monitor.getStatsInfo());
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::routeERPanel(ERModel& er_model, ERPanel& er_panel)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  std::vector<ERModel::GlobalResultRTree::value_type> value_list;
  er_model.get_global_result_rtree().query(bgi::intersects(RTUTIL.convertToBGRectInt(er_panel.get_panel_rect().get_grid_rect())),
                                           std::back_inserter(value_list));
  for (auto& [rect, net_segment_idx] : value_list) {
    int32_t net_idx = net_segment_idx.first;
    Segment<LayerCoord>* segment = &er_model.get_net_global_result_map().at(net_idx).at(net_segment_idx.second);
    LayerCoord& first_coord = segment->get_first();
    LayerCoord& second_coord = segment->get_second();
    if (first_coord.get_layer_idx() != second_coord.get_layer_idx()) {
      continue;
    }
    if (first_coord.get_layer_idx() != er_panel.get_er_panel_id().get_layer_idx()) {
      continue;
    }
    PlanarRect ll_rect = RTUTIL.getRealRectByGCell(first_coord, gcell_axis);
    PlanarRect ur_rect = RTUTIL.getRealRectByGCell(second_coord, gcell_axis);
    int32_t layer_idx = first_coord.get_layer_idx();

    RoutingLayer& routing_layer = routing_layer_list[layer_idx];
    std::vector<ScaleGrid>& x_track_grid_list = routing_layer.getXTrackGridList();
    std::vector<ScaleGrid>& y_track_grid_list = routing_layer.getYTrackGridList();

    if (RTUTIL.isHorizontal(first_coord, second_coord)) {
      RTUTIL.swapByCMP(ll_rect, ur_rect, [](PlanarRect& a, PlanarRect& b) { return CmpPlanarCoordByXASC()(a.getMidPoint(), b.getMidPoint()); });
      std::vector<int32_t> ll_scale_list = RTUTIL.getScaleList(ll_rect.get_ll_x(), ll_rect.get_ur_x(), x_track_grid_list);
      std::vector<int32_t> ur_scale_list = RTUTIL.getScaleList(ur_rect.get_ll_x(), ur_rect.get_ur_x(), x_track_grid_list);
      auto ll_iter = ll_scale_list.rbegin();
      auto ur_iter = ur_scale_list.begin();
      while (ll_iter != ll_scale_list.rend() && ur_iter != ur_scale_list.end()) {
        if (*ll_iter != *ur_iter) {
          break;
        }
        ++ll_iter;
        ++ur_iter;
      }
      int32_t ll_x = *ll_iter;
      int32_t ur_x = *ur_iter;
      std::vector<int32_t> scale_list = RTUTIL.getScaleList(ll_rect.get_ll_y(), ll_rect.get_ur_y(), y_track_grid_list);
      int32_t y = scale_list[ll_x % scale_list.size()];
      er_panel.get_net_detailed_result_map()[net_idx].emplace_back(LayerCoord(ll_x, y, layer_idx), LayerCoord(ur_x, y, layer_idx));

    } else if (RTUTIL.isVertical(first_coord, second_coord)) {
      RTUTIL.swapByCMP(ll_rect, ur_rect, [](PlanarRect& a, PlanarRect& b) { return CmpPlanarCoordByYASC()(a.getMidPoint(), b.getMidPoint()); });
      std::vector<int32_t> ll_scale_list = RTUTIL.getScaleList(ll_rect.get_ll_y(), ll_rect.get_ur_y(), y_track_grid_list);
      std::vector<int32_t> ur_scale_list = RTUTIL.getScaleList(ur_rect.get_ll_y(), ur_rect.get_ur_y(), y_track_grid_list);
      auto ll_iter = ll_scale_list.rbegin();
      auto ur_iter = ur_scale_list.begin();
      while (ll_iter != ll_scale_list.rend() && ur_iter != ur_scale_list.end()) {
        if (*ll_iter != *ur_iter) {
          break;
        }
        ++ll_iter;
        ++ur_iter;
      }
      int32_t ll_y = *ll_iter;
      int32_t ur_y = *ur_iter;
      std::vector<int32_t> scale_list = RTUTIL.getScaleList(ll_rect.get_ll_x(), ll_rect.get_ur_x(), x_track_grid_list);
      int32_t x = scale_list[ll_y % scale_list.size()];
      er_panel.get_net_detailed_result_map()[net_idx].emplace_back(LayerCoord(x, ll_y, layer_idx), LayerCoord(x, ur_y, layer_idx));
    }
  }
}

void EarlyRouter::initERBoxMap(ERModel& er_model)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();

  std::vector<int32_t> x_scale_list;
  {
    int32_t x_gcell_num = 0;
    for (ScaleGrid& x_grid : gcell_axis.get_x_grid_list()) {
      x_gcell_num += x_grid.get_step_num();
    }
    x_scale_list.push_back(0);
    for (int32_t x_scale = 0; x_scale <= x_gcell_num; x_scale += 1) {
      x_scale_list.push_back(x_scale);
    }
    x_scale_list.push_back(x_gcell_num);
    std::sort(x_scale_list.begin(), x_scale_list.end());
    x_scale_list.erase(std::unique(x_scale_list.begin(), x_scale_list.end()), x_scale_list.end());
  }
  std::vector<int32_t> y_scale_list;
  {
    int32_t y_gcell_num = 0;
    for (ScaleGrid& y_grid : gcell_axis.get_y_grid_list()) {
      y_gcell_num += y_grid.get_step_num();
    }
    y_scale_list.push_back(0);
    for (int32_t y_scale = 0; y_scale <= y_gcell_num; y_scale += 1) {
      y_scale_list.push_back(y_scale);
    }
    y_scale_list.push_back(y_gcell_num);
    std::sort(y_scale_list.begin(), y_scale_list.end());
    y_scale_list.erase(std::unique(y_scale_list.begin(), y_scale_list.end()), y_scale_list.end());
  }
  GridMap<ERBox>& er_box_map = er_model.get_er_box_map();
  {
    int32_t x_box_num = static_cast<int32_t>(x_scale_list.size()) - 1;
    int32_t y_box_num = static_cast<int32_t>(y_scale_list.size()) - 1;
    er_box_map.init(x_box_num, y_box_num);
  }
  for (int32_t x = 0; x < er_box_map.get_x_size(); x++) {
    for (int32_t y = 0; y < er_box_map.get_y_size(); y++) {
      int32_t grid_ll_x = x_scale_list[x];
      int32_t grid_ll_y = y_scale_list[y];
      int32_t grid_ur_x = x_scale_list[x + 1] - 1;
      int32_t grid_ur_y = y_scale_list[y + 1] - 1;

      PlanarRect ll_gcell_rect = RTUTIL.getRealRectByGCell(PlanarCoord(grid_ll_x, grid_ll_y), gcell_axis);
      PlanarRect ur_gcell_rect = RTUTIL.getRealRectByGCell(PlanarCoord(grid_ur_x, grid_ur_y), gcell_axis);
      PlanarRect box_real_rect(ll_gcell_rect.get_ll(), ur_gcell_rect.get_ur());

      ERBox& er_box = er_box_map[x][y];

      EXTPlanarRect er_box_rect;
      er_box_rect.set_real_rect(box_real_rect);
      er_box_rect.set_grid_rect(RTUTIL.getOpenGCellGridRect(box_real_rect, gcell_axis));
      er_box.set_box_rect(er_box_rect);
      ERBoxId er_box_id;
      er_box_id.set_x(x);
      er_box_id.set_y(y);
      er_box.set_er_box_id(er_box_id);
    }
  }
}

void EarlyRouter::buildBoxSchedule(ERModel& er_model)
{
  GridMap<ERBox>& er_box_map = er_model.get_er_box_map();
  int32_t schedule_interval = er_model.get_er_com_param().get_schedule_interval();

  std::vector<std::vector<ERBoxId>> er_box_id_list_list;
  for (int32_t start_x = 0; start_x < schedule_interval; start_x++) {
    for (int32_t start_y = 0; start_y < schedule_interval; start_y++) {
      std::vector<ERBoxId> er_box_id_list;
      for (int32_t x = start_x; x < er_box_map.get_x_size(); x += schedule_interval) {
        for (int32_t y = start_y; y < er_box_map.get_y_size(); y += schedule_interval) {
          er_box_id_list.emplace_back(x, y);
        }
      }
      if (!er_box_id_list.empty()) {
        er_box_id_list_list.push_back(er_box_id_list);
      }
    }
  }
  er_model.set_er_box_id_list_list(er_box_id_list_list);
}

void EarlyRouter::routeDetailed(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<ERBox>& er_box_map = er_model.get_er_box_map();
  for (auto& [net_idx, segment_list] : er_model.get_net_detailed_result_map()) {
    for (Segment<LayerCoord>& segment : segment_list) {
      addNetResultToERBoxTask(er_model, net_idx, segment);
    }
  }

  size_t total_box_num = 0;
  for (std::vector<ERBoxId>& er_box_id_list : er_model.get_er_box_id_list_list()) {
    total_box_num += er_box_id_list.size();
  }

  size_t routed_box_num = 0;
  for (std::vector<ERBoxId>& er_box_id_list : er_model.get_er_box_id_list_list()) {
    Monitor stage_monitor;
#pragma omp parallel for
    for (ERBoxId& er_box_id : er_box_id_list) {
      ERBox& er_box = er_box_map[er_box_id.get_x()][er_box_id.get_y()];
      routeERBox(er_model, er_box);
    }
    for (ERBoxId& er_box_id : er_box_id_list) {
      ERBox& er_box = er_box_map[er_box_id.get_x()][er_box_id.get_y()];
      for (auto& [net_idx, segment_list] : er_box.get_net_detailed_result_map()) {
        for (Segment<LayerCoord>& segment : segment_list) {
          addNetResultToERBoxTask(er_model, net_idx, segment);
        }
        std::vector<Segment<LayerCoord>>& model_segment_list = er_model.get_net_detailed_result_map()[net_idx];
        model_segment_list.insert(model_segment_list.end(), std::make_move_iterator(segment_list.begin()), std::make_move_iterator(segment_list.end()));
      }
      er_box.get_net_detailed_result_map().clear();
    }
    routed_box_num += er_box_id_list.size();
    RTLOG.info(Loc::current(), "Routed ", routed_box_num, "/", total_box_num, "(", RTUTIL.getPercentage(routed_box_num, total_box_num), ") boxes",
               stage_monitor.getStatsInfo());
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::addNetResultToERBoxTask(ERModel& er_model, int32_t net_idx, Segment<LayerCoord>& segment)
{
  GridMap<ERBox>& er_box_map = er_model.get_er_box_map();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::set<ERBoxId, CmpERBoxId> box_id_set;
  for (LayerCoord* coord : {&segment.get_first(), &segment.get_second()}) {
    PlanarRect coord_rect(coord->get_x(), coord->get_y(), coord->get_x(), coord->get_y());
    PlanarRect grid_rect = RTUTIL.getClosedGCellGridRect(coord_rect, gcell_axis);
    for (int32_t x = grid_rect.get_ll_x(); x <= grid_rect.get_ur_x(); x++) {
      for (int32_t y = grid_rect.get_ll_y(); y <= grid_rect.get_ur_y(); y++) {
        if (RTUTIL.isInside(er_box_map[x][y].get_box_rect().get_real_rect(), *coord)) {
          box_id_set.emplace(x, y);
        }
      }
    }
  }
  for (const ERBoxId& box_id : box_id_set) {
    er_box_map[box_id.get_x()][box_id.get_y()].get_net_task_detailed_result_map()[net_idx].push_back(segment);
  }
}

void EarlyRouter::routeERBox(ERModel& er_model, ERBox& er_box)
{
  int32_t bottom_routing_layer_idx = RTDM.getConfig().bottom_routing_layer_idx;
  int32_t top_routing_layer_idx = RTDM.getConfig().top_routing_layer_idx;

  EXTPlanarRect& box_rect = er_box.get_box_rect();
  PlanarRect& box_real_rect = box_rect.get_real_rect();

  std::map<int32_t, std::set<AccessPoint*, CmpAccessPoint>> net_access_point_map;
  PlanarRect query_rect = RTUTIL.getEnlargedRect(box_rect.get_real_rect(), RTDM.getDatabase().get_detection_distance());
  std::vector<ERModel::AccessPointRTree::value_type> value_list;
  er_model.get_access_point_rtree().query(bgi::intersects(RTUTIL.convertToBGRectInt(query_rect)), std::back_inserter(value_list));
  for (auto& [rect, net_access_point] : value_list) {
    net_access_point_map[net_access_point.first].insert(net_access_point.second);
  }
  std::map<int32_t, std::vector<Segment<LayerCoord>>>& net_task_detailed_result_map = er_box.get_net_task_detailed_result_map();

  std::map<int32_t, std::vector<std::vector<LayerCoord>>> net_coord_list_list_map;
  {
    for (auto& [net_idx, access_point_set] : net_access_point_map) {
      std::map<int32_t, std::vector<LayerCoord>> pin_coord_list_map;
      for (AccessPoint* access_point : access_point_set) {
        if (!RTUTIL.isInside(box_real_rect, access_point->get_real_coord())) {
          continue;
        }
        pin_coord_list_map[access_point->get_pin_idx()].push_back(access_point->getRealLayerCoord());
      }
      for (auto& [pin_idx, coord_list] : pin_coord_list_map) {
        net_coord_list_list_map[net_idx].push_back(coord_list);
      }
    }
    for (auto& [net_idx, segment_list] : net_task_detailed_result_map) {
      std::vector<LayerCoord> coord_list;
      for (const Segment<LayerCoord>& segment : segment_list) {
        const LayerCoord& first = segment.get_first();
        const LayerCoord& second = segment.get_second();
        if (first.get_layer_idx() != second.get_layer_idx()) {
          continue;
        }
        if (RTUTIL.isHorizontal(first, second)) {
          int32_t first_x = first.get_x();
          int32_t second_x = second.get_x();
          if (first.get_y() < box_real_rect.get_ll_y() || box_real_rect.get_ur_y() < first.get_y()) {
            continue;
          }
          RTUTIL.swapByASC(first_x, second_x);
          if (first_x <= box_real_rect.get_ll_x() && box_real_rect.get_ll_x() <= second_x) {
            coord_list.emplace_back(box_real_rect.get_ll_x(), first.get_y(), first.get_layer_idx());
          }
          if (first_x <= box_real_rect.get_ur_x() && box_real_rect.get_ur_x() <= second_x) {
            coord_list.emplace_back(box_real_rect.get_ur_x(), first.get_y(), first.get_layer_idx());
          }
        } else if (RTUTIL.isVertical(first, second)) {
          int32_t first_y = first.get_y();
          int32_t second_y = second.get_y();
          if (first.get_x() < box_real_rect.get_ll_x() || box_real_rect.get_ur_x() < first.get_x()) {
            continue;
          }
          RTUTIL.swapByASC(first_y, second_y);
          if (first_y <= box_real_rect.get_ll_y() && box_real_rect.get_ll_y() <= second_y) {
            coord_list.emplace_back(first.get_x(), box_real_rect.get_ll_y(), first.get_layer_idx());
          }
          if (first_y <= box_real_rect.get_ur_y() && box_real_rect.get_ur_y() <= second_y) {
            coord_list.emplace_back(first.get_x(), box_real_rect.get_ur_y(), first.get_layer_idx());
          }
        }
      }
      for (LayerCoord& coord : coord_list) {
        net_coord_list_list_map[net_idx].push_back({coord});
      }
    }
  }
  for (auto& [net_idx, coord_list_list] : net_coord_list_list_map) {
    if (coord_list_list.size() < 2) {
      continue;
    }
    std::vector<LayerCoord> connect_coord_list;
    for (std::vector<LayerCoord>& coord_list : coord_list_list) {
      connect_coord_list.push_back(coord_list.front());
    }
    std::sort(connect_coord_list.begin(), connect_coord_list.end(), CmpLayerCoordByLayerASC());
    connect_coord_list.erase(std::unique(connect_coord_list.begin(), connect_coord_list.end()), connect_coord_list.end());

    LayerCoord balance_coord = RTUTIL.getBalanceCoord(connect_coord_list);
    balance_coord.set_layer_idx(std::clamp(balance_coord.get_layer_idx(), bottom_routing_layer_idx, top_routing_layer_idx));

    for (LayerCoord& connect_coord : connect_coord_list) {
      LayerCoord inflection_coord1(connect_coord.get_x(), connect_coord.get_y(), balance_coord.get_layer_idx());

      std::vector<Segment<LayerCoord>> routing_segment_list;
      routing_segment_list.emplace_back(connect_coord, inflection_coord1);

      if (RTUTIL.isOblique(inflection_coord1, balance_coord)) {
        LayerCoord inflection_coord2(inflection_coord1.get_x(), balance_coord.get_y(), balance_coord.get_layer_idx());
        routing_segment_list.emplace_back(inflection_coord1, inflection_coord2);
        routing_segment_list.emplace_back(inflection_coord2, balance_coord);
      } else {
        routing_segment_list.emplace_back(inflection_coord1, balance_coord);
      }

      for (Segment<LayerCoord>& routing_segment : routing_segment_list) {
        er_box.get_net_detailed_result_map()[net_idx].push_back(std::move(routing_segment));
      }
    }
  }
}

void EarlyRouter::updateNetResult(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<ERNet>& er_net_list = er_model.get_er_net_list();

  // detailed result
  {
    std::vector<std::vector<Segment<LayerCoord>>> detailed_result_list(er_net_list.size());
    for (auto& [net_idx, segment_list] : er_model.get_net_detailed_result_map()) {
      detailed_result_list[net_idx] = std::move(segment_list);
    }
    er_model.get_net_detailed_result_map().clear();
    std::vector<std::vector<Segment<LayerCoord>>> new_detailed_result_list(er_net_list.size());
#pragma omp parallel for
    for (int32_t net_idx = 0; net_idx < static_cast<int32_t>(detailed_result_list.size()); net_idx++) {
      std::vector<Segment<LayerCoord>>& routing_segment_list = detailed_result_list[net_idx];
      std::vector<LayerCoord> candidate_root_coord_list;
      std::map<LayerCoord, std::set<int32_t>, CmpLayerCoordByXASC> key_coord_pin_map;
      std::vector<ERPin>& er_pin_list = er_net_list[net_idx].get_er_pin_list();
      for (size_t i = 0; i < er_pin_list.size(); i++) {
        LayerCoord coord = er_pin_list[i].get_access_point().getRealLayerCoord();
        candidate_root_coord_list.push_back(coord);
        key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
      }
      MTree<LayerCoord> coord_tree = RTUTIL.getTreeByFullFlow(candidate_root_coord_list, routing_segment_list, key_coord_pin_map);
      for (Segment<TNode<LayerCoord>*>& coord_segment : RTUTIL.getSegListByTree(coord_tree)) {
        new_detailed_result_list[net_idx].emplace_back(coord_segment.get_first()->value(), coord_segment.get_second()->value());
      }
    }
    for (int32_t net_idx = 0; net_idx < static_cast<int32_t>(new_detailed_result_list.size()); net_idx++) {
      if (!new_detailed_result_list[net_idx].empty()) {
        er_model.get_net_detailed_result_map()[net_idx] = std::move(new_detailed_result_list[net_idx]);
      }
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::updateNetPatch(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  er_model.get_net_detailed_patch_map().clear();

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::cleanTempResult(ERModel& er_model)
{
  er_model.get_net_detailed_result_map().clear();
  er_model.get_net_detailed_patch_map().clear();
}

void EarlyRouter::uploadERModel(ERModel& er_model)
{
  for (ERNet& er_net : er_model.get_er_net_list()) {
    Net* origin_net = er_net.get_origin_net();
    origin_net->set_bounding_box(er_net.get_bounding_box());
    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      origin_net->get_pin_list()[er_pin.get_pin_idx()].set_access_point(er_pin.get_access_point());
    }
  }
  RTDM.rebuildAccessPointRTree();
  RTDM.getDatabase().get_net_global_result_map() = std::move(er_model.get_net_global_result_map());
  RTDM.rebuildGlobalResultRTree();
  RTDM.getDatabase().get_net_detailed_result_map() = std::move(er_model.get_net_detailed_result_map());
  RTDM.getDatabase().get_net_detailed_patch_map() = std::move(er_model.get_net_detailed_patch_map());
}

#if 1  // output

void EarlyRouter::outputGCellCSV(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::string& er_temp_directory_path = RTDM.getConfig().er_temp_directory_path;

  std::ofstream* guide_file_stream = RTUTIL.getOutputFileStream(RTUTIL.getString(er_temp_directory_path, "gcell.info"));
  if (guide_file_stream == nullptr) {
    return;
  }
  for (int32_t x = 0; x < gcell_map.get_x_size(); x++) {
    for (int32_t y = 0; y < gcell_map.get_y_size(); y++) {
      PlanarRect& gcell = gcell_map[x][y];
      RTUTIL.pushStream(guide_file_stream, x, ",", y, ",", gcell.get_ll_x(), ",", gcell.get_ll_y(), ",", gcell.get_ur_x(), ",", gcell.get_ur_y(), "\n");
    }
  }
  RTUTIL.closeFileStream(guide_file_stream);

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::outputPlanarSupplyCSV(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<EREdge>& planar_h_edge_map = er_model.get_planar_h_edge_map();
  GridMap<EREdge>& planar_v_edge_map = er_model.get_planar_v_edge_map();
  std::string& er_temp_directory_path = RTDM.getConfig().er_temp_directory_path;

  for (std::pair<std::string, GridMap<EREdge>*> edge_map_pair :
       {std::make_pair("h_supply_map.csv", &planar_h_edge_map), std::make_pair("v_supply_map.csv", &planar_v_edge_map)}) {
    GridMap<EREdge>& edge_map = *edge_map_pair.second;
    std::ofstream* supply_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(er_temp_directory_path, edge_map_pair.first));
    for (int32_t y = edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
        RTUTIL.pushStream(supply_csv_file, edge_map[x][y].get_supply(), ",");
      }
      RTUTIL.pushStream(supply_csv_file, "\n");
    }
    RTUTIL.closeFileStream(supply_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::outputPlanarGuide(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& er_temp_directory_path = RTDM.getConfig().er_temp_directory_path;

  std::vector<ERNet>& er_net_list = er_model.get_er_net_list();

  std::ofstream* guide_file_stream = RTUTIL.getOutputFileStream(RTUTIL.getString(er_temp_directory_path, "route_planar.guide"));
  if (guide_file_stream == nullptr) {
    return;
  }
  RTUTIL.pushStream(guide_file_stream, "guide net_name\n");
  RTUTIL.pushStream(guide_file_stream, "pin grid_x grid_y real_x real_y layer energy name\n");
  RTUTIL.pushStream(guide_file_stream, "wire grid1_x grid1_y grid2_x grid2_y real1_x real1_y real2_x real2_y layer\n");
  RTUTIL.pushStream(guide_file_stream, "via grid_x grid_y real_x real_y layer1 layer2\n");

  for (auto& [net_idx, segment_set] : er_model.get_net_global_result_map()) {
    ERNet& er_net = er_net_list[net_idx];
    RTUTIL.pushStream(guide_file_stream, "guide ", er_net.get_origin_net()->get_net_name(), "\n");

    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      AccessPoint& access_point = er_pin.get_access_point();
      double grid_x = access_point.get_grid_x();
      double grid_y = access_point.get_grid_y();
      double real_x = access_point.get_real_x() / 1.0 / micron_dbu;
      double real_y = access_point.get_real_y() / 1.0 / micron_dbu;
      std::string layer = routing_layer_list[access_point.get_layer_idx()].get_layer_name();
      std::string connnect;
      if (er_pin.get_is_driven()) {
        connnect = "driven";
      } else {
        connnect = "load";
      }
      RTUTIL.pushStream(guide_file_stream, "pin ", grid_x, " ", grid_y, " ", real_x, " ", real_y, " ", layer, " ", connnect, " ", er_pin.get_pin_name(), "\n");
    }

    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      LayerCoord first_layer_coord = segment->get_first();
      double grid1_x = first_layer_coord.get_x();
      double grid1_y = first_layer_coord.get_y();
      int32_t first_layer_idx = first_layer_coord.get_layer_idx();

      PlanarCoord first_mid_coord = RTUTIL.getRealRectByGCell(first_layer_coord, gcell_axis).getMidPoint();
      double real1_x = first_mid_coord.get_x() / 1.0 / micron_dbu;
      double real1_y = first_mid_coord.get_y() / 1.0 / micron_dbu;

      LayerCoord second_layer_coord = segment->get_second();
      double grid2_x = second_layer_coord.get_x();
      double grid2_y = second_layer_coord.get_y();
      int32_t second_layer_idx = second_layer_coord.get_layer_idx();

      PlanarCoord second_mid_coord = RTUTIL.getRealRectByGCell(second_layer_coord, gcell_axis).getMidPoint();
      double real2_x = second_mid_coord.get_x() / 1.0 / micron_dbu;
      double real2_y = second_mid_coord.get_y() / 1.0 / micron_dbu;

      if (first_layer_idx != second_layer_idx) {
        RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
        std::string layer1 = routing_layer_list[first_layer_idx].get_layer_name();
        std::string layer2 = routing_layer_list[second_layer_idx].get_layer_name();
        RTUTIL.pushStream(guide_file_stream, "via ", grid1_x, " ", grid1_y, " ", real1_x, " ", real1_y, " ", layer1, " ", layer2, "\n");
      } else {
        std::string layer = routing_layer_list[first_layer_idx].get_layer_name();
        RTUTIL.pushStream(guide_file_stream, "wire ", grid1_x, " ", grid1_y, " ", grid2_x, " ", grid2_y, " ", real1_x, " ", real1_y, " ", real2_x, " ", real2_y,
                          " ", layer, "\n");
      }
    }
  }
  RTUTIL.closeFileStream(guide_file_stream);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::outputPlanarOverflowCSV(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<EREdge>& planar_h_edge_map = er_model.get_planar_h_edge_map();
  GridMap<EREdge>& planar_v_edge_map = er_model.get_planar_v_edge_map();
  std::string& er_temp_directory_path = RTDM.getConfig().er_temp_directory_path;

  for (std::pair<std::string, GridMap<EREdge>*> edge_map_pair :
       {std::make_pair("h_overflow_map.csv", &planar_h_edge_map), std::make_pair("v_overflow_map.csv", &planar_v_edge_map)}) {
    GridMap<EREdge>& edge_map = *edge_map_pair.second;
    std::ofstream* overflow_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(er_temp_directory_path, edge_map_pair.first));
    for (int32_t y = edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
        RTUTIL.pushStream(overflow_csv_file, edge_map[x][y].get_overflow(), ",");
      }
      RTUTIL.pushStream(overflow_csv_file, "\n");
    }
    RTUTIL.closeFileStream(overflow_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::outputLayerSupplyCSV(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<GridMap<EREdge>>& layer_h_edge_map = er_model.get_layer_h_edge_map();
  std::vector<GridMap<EREdge>>& layer_v_edge_map = er_model.get_layer_v_edge_map();
  std::string& er_temp_directory_path = RTDM.getConfig().er_temp_directory_path;

  for (RoutingLayer& routing_layer : routing_layer_list) {
    int32_t layer_idx = routing_layer.get_layer_idx();
    GridMap<EREdge>& edge_map = routing_layer.isPreferH() ? layer_h_edge_map[layer_idx] : layer_v_edge_map[layer_idx];
    std::ofstream* supply_csv_file
        = RTUTIL.getOutputFileStream(RTUTIL.getString(er_temp_directory_path, "supply_map_", routing_layer.get_layer_name(), ".csv"));
    for (int32_t y = edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
        RTUTIL.pushStream(supply_csv_file, edge_map[x][y].get_supply(), ",");
      }
      RTUTIL.pushStream(supply_csv_file, "\n");
    }
    RTUTIL.closeFileStream(supply_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::outputLayerGuide(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& er_temp_directory_path = RTDM.getConfig().er_temp_directory_path;

  std::vector<ERNet>& er_net_list = er_model.get_er_net_list();

  std::ofstream* guide_file_stream = RTUTIL.getOutputFileStream(RTUTIL.getString(er_temp_directory_path, "route.guide"));
  if (guide_file_stream == nullptr) {
    return;
  }
  RTUTIL.pushStream(guide_file_stream, "guide net_name\n");
  RTUTIL.pushStream(guide_file_stream, "pin grid_x grid_y real_x real_y layer energy name\n");
  RTUTIL.pushStream(guide_file_stream, "wire grid1_x grid1_y grid2_x grid2_y real1_x real1_y real2_x real2_y layer\n");
  RTUTIL.pushStream(guide_file_stream, "via grid_x grid_y real_x real_y layer1 layer2\n");

  for (auto& [net_idx, segment_set] : er_model.get_net_global_result_map()) {
    ERNet& er_net = er_net_list[net_idx];
    RTUTIL.pushStream(guide_file_stream, "guide ", er_net.get_origin_net()->get_net_name(), "\n");

    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      AccessPoint& access_point = er_pin.get_access_point();
      double grid_x = access_point.get_grid_x();
      double grid_y = access_point.get_grid_y();
      double real_x = access_point.get_real_x() / 1.0 / micron_dbu;
      double real_y = access_point.get_real_y() / 1.0 / micron_dbu;
      std::string layer = routing_layer_list[access_point.get_layer_idx()].get_layer_name();
      std::string connnect;
      if (er_pin.get_is_driven()) {
        connnect = "driven";
      } else {
        connnect = "load";
      }
      RTUTIL.pushStream(guide_file_stream, "pin ", grid_x, " ", grid_y, " ", real_x, " ", real_y, " ", layer, " ", connnect, " ", er_pin.get_pin_name(), "\n");
    }

    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      LayerCoord first_layer_coord = segment->get_first();
      double grid1_x = first_layer_coord.get_x();
      double grid1_y = first_layer_coord.get_y();
      int32_t first_layer_idx = first_layer_coord.get_layer_idx();

      PlanarCoord first_mid_coord = RTUTIL.getRealRectByGCell(first_layer_coord, gcell_axis).getMidPoint();
      double real1_x = first_mid_coord.get_x() / 1.0 / micron_dbu;
      double real1_y = first_mid_coord.get_y() / 1.0 / micron_dbu;

      LayerCoord second_layer_coord = segment->get_second();
      double grid2_x = second_layer_coord.get_x();
      double grid2_y = second_layer_coord.get_y();
      int32_t second_layer_idx = second_layer_coord.get_layer_idx();

      PlanarCoord second_mid_coord = RTUTIL.getRealRectByGCell(second_layer_coord, gcell_axis).getMidPoint();
      double real2_x = second_mid_coord.get_x() / 1.0 / micron_dbu;
      double real2_y = second_mid_coord.get_y() / 1.0 / micron_dbu;

      if (first_layer_idx != second_layer_idx) {
        RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
        std::string layer1 = routing_layer_list[first_layer_idx].get_layer_name();
        std::string layer2 = routing_layer_list[second_layer_idx].get_layer_name();
        RTUTIL.pushStream(guide_file_stream, "via ", grid1_x, " ", grid1_y, " ", real1_x, " ", real1_y, " ", layer1, " ", layer2, "\n");
      } else {
        std::string layer = routing_layer_list[first_layer_idx].get_layer_name();
        RTUTIL.pushStream(guide_file_stream, "wire ", grid1_x, " ", grid1_y, " ", grid2_x, " ", grid2_y, " ", real1_x, " ", real1_y, " ", real2_x, " ", real2_y,
                          " ", layer, "\n");
      }
    }
  }
  RTUTIL.closeFileStream(guide_file_stream);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void EarlyRouter::outputLayerOverflowCSV(ERModel& er_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<GridMap<EREdge>>& layer_h_edge_map = er_model.get_layer_h_edge_map();
  std::vector<GridMap<EREdge>>& layer_v_edge_map = er_model.get_layer_v_edge_map();
  std::string& er_temp_directory_path = RTDM.getConfig().er_temp_directory_path;

  for (RoutingLayer& routing_layer : routing_layer_list) {
    int32_t layer_idx = routing_layer.get_layer_idx();
    GridMap<EREdge>& edge_map = routing_layer.isPreferH() ? layer_h_edge_map[layer_idx] : layer_v_edge_map[layer_idx];
    std::ofstream* overflow_csv_file
        = RTUTIL.getOutputFileStream(RTUTIL.getString(er_temp_directory_path, "overflow_map_", routing_layer.get_layer_name(), ".csv"));

    for (int32_t y = edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
        RTUTIL.pushStream(overflow_csv_file, edge_map[x][y].get_overflow(), ",");
      }
      RTUTIL.pushStream(overflow_csv_file, "\n");
    }
    RTUTIL.closeFileStream(overflow_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

#endif

#if 1  // update env

void EarlyRouter::updateEdgeDemand(EREdge& edge, int32_t net_idx, ChangeType change_type, std::unordered_set<EREdge*>& edge_set)
{
  int32_t delta = 0;
  if (change_type == ChangeType::kAdd) {
    delta = 1;
  } else if (change_type == ChangeType::kDel) {
    delta = -1;
  } else {
    RTLOG.error(Loc::current(), "The change type is error!");
  }
  bool is_changed = delta > 0 ? edge_set.insert(&edge).second : edge_set.erase(&edge) > 0;
  if (!is_changed) {
    return;
  }
  if (edge.get_ignore_net_set().contains(net_idx)) {
    return;
  }
  if (delta < 0 && edge.get_demand() <= 0) {
    RTLOG.error(Loc::current(), "The routing edge demand is error!");
  }
  std::vector<int32_t>& demand_net_idx_list = edge.get_demand_net_idx_list();
  if (delta > 0) {
    demand_net_idx_list.push_back(net_idx);
  } else {
    auto iter = std::find(demand_net_idx_list.begin(), demand_net_idx_list.end(), net_idx);
    if (iter == demand_net_idx_list.end()) {
      RTLOG.error(Loc::current(), "The routing edge demand net is error!");
    }
    demand_net_idx_list.erase(iter);
  }
  edge.set_demand(edge.get_demand() + delta);
}

void EarlyRouter::updateDemandToGraph(ERModel& er_model, ChangeType change_type, MTree<PlanarCoord>& coord_tree)
{
  ERNet* curr_er_task = er_model.get_curr_er_task();
  int32_t curr_net_idx = curr_er_task->get_net_idx();
  std::unordered_set<EREdge*>& edge_set = curr_er_task->get_planar_edge_set();

  for (Segment<TNode<PlanarCoord>*>& coord_segment : RTUTIL.getSegListByTree(coord_tree)) {
    PlanarCoord& first_coord = coord_segment.get_first()->value();
    PlanarCoord& second_coord = coord_segment.get_second()->value();
    for (EREdge* edge : getPlanarEdgeList(er_model, first_coord, second_coord)) {
      updateEdgeDemand(*edge, curr_net_idx, change_type, edge_set);
    }
  }
}

void EarlyRouter::updateDemandToGraph(ERModel& er_model, ChangeType change_type, MTree<LayerCoord>& coord_tree)
{
  ERNet* curr_er_task = er_model.get_curr_er_task();
  int32_t curr_net_idx = curr_er_task->get_net_idx();
  std::unordered_set<EREdge*>& edge_set = curr_er_task->get_layer_edge_set();

  for (Segment<TNode<LayerCoord>*>& coord_segment : RTUTIL.getSegListByTree(coord_tree)) {
    LayerCoord& first_coord = coord_segment.get_first()->value();
    LayerCoord& second_coord = coord_segment.get_second()->value();
    if (first_coord.get_layer_idx() != second_coord.get_layer_idx()) {
      if (first_coord.get_planar_coord() != second_coord.get_planar_coord()) {
        RTLOG.error(Loc::current(), "The via segment changes planar coordinates!");
      }
      continue;
    }
    for (EREdge* edge : getLayerEdgeList(er_model, first_coord.get_layer_idx(), first_coord.get_planar_coord(), second_coord.get_planar_coord())) {
      updateEdgeDemand(*edge, curr_net_idx, change_type, edge_set);
    }
  }
}

#endif

#if 1  // exhibit

void EarlyRouter::printAccessSummary(ERModel& er_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  std::map<int32_t, int32_t> routing_patch_num_map;
  int32_t total_patch_num = 0;

  for (auto& [net_idx, patch_list] : er_model.get_net_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      routing_patch_num_map[patch.get_layer_idx()]++;
      total_patch_num++;
    }
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
  RTUTIL.printTableList({routing_patch_num_map_table});
}

void EarlyRouter::printSupplySummary(ERModel& er_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<GridMap<EREdge>>& layer_h_edge_map = er_model.get_layer_h_edge_map();
  std::vector<GridMap<EREdge>>& layer_v_edge_map = er_model.get_layer_v_edge_map();

  std::map<int32_t, int32_t> routing_supply_map;
  int32_t total_supply = 0;

  for (RoutingLayer& routing_layer : routing_layer_list) {
    int32_t layer_idx = routing_layer.get_layer_idx();
    for (GridMap<EREdge>* edge_map : {&layer_h_edge_map[layer_idx], &layer_v_edge_map[layer_idx]}) {
      for (int32_t x = 0; x < edge_map->get_x_size(); x++) {
        for (int32_t y = 0; y < edge_map->get_y_size(); y++) {
          int32_t supply = (*edge_map)[x][y].get_supply();
          routing_supply_map[layer_idx] += supply;
          total_supply += supply;
        }
      }
    }
  }

  fort::char_table routing_supply_map_table;
  {
    routing_supply_map_table.set_cell_text_align(fort::text_align::right);
    routing_supply_map_table << fort::header << "routing"
                             << "supply"
                             << "prop" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_supply_map_table << routing_layer.get_layer_name() << routing_supply_map[routing_layer.get_layer_idx()]
                               << RTUTIL.getPercentage(routing_supply_map[routing_layer.get_layer_idx()], total_supply) << fort::endr;
    }
    routing_supply_map_table << fort::header << "Total" << total_supply << RTUTIL.getPercentage(total_supply, total_supply) << fort::endr;
  }
  RTUTIL.printTableList({routing_supply_map_table});
}

void EarlyRouter::printPlanarSummary(ERModel& er_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();

  double total_demand = 0;
  double total_overflow = 0;
  double total_wire_length = 0;

  for (GridMap<EREdge>* edge_map : {&er_model.get_planar_h_edge_map(), &er_model.get_planar_v_edge_map()}) {
    for (int32_t x = 0; x < edge_map->get_x_size(); x++) {
      for (int32_t y = 0; y < edge_map->get_y_size(); y++) {
        total_demand += (*edge_map)[x][y].get_demand();
        total_overflow += (*edge_map)[x][y].get_overflow();
      }
    }
  }
  for (auto& [net_idx, segment_set] : er_model.get_net_global_result_map()) {
    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      LayerCoord& first_coord = segment->get_first();
      int32_t first_layer_idx = first_coord.get_layer_idx();
      LayerCoord& second_coord = segment->get_second();
      int32_t second_layer_idx = second_coord.get_layer_idx();

      if (first_layer_idx == second_layer_idx) {
        PlanarRect& first_gcell = gcell_map[first_coord.get_x()][first_coord.get_y()];
        PlanarRect& second_gcell = gcell_map[second_coord.get_x()][second_coord.get_y()];
        double wire_length = RTUTIL.getManhattanDistance(first_gcell.getMidPoint(), second_gcell.getMidPoint()) / 1.0 / micron_dbu;
        total_wire_length += wire_length;
      } else {
        RTLOG.error(Loc::current(), "first_layer_idx != second_layer_idx!");
      }
    }
  }

  fort::char_table summary_table;
  {
    summary_table.set_cell_text_align(fort::text_align::right);
    summary_table << fort::header << "total_demand" << total_demand << fort::endr;
    summary_table << fort::header << "total_overflow" << total_overflow << fort::endr;
    summary_table << fort::header << "total_wire_length" << total_wire_length << fort::endr;
  }
  RTUTIL.printTableList({summary_table});
  if (RTDM.getConfig().output_inter_result) {
    std::ofstream* summary_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(RTDM.getConfig().er_temp_directory_path, "route_summary.csv"));
    RTUTIL.pushStream(summary_csv_file, "metric,value\n");
    RTUTIL.pushStream(summary_csv_file, "total_overflow,", total_overflow, "\n");
    RTUTIL.pushStream(summary_csv_file, "total_wire_length,", total_wire_length, "\n");
    RTUTIL.pushStream(summary_csv_file, "total_via_num,0\n");
    RTUTIL.closeFileStream(summary_csv_file);
  }
}

void EarlyRouter::printLayerSummary(ERModel& er_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<CutLayer>& cut_layer_list = RTDM.getDatabase().get_cut_layer_list();
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();

  std::map<int32_t, double> routing_demand_map;
  double total_demand = 0;
  std::map<int32_t, double> routing_overflow_map;
  double total_overflow = 0;
  std::map<int32_t, double> routing_wire_length_map;
  double total_wire_length = 0;
  std::map<int32_t, int32_t> cut_via_num_map;
  int32_t total_via_num = 0;

  std::vector<GridMap<EREdge>>& layer_h_edge_map = er_model.get_layer_h_edge_map();
  std::vector<GridMap<EREdge>>& layer_v_edge_map = er_model.get_layer_v_edge_map();

  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(layer_h_edge_map.size()); layer_idx++) {
    for (GridMap<EREdge>* edge_map : {&layer_h_edge_map[layer_idx], &layer_v_edge_map[layer_idx]}) {
      for (int32_t x = 0; x < edge_map->get_x_size(); x++) {
        for (int32_t y = 0; y < edge_map->get_y_size(); y++) {
          int32_t demand = (*edge_map)[x][y].get_demand();
          int32_t overflow = (*edge_map)[x][y].get_overflow();
          routing_demand_map[layer_idx] += demand;
          total_demand += demand;
          routing_overflow_map[layer_idx] += overflow;
          total_overflow += overflow;
        }
      }
    }
  }
  for (auto& [net_idx, segment_set] : er_model.get_net_global_result_map()) {
    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      LayerCoord& first_coord = segment->get_first();
      int32_t first_layer_idx = first_coord.get_layer_idx();
      LayerCoord& second_coord = segment->get_second();
      int32_t second_layer_idx = second_coord.get_layer_idx();

      if (first_layer_idx == second_layer_idx) {
        PlanarRect& first_gcell = gcell_map[first_coord.get_x()][first_coord.get_y()];
        PlanarRect& second_gcell = gcell_map[second_coord.get_x()][second_coord.get_y()];
        double wire_length = RTUTIL.getManhattanDistance(first_gcell.getMidPoint(), second_gcell.getMidPoint()) / 1.0 / micron_dbu;
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

  fort::char_table routing_demand_map_table;
  {
    routing_demand_map_table.set_cell_text_align(fort::text_align::right);
    routing_demand_map_table << fort::header << "routing"
                             << "demand"
                             << "prop" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_demand_map_table << routing_layer.get_layer_name() << routing_demand_map[routing_layer.get_layer_idx()]
                               << RTUTIL.getPercentage(routing_demand_map[routing_layer.get_layer_idx()], total_demand) << fort::endr;
    }
    routing_demand_map_table << fort::header << "Total" << total_demand << RTUTIL.getPercentage(total_demand, total_demand) << fort::endr;
  }
  fort::char_table routing_overflow_map_table;
  {
    routing_overflow_map_table.set_cell_text_align(fort::text_align::right);
    routing_overflow_map_table << fort::header << "routing"
                               << "overflow"
                               << "prop" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_overflow_map_table << routing_layer.get_layer_name() << routing_overflow_map[routing_layer.get_layer_idx()]
                                 << RTUTIL.getPercentage(routing_overflow_map[routing_layer.get_layer_idx()], total_overflow) << fort::endr;
    }
    routing_overflow_map_table << fort::header << "Total" << total_overflow << RTUTIL.getPercentage(total_overflow, total_overflow) << fort::endr;
  }
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
  RTUTIL.printTableList({routing_demand_map_table, routing_overflow_map_table, routing_wire_length_map_table, cut_via_num_map_table});
  if (RTDM.getConfig().output_inter_result) {
    std::ofstream* summary_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(RTDM.getConfig().er_temp_directory_path, "route_summary.csv"));
    RTUTIL.pushStream(summary_csv_file, "metric,value\n");
    RTUTIL.pushStream(summary_csv_file, "total_overflow,", total_overflow, "\n");
    RTUTIL.pushStream(summary_csv_file, "total_wire_length,", total_wire_length, "\n");
    RTUTIL.pushStream(summary_csv_file, "total_via_num,", total_via_num, "\n");
    RTUTIL.closeFileStream(summary_csv_file);
  }
}

void EarlyRouter::printTrackSummary(ERModel& er_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  std::map<int32_t, double> routing_wire_length_map;
  double total_wire_length = 0;

  for (auto& [net_idx, segment_list] : er_model.get_net_detailed_result_map()) {
    for (Segment<LayerCoord>& segment : segment_list) {
      LayerCoord& first_coord = segment.get_first();
      LayerCoord& second_coord = segment.get_second();
      if (first_coord.get_layer_idx() == second_coord.get_layer_idx()) {
        double wire_length = RTUTIL.getManhattanDistance(first_coord, second_coord) / 1.0 / micron_dbu;
        routing_wire_length_map[first_coord.get_layer_idx()] += wire_length;
        total_wire_length += wire_length;
      }
    }
  }

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
  RTUTIL.printTableList({routing_wire_length_map_table});
}

void EarlyRouter::printDetailedSummary(ERModel& er_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<CutLayer>& cut_layer_list = RTDM.getDatabase().get_cut_layer_list();
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();

  std::map<int32_t, double> routing_wire_length_map;
  double total_wire_length = 0;
  std::map<int32_t, int32_t> cut_via_num_map;
  int32_t total_via_num = 0;
  std::map<int32_t, int32_t> routing_patch_num_map;
  int32_t total_patch_num = 0;

  for (auto& [net_idx, segment_list] : er_model.get_net_detailed_result_map()) {
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
  for (auto& [net_idx, patch_list] : er_model.get_net_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      routing_patch_num_map[patch.get_layer_idx()]++;
      total_patch_num++;
    }
  }

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
  RTUTIL.printTableList({routing_wire_length_map_table, cut_via_num_map_table, routing_patch_num_map_table});
}

#endif

#if 1  // debug

void EarlyRouter::debugPlotERModel(ERModel& er_model, std::string flag)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& er_temp_directory_path = RTDM.getConfig().er_temp_directory_path;

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
  for (ERNet& er_net : er_model.get_er_net_list()) {
    GPStruct access_point_struct(RTUTIL.getString("access_point(net_", er_net.get_net_idx(), ")"));
    for (ERPin& er_pin : er_net.get_er_pin_list()) {
      AccessPoint& access_point = er_pin.get_access_point();
      int32_t x = access_point.get_real_x();
      int32_t y = access_point.get_real_y();

      GPBoundary access_point_boundary;
      access_point_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(access_point.get_layer_idx()));
      access_point_boundary.set_data_type(static_cast<int32_t>(GPDataType::kAccessPoint));
      access_point_boundary.set_rect(x - point_size, y - point_size, x + point_size, y + point_size);
      access_point_struct.push(access_point_boundary);
    }
    gp_gds.addStruct(access_point_struct);
  }

  // routing result
  for (auto& [net_idx, segment_set] : er_model.get_net_global_result_map()) {
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
  for (auto& [net_idx, segment_list] : er_model.get_net_detailed_result_map()) {
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
  for (auto& [net_idx, patch_list] : er_model.get_net_detailed_patch_map()) {
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

  std::string gds_file_path = RTUTIL.getString(er_temp_directory_path, flag, "_er_model.gds");
  RTGP.plot(gp_gds, gds_file_path);
}

#endif

}  // namespace irt
