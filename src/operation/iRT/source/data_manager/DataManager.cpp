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
#include "DataManager.hpp"

#include "Monitor.hpp"
#include "RTHeader.hpp"
#include "RTInterface.hpp"
#include "Utility.hpp"

namespace irt {

// public

void DataManager::initInst()
{
  if (_dm_instance == nullptr) {
    _dm_instance = new DataManager();
  }
}

DataManager& DataManager::getInst()
{
  if (_dm_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_dm_instance;
}

void DataManager::destroyInst()
{
  if (_dm_instance != nullptr) {
    delete _dm_instance;
    _dm_instance = nullptr;
  }
}

// function

void DataManager::input(std::map<std::string, std::any>& config_map)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  RTI.input(config_map);
  buildConfig();
  buildDatabase();
  printConfig();
  printDatabase();
  outputScript();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  RTI.output();
  destroyDatabaseResult();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

#if 1  // 更新Database

void DataManager::rebuildAccessPointRTree()
{
  std::vector<Database::AccessPointRTree::value_type> value_list;
  for (Net& net : _database.get_net_list()) {
    for (Pin& pin : net.get_pin_list()) {
      AccessPoint& access_point = pin.get_access_point();
      if (access_point.get_layer_idx() >= 0) {
        value_list.emplace_back(RTUTIL.convertToBGRectInt(PlanarRect(access_point.get_real_coord(), access_point.get_real_coord())),
                                std::make_pair(net.get_net_idx(), &access_point));
      }
    }
  }
  _database.get_access_point_rtree() = Database::AccessPointRTree(value_list);
}

void DataManager::rebuildGlobalResultRTree()
{
  std::vector<Database::GlobalResultRTree::value_type> value_list;
  for (auto& [net_idx, segment_list] : _database.get_net_global_result_map()) {
    for (int32_t segment_idx = 0; segment_idx < static_cast<int32_t>(segment_list.size()); segment_idx++) {
      Segment<LayerCoord>& segment = segment_list[segment_idx];
      PlanarRect segment_rect(
          std::min(segment.get_first().get_x(), segment.get_second().get_x()), std::min(segment.get_first().get_y(), segment.get_second().get_y()),
          std::max(segment.get_first().get_x(), segment.get_second().get_x()), std::max(segment.get_first().get_y(), segment.get_second().get_y()));
      value_list.emplace_back(RTUTIL.convertToBGRectInt(segment_rect), std::make_pair(net_idx, segment_idx));
    }
  }
  _database.get_global_result_rtree() = Database::GlobalResultRTree(value_list);
}

void DataManager::updateViolationToRTree(ChangeType change_type, const Violation& violation)
{
  Database::ViolationRTree& violation_rtree = _database.get_violation_rtree();
  Database::ViolationRTree::value_type violation_value(RTUTIL.convertToBGRectInt(violation.get_violation_shape().get_real_rect()), violation);
  if (change_type == ChangeType::kAdd) {
    std::vector<Database::ViolationRTree::value_type> value_list;
    violation_rtree.query(bgi::intersects(violation_value.first), std::back_inserter(value_list));
    for (auto& [rect, stored_violation] : value_list) {
      if (stored_violation == violation) {
        return;
      }
    }
    violation_rtree.insert(violation_value);
  } else if (change_type == ChangeType::kDel) {
    violation_rtree.remove(violation_value);
  }
}

std::map<bool, std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>> DataManager::getTypeLayerNetFixedRectMap(EXTPlanarRect& region)
{
  std::map<bool, std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>> type_layer_net_fixed_rect_map;
  BGRectInt query_rect = RTUTIL.convertToBGRectInt(RTUTIL.getEnlargedRect(region.get_real_rect(), _database.get_detection_distance()));
  auto& type_layer_fixed_rect_rtree_map = _database.get_type_layer_fixed_rect_rtree_map();
  for (bool is_routing : {false, true}) {
    for (auto& [layer_idx, fixed_rect_rtree] : type_layer_fixed_rect_rtree_map[is_routing]) {
      std::vector<Database::FixedRectRTree::value_type> value_list;
      fixed_rect_rtree.query(bgi::intersects(query_rect), std::back_inserter(value_list));
      for (auto& [rect, net_fixed_rect] : value_list) {
        type_layer_net_fixed_rect_map[is_routing][layer_idx][net_fixed_rect.first].insert(net_fixed_rect.second);
      }
    }
  }
  return type_layer_net_fixed_rect_map;
}

std::map<int32_t, std::set<EXTLayerRect*>> DataManager::getNetFixedRectMap(bool is_routing, EXTLayerRect& region)
{
  std::map<int32_t, std::set<EXTLayerRect*>> net_fixed_rect_map;
  auto& layer_fixed_rect_rtree_map = _database.get_type_layer_fixed_rect_rtree_map()[is_routing];
  auto rtree_iter = layer_fixed_rect_rtree_map.find(region.get_layer_idx());
  if (rtree_iter == layer_fixed_rect_rtree_map.end()) {
    return net_fixed_rect_map;
  }
  BGRectInt query_rect = RTUTIL.convertToBGRectInt(RTUTIL.getEnlargedRect(region.get_real_rect(), _database.get_detection_distance()));
  std::vector<Database::FixedRectRTree::value_type> value_list;
  rtree_iter->second.query(bgi::intersects(query_rect), std::back_inserter(value_list));
  for (auto& [rect, net_fixed_rect] : value_list) {
    net_fixed_rect_map[net_fixed_rect.first].insert(net_fixed_rect.second);
  }
  return net_fixed_rect_map;
}

std::vector<Violation> DataManager::getViolationList(EXTPlanarRect& region)
{
  std::vector<Violation> violation_list;
  if (region == _database.get_die()) {
    violation_list.reserve(_database.get_violation_rtree().size());
    for (const auto& [rect, violation] : _database.get_violation_rtree()) {
      violation_list.push_back(violation);
    }
    std::sort(violation_list.begin(), violation_list.end(), CmpViolation());
    return violation_list;
  }
  BGRectInt query_rect = RTUTIL.convertToBGRectInt(region.get_real_rect());
  std::vector<Database::ViolationRTree::value_type> value_list;
  _database.get_violation_rtree().query(bgi::intersects(query_rect), std::back_inserter(value_list));
  for (auto& [rect, violation] : value_list) {
    violation_list.push_back(std::move(violation));
  }
  std::sort(violation_list.begin(), violation_list.end(), CmpViolation());
  return violation_list;
}

std::vector<Violation> DataManager::getViolationList(EXTLayerRect& region)
{
  std::vector<Violation> violation_list;
  BGRectInt query_rect = RTUTIL.convertToBGRectInt(region.get_real_rect());
  std::vector<Database::ViolationRTree::value_type> value_list;
  _database.get_violation_rtree().query(bgi::intersects(query_rect), std::back_inserter(value_list));
  for (auto& [rect, violation] : value_list) {
    if (violation.get_violation_shape().get_layer_idx() == region.get_layer_idx()) {
      violation_list.push_back(std::move(violation));
    }
  }
  std::sort(violation_list.begin(), violation_list.end(), CmpViolation());
  return violation_list;
}

std::map<int32_t, std::set<AccessPoint*, CmpAccessPoint>> DataManager::getNetAccessPointMap(EXTPlanarRect& region)
{
  std::map<int32_t, std::set<AccessPoint*, CmpAccessPoint>> net_access_point_map;
  PlanarRect query_rect = RTUTIL.getEnlargedRect(region.get_real_rect(), _database.get_detection_distance());
  std::vector<Database::AccessPointRTree::value_type> value_list;
  _database.get_access_point_rtree().query(bgi::intersects(RTUTIL.convertToBGRectInt(query_rect)), std::back_inserter(value_list));
  for (auto& [rect, net_access_point] : value_list) {
    net_access_point_map[net_access_point.first].insert(net_access_point.second);
  }
  return net_access_point_map;
}

std::map<int32_t, std::vector<Segment<LayerCoord>>> DataManager::getNetGlobalResultMap(EXTPlanarRect& region)
{
  if (region == _database.get_die()) {
    return _database.get_net_global_result_map();
  }
  std::map<int32_t, std::vector<Segment<LayerCoord>>> net_global_result_map;
  std::vector<Database::GlobalResultRTree::value_type> value_list;
  _database.get_global_result_rtree().query(bgi::intersects(RTUTIL.convertToBGRectInt(region.get_grid_rect())), std::back_inserter(value_list));
  for (auto& [rect, net_segment_idx] : value_list) {
    net_global_result_map[net_segment_idx.first].push_back(_database.get_net_global_result_map().at(net_segment_idx.first).at(net_segment_idx.second));
  }
  return net_global_result_map;
}

std::map<int32_t, std::set<Segment<LayerCoord>*>> DataManager::getNetDetailedResultMap(EXTPlanarRect& region)
{
  std::map<int32_t, std::set<Segment<LayerCoord>*>> net_detailed_result_map;
  Die& die = _database.get_die();
  if (region == die) {
    for (auto& [net_idx, segment_list] : _database.get_net_detailed_result_map()) {
      for (Segment<LayerCoord>& segment : segment_list) {
        net_detailed_result_map[net_idx].insert(&segment);
      }
    }
    return net_detailed_result_map;
  }
  int32_t detection_distance = _database.get_detection_distance();
  for (auto& [net_idx, segment_list] : _database.get_net_detailed_result_map()) {
    for (Segment<LayerCoord>& segment : segment_list) {
      for (NetShape& net_shape : getNetDetailedShapeList(net_idx, segment)) {
        PlanarRect real_rect = RTUTIL.getEnlargedRect(net_shape, detection_distance);
        if (!RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
          continue;
        }
        PlanarRect grid_rect = RTUTIL.getClosedGCellGridRect(RTUTIL.getRegularRect(real_rect, die.get_real_rect()), _database.get_gcell_axis());
        if (RTUTIL.isClosedOverlap(grid_rect, region.get_grid_rect())) {
          net_detailed_result_map[net_idx].insert(&segment);
          break;
        }
      }
    }
  }
  return net_detailed_result_map;
}

std::map<int32_t, std::set<EXTLayerRect*>> DataManager::getNetDetailedPatchMap(EXTPlanarRect& region)
{
  std::map<int32_t, std::set<EXTLayerRect*>> net_detailed_patch_map;
  Die& die = _database.get_die();
  if (region == die) {
    for (auto& [net_idx, patch_list] : _database.get_net_detailed_patch_map()) {
      for (EXTLayerRect& patch : patch_list) {
        net_detailed_patch_map[net_idx].insert(&patch);
      }
    }
    return net_detailed_patch_map;
  }
  int32_t detection_distance = _database.get_detection_distance();
  for (auto& [net_idx, patch_list] : _database.get_net_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      PlanarRect real_rect = RTUTIL.getEnlargedRect(patch.get_real_rect(), detection_distance);
      if (!RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
        continue;
      }
      PlanarRect grid_rect = RTUTIL.getClosedGCellGridRect(RTUTIL.getRegularRect(real_rect, die.get_real_rect()), _database.get_gcell_axis());
      if (RTUTIL.isClosedOverlap(grid_rect, region.get_grid_rect())) {
        net_detailed_patch_map[net_idx].insert(&patch);
      }
    }
  }
  return net_detailed_patch_map;
}

#endif

#if 1  // 获得NetShapeList

std::vector<NetShape> DataManager::getNetGlobalShapeList(int32_t net_idx, Segment<LayerCoord>& segment)
{
  std::vector<NetShape> net_shape_list;
  for (NetShape& net_shape : getNetGlobalShapeList(net_idx, segment.get_first(), segment.get_second())) {
    net_shape_list.push_back(net_shape);
  }
  return net_shape_list;
}

std::vector<NetShape> DataManager::getNetGlobalShapeList(int32_t net_idx, LayerCoord& first_coord, LayerCoord& second_coord)
{
  ScaleAxis& gcell_axis = _database.get_gcell_axis();
  std::map<int32_t, std::vector<int32_t>>& routing_to_adjacent_cut_map = _database.get_routing_to_adjacent_cut_map();

  PlanarRect first_gcell = RTUTIL.getRealRectByGCell(first_coord, gcell_axis);
  int32_t first_layer_idx = first_coord.get_layer_idx();
  PlanarRect second_gcell = RTUTIL.getRealRectByGCell(second_coord, gcell_axis);
  int32_t second_layer_idx = second_coord.get_layer_idx();

  std::vector<NetShape> net_shape_list;
  if (first_layer_idx != second_layer_idx) {
    RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
    for (int32_t routing_layer_idx = first_layer_idx; routing_layer_idx < second_layer_idx; routing_layer_idx++) {
      int32_t cut_layer_idx = -1;
      {
        std::vector<int32_t>& cut_layer_idx_list = routing_to_adjacent_cut_map[routing_layer_idx];
        cut_layer_idx = *std::max_element(cut_layer_idx_list.begin(), cut_layer_idx_list.end());
      }
      LayerRect gcell_rect(first_gcell, cut_layer_idx);
      net_shape_list.emplace_back(net_idx, gcell_rect, false);
    }
  } else {
    LayerRect gcell_rect(RTUTIL.getBoundingBox({first_gcell, second_gcell}), first_layer_idx);
    net_shape_list.emplace_back(net_idx, gcell_rect, true);
  }
  return net_shape_list;
}

std::vector<NetShape> DataManager::getNetDetailedShapeList(int32_t net_idx, Segment<LayerCoord>& segment)
{
  std::vector<NetShape> net_shape_list;
  LayerCoord& first_coord = segment.get_first();
  LayerCoord& second_coord = segment.get_second();
  if (segment.hasValidViaMaster() && first_coord.get_layer_idx() != second_coord.get_layer_idx()
      && first_coord.get_planar_coord() == second_coord.get_planar_coord() && std::abs(first_coord.get_layer_idx() - second_coord.get_layer_idx()) == 1) {
    std::vector<std::vector<ViaMaster>>& layer_via_master_list = _database.get_layer_via_master_list();
    ViaMasterIdx& via_master_idx = segment.get_via_master_idx();
    int32_t below_layer_idx = std::min(first_coord.get_layer_idx(), second_coord.get_layer_idx());
    int32_t via_idx = via_master_idx.get_via_idx();
    if (via_master_idx.get_below_layer_idx() == below_layer_idx && below_layer_idx >= 0 && below_layer_idx < static_cast<int32_t>(layer_via_master_list.size())
        && via_idx >= 0 && via_idx < static_cast<int32_t>(layer_via_master_list[below_layer_idx].size())) {
      ViaMaster& via_master = layer_via_master_list[below_layer_idx][via_idx];

      LayerRect& above_enclosure = via_master.get_above_enclosure();
      LayerRect offset_above_enclosure(RTUTIL.getOffsetRect(above_enclosure, first_coord), above_enclosure.get_layer_idx());
      net_shape_list.emplace_back(net_idx, offset_above_enclosure, true);

      LayerRect& below_enclosure = via_master.get_below_enclosure();
      LayerRect offset_below_enclosure(RTUTIL.getOffsetRect(below_enclosure, first_coord), below_enclosure.get_layer_idx());
      net_shape_list.emplace_back(net_idx, offset_below_enclosure, true);

      for (PlanarRect& cut_shape : via_master.get_cut_shape_list()) {
        LayerRect offset_cut_shape(RTUTIL.getOffsetRect(cut_shape, first_coord), via_master.get_cut_layer_idx());
        net_shape_list.emplace_back(net_idx, offset_cut_shape, false);
      }
      return net_shape_list;
    }
  }
  for (NetShape& net_shape : getNetDetailedShapeList(net_idx, segment.get_first(), segment.get_second())) {
    net_shape_list.push_back(net_shape);
  }
  return net_shape_list;
}

std::vector<NetShape> DataManager::getNetDetailedShapeList(int32_t net_idx, LayerCoord& first_coord, LayerCoord& second_coord)
{
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = _database.get_layer_via_master_list();

  std::vector<NetShape> net_shape_list;
  int32_t first_layer_idx = first_coord.get_layer_idx();
  int32_t second_layer_idx = second_coord.get_layer_idx();
  if (first_layer_idx != second_layer_idx) {
    RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
    for (int32_t layer_idx = first_layer_idx; layer_idx < second_layer_idx; layer_idx++) {
      ViaMaster& via_master = layer_via_master_list[layer_idx].front();

      LayerRect& above_enclosure = via_master.get_above_enclosure();
      LayerRect offset_above_enclosure(RTUTIL.getOffsetRect(above_enclosure, first_coord), above_enclosure.get_layer_idx());
      net_shape_list.emplace_back(net_idx, offset_above_enclosure, true);

      LayerRect& below_enclosure = via_master.get_below_enclosure();
      LayerRect offset_below_enclosure(RTUTIL.getOffsetRect(below_enclosure, first_coord), below_enclosure.get_layer_idx());
      net_shape_list.emplace_back(net_idx, offset_below_enclosure, true);

      for (PlanarRect& cut_shape : via_master.get_cut_shape_list()) {
        LayerRect offset_cut_shape(RTUTIL.getOffsetRect(cut_shape, first_coord), via_master.get_cut_layer_idx());
        net_shape_list.emplace_back(net_idx, offset_cut_shape, false);
      }
    }
  } else {
    int32_t half_width = routing_layer_list[first_layer_idx].get_min_width() / 2;
    LayerRect wire_rect(RTUTIL.getEnlargedRect(first_coord, second_coord, half_width), first_layer_idx);
    net_shape_list.emplace_back(net_idx, wire_rect, true);
  }
  return net_shape_list;
}

#endif

#if 1  // 获得唯一的track

int32_t DataManager::getOnlyOffset()
{
  int32_t x_offset = getOnlyOffset(Direction::kVertical);
  int32_t y_offset = getOnlyOffset(Direction::kHorizontal);
  (void) y_offset;
  return x_offset;
}

int32_t DataManager::getOnlyOffset(Direction direction)
{
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  std::vector<int32_t> offset_list;
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::vector<ScaleGrid>& grid_list = (direction == Direction::kVertical ? routing_layer.getXTrackGridList() : routing_layer.getYTrackGridList());
    for (ScaleGrid& grid : grid_list) {
      offset_list.push_back(grid.get_start_line());
    }
  }
  for (int32_t offset : offset_list) {
    if (offset_list.front() != offset) {
      RTLOG.error(Loc::current(), "The offset is not equal!");
    }
  }
  return offset_list.front();
}

int32_t DataManager::getOnlyPitch()
{
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();

  std::vector<int32_t> pitch_list;
  for (RoutingLayer& routing_layer : routing_layer_list) {
    for (ScaleGrid& x_grid : routing_layer.get_track_axis().get_x_grid_list()) {
      pitch_list.push_back(x_grid.get_step_length());
    }
    for (ScaleGrid& y_grid : routing_layer.get_track_axis().get_y_grid_list()) {
      pitch_list.push_back(y_grid.get_step_length());
    }
  }
  for (int32_t pitch : pitch_list) {
    if (pitch_list.front() != pitch) {
      RTLOG.error(Loc::current(), "The pitch is not equal!");
    }
  }
  return pitch_list.front();
}

#endif

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildConfig()
{
  /////////////////////////////////////////////
  // **********        RT         ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "rt.log";
  if (_config.bottom_routing_layer.empty()) {
    _config.bottom_routing_layer = _database.get_routing_layer_list().front().get_layer_name();
  }
  if (_config.top_routing_layer.empty()) {
    _config.top_routing_layer = _database.get_routing_layer_list().back().get_layer_name();
  }
  _config.bottom_routing_layer_idx = _database.get_routing_layer_name_to_idx_map()[_config.bottom_routing_layer];
  _config.top_routing_layer_idx = _database.get_routing_layer_name_to_idx_map()[_config.top_routing_layer];
  if (_config.bottom_routing_layer_idx >= _config.top_routing_layer_idx) {
    RTLOG.error(Loc::current(), "The routing layer should be at least two layers!");
  }
  // **********    DataManager    ********** //
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  // **********     DRCEngine     ********** //
  _config.de_temp_directory_path = _config.temp_directory_path + "drc_engine/";
  // **********     GDSPlotter    ********** //
  _config.gp_temp_directory_path = _config.temp_directory_path + "gds_plotter/";
  // **********    PinAccessor    ********** //
  _config.pa_temp_directory_path = _config.temp_directory_path + "pin_accessor/";
  // ********     SupplyAnalyzer    ******** //
  _config.sa_temp_directory_path = _config.temp_directory_path + "supply_analyzer/";
  // ********     PlanarRouter     ******** //
  _config.pr_temp_directory_path = _config.temp_directory_path + "planar_router/";
  // **********   LayerAssigner   ********** //
  _config.la_temp_directory_path = _config.temp_directory_path + "layer_assigner/";
  // **********    SpaceRouter    ********** //
  _config.sr_temp_directory_path = _config.temp_directory_path + "space_router/";
  // **********   TrackAssigner   ********** //
  _config.ta_temp_directory_path = _config.temp_directory_path + "track_assigner/";
  // **********  DetailedRouter   ********** //
  _config.dr_temp_directory_path = _config.temp_directory_path + "detailed_router/";
  // ********** ViolationReporter ********** //
  _config.vr_temp_directory_path = _config.temp_directory_path + "violation_reporter/";
  // **********    EarlyRouter    ********** //
  _config.er_temp_directory_path = _config.temp_directory_path + "early_router/";
  /////////////////////////////////////////////
  // **********        RT         ********** //
  RTUTIL.removeDir(_config.temp_directory_path);
  RTUTIL.createDir(_config.temp_directory_path);
  RTUTIL.createDirByFile(_config.log_file_path);
  // **********    DataManager    ********** //
  RTUTIL.createDir(_config.dm_temp_directory_path);
  // **********     DRCEngine     ********** //
  RTUTIL.createDir(_config.de_temp_directory_path);
  // **********    GDSPlotter     ********** //
  RTUTIL.createDir(_config.gp_temp_directory_path);
  // **********    PinAccessor    ********** //
  RTUTIL.createDir(_config.pa_temp_directory_path);
  // **********  SupplyAnalyzer   ********** //
  RTUTIL.createDir(_config.sa_temp_directory_path);
  // **********    PlanarRouter    ********** //
  RTUTIL.createDir(_config.pr_temp_directory_path);
  // **********   LayerAssigner   ********** //
  RTUTIL.createDir(_config.la_temp_directory_path);
  // **********    SpaceRouter    ********** //
  RTUTIL.createDir(_config.sr_temp_directory_path);
  // **********   TrackAssigner   ********** //
  RTUTIL.createDir(_config.ta_temp_directory_path);
  // **********  DetailedRouter   ********** //
  RTUTIL.createDir(_config.dr_temp_directory_path);
  // ********** ViolationReporter ********** //
  RTUTIL.createDir(_config.vr_temp_directory_path);
  // **********    EarlyRouter    ********** //
  RTUTIL.createDir(_config.er_temp_directory_path);
  /////////////////////////////////////////////
  RTLOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  buildLayerList();
  buildLayerInfo();
  buildGCellAxis();
  buildDie();
  buildMacroList();
  buildLayerViaMasterList();
  buildLayerViaMasterInfo();
  buildObstacleList();
  buildNetList();
  buildDetectionDistance();
  buildGCellMap();
  buildFixRectMap();
}

void DataManager::buildLayerList()
{
  transLayerList();
  makeLayerList();
  checkLayerList();
}

void DataManager::transLayerList()
{
  std::map<int32_t, int32_t>& routing_idb_layer_id_to_idx_map = _database.get_routing_idb_layer_id_to_idx_map();
  std::map<int32_t, int32_t>& cut_idb_layer_id_to_idx_map = _database.get_cut_idb_layer_id_to_idx_map();

  for (RoutingLayer& routing_layer : _database.get_routing_layer_list()) {
    routing_layer.set_layer_idx(routing_idb_layer_id_to_idx_map[routing_layer.get_layer_idx()]);
  }
  for (CutLayer& cut_layer_list : _database.get_cut_layer_list()) {
    cut_layer_list.set_layer_idx(cut_idb_layer_id_to_idx_map[cut_layer_list.get_layer_idx()]);
  }
}

void DataManager::makeLayerList()
{
  makeRoutingLayerList();
  makeCutLayerList();
}

void DataManager::makeRoutingLayerList()
{
  Die& die = _database.get_die();
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();

  auto getFrequentNum = [](const std::vector<int32_t>& num_list) {
    if (num_list.empty()) {
      RTLOG.error(Loc::current(), "The num_list is empty!");
    }
    std::map<int32_t, int32_t> num_count_map;
    for (int32_t num : num_list) {
      num_count_map[num]++;
    }
    std::map<int32_t, std::vector<int32_t>, std::greater<int32_t>> count_num_list_map;
    for (auto& [num, count] : num_count_map) {
      count_num_list_map[count].push_back(num);
    }
    int32_t frequent_num = INT32_MAX;
    for (int32_t num : count_num_list_map.begin()->second) {
      frequent_num = std::min(frequent_num, num);
    }
    return frequent_num;
  };
  int32_t start_line;
  {
    std::vector<int32_t> start_line_list;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      start_line_list.push_back(routing_layer.getPreferTrackGridList().front().get_start_line());
    }
    start_line = getFrequentNum(start_line_list);
  }
  int32_t step_length;
  {
    std::vector<int32_t> step_length_list;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      step_length_list.push_back(routing_layer.getPreferTrackGridList().front().get_step_length());
    }
    step_length = getFrequentNum(step_length_list);
  }
  auto getScaleGrid = [](int32_t real_ll_scale, int32_t real_ur_scale, int32_t start_line, int32_t step_length) {
    while (start_line < step_length) {
      start_line += step_length;
    }
    start_line += real_ll_scale;
    int32_t step_num = (real_ur_scale - start_line) / step_length;
    int32_t end_line = start_line + step_num * step_length;
    if (end_line > real_ur_scale) {
      step_num -= 1;
      end_line = start_line + step_num * step_length;
    }
    if (std::abs(end_line - real_ur_scale) < step_length) {
      step_num -= 1;
      end_line = start_line + step_num * step_length;
    }
    ScaleGrid scale_grid;
    scale_grid.set_start_line(start_line);
    scale_grid.set_step_length(step_length);
    scale_grid.set_step_num(step_num);
    scale_grid.set_end_line(end_line);
    return scale_grid;
  };
  ScaleAxis track_axis;
  {
    track_axis.set_x_grid_list({getScaleGrid(die.get_real_ll_x(), die.get_real_ur_x(), start_line, step_length)});
    track_axis.set_y_grid_list({getScaleGrid(die.get_real_ll_y(), die.get_real_ur_y(), start_line, step_length)});
  }
  for (RoutingLayer& routing_layer : routing_layer_list) {
    routing_layer.set_track_axis(track_axis);
  }
}

void DataManager::makeCutLayerList()
{
  std::vector<CutLayer>& cut_layer_list = _database.get_cut_layer_list();

  for (size_t i = 1; i < cut_layer_list.size(); i++) {
    CutLayer& pre_cut_layer = cut_layer_list[i - 1];
    CutLayer& curr_cut_layer = cut_layer_list[i];
    pre_cut_layer.set_above_spacing(curr_cut_layer.get_below_spacing());
    pre_cut_layer.set_above_prl(curr_cut_layer.get_below_prl());
    pre_cut_layer.set_above_prl_spacing(curr_cut_layer.get_below_prl_spacing());
  }
  cut_layer_list.back().set_above_spacing(0);
  cut_layer_list.back().set_above_prl(0);
  cut_layer_list.back().set_above_prl_spacing(0);
}

void DataManager::checkLayerList()
{
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  std::vector<CutLayer>& cut_layer_list = _database.get_cut_layer_list();

  if (routing_layer_list.empty()) {
    RTLOG.error(Loc::current(), "The routing_layer_list is empty!");
  }
  if (cut_layer_list.empty()) {
    RTLOG.error(Loc::current(), "The cut_layer_list is empty!");
  }
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::string& layer_name = routing_layer.get_layer_name();
    if (routing_layer.get_prefer_direction() == Direction::kNone) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' prefer_direction is none!");
    }
    for (ScaleGrid& x_track_grid : routing_layer.getXTrackGridList()) {
      if (x_track_grid.get_step_length() <= 0) {
        RTLOG.error(Loc::current(), "The layer '", layer_name, "' x_track_grid step length '", x_track_grid.get_step_length(), "' is wrong!");
      }
    }
    for (ScaleGrid& y_track_grid : routing_layer.getYTrackGridList()) {
      if (y_track_grid.get_step_length() <= 0) {
        RTLOG.error(Loc::current(), "The layer '", layer_name, "' y_track_grid step length '", y_track_grid.get_step_length(), "' is wrong!");
      }
    }
    SpacingTable& prl_spacing_table = routing_layer.get_prl_spacing_table();
    if (prl_spacing_table.get_width_list().empty()) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' spacing width_list is empty!");
    }
    if (routing_layer.get_notch_spacing() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' notch_spacing == -1!");
    }
    if (prl_spacing_table.get_parallel_length_list().empty()) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' spacing parallel_length_list is empty!");
    }
    if (routing_layer.get_eol_spacing() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' eol_spacing == -1!");
    }
    if (routing_layer.get_eol_within() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' eol_within == -1!");
    }
  }
  for (CutLayer& cut_layer : cut_layer_list) {
    std::string& layer_name = cut_layer.get_layer_name();
    if (cut_layer.get_curr_spacing() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' curr_spacing == -1!");
    }
    if (cut_layer.get_curr_prl() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' curr_prl == -1!");
    }
    if (cut_layer.get_curr_prl_spacing() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' curr_prl_spacing == -1!");
    }
    if (cut_layer.get_curr_eol_spacing() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' curr_eol_spacing == -1!");
    }
    if (cut_layer.get_above_spacing() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' above_spacing == -1!");
    }
    if (cut_layer.get_above_prl() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' above_prl == -1!");
    }
    if (cut_layer.get_above_prl_spacing() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' above_prl_spacing == -1!");
    }
    if (cut_layer.get_below_spacing() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' below_spacing == -1!");
    }
    if (cut_layer.get_below_prl() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' below_prl == -1!");
    }
    if (cut_layer.get_below_prl_spacing() == -1) {
      RTLOG.error(Loc::current(), "The layer '", layer_name, "' below_prl_spacing == -1!");
    }
  }
}

void DataManager::buildLayerInfo()
{
  std::map<int32_t, std::vector<int32_t>>& routing_to_adjacent_cut_map = _database.get_routing_to_adjacent_cut_map();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = _database.get_cut_to_adjacent_routing_map();

  std::vector<std::tuple<int32_t, bool, int32_t>> order_routing_layer_list;
  for (RoutingLayer& routing_layer : _database.get_routing_layer_list()) {
    order_routing_layer_list.emplace_back(routing_layer.get_layer_order(), true, routing_layer.get_layer_idx());
  }
  for (CutLayer& cut_layer : _database.get_cut_layer_list()) {
    order_routing_layer_list.emplace_back(cut_layer.get_layer_order(), false, cut_layer.get_layer_idx());
  }
  std::sort(order_routing_layer_list.begin(), order_routing_layer_list.end(),
            [](std::tuple<int32_t, bool, int32_t>& a, std::tuple<int32_t, bool, int32_t>& b) { return std::get<0>(a) < std::get<0>(b); });
  for (int32_t i = 0; i < static_cast<int32_t>(order_routing_layer_list.size()); i++) {
    if (std::get<1>(order_routing_layer_list[i]) == true) {
      if (i - 1 >= 0) {
        routing_to_adjacent_cut_map[std::get<2>(order_routing_layer_list[i])].push_back(std::get<2>(order_routing_layer_list[i - 1]));
      }
      if (i + 1 < static_cast<int32_t>(order_routing_layer_list.size())) {
        routing_to_adjacent_cut_map[std::get<2>(order_routing_layer_list[i])].push_back(std::get<2>(order_routing_layer_list[i + 1]));
      }
    } else {
      if (i - 1 >= 0) {
        cut_to_adjacent_routing_map[std::get<2>(order_routing_layer_list[i])].push_back(std::get<2>(order_routing_layer_list[i - 1]));
      }
      if (i + 1 < static_cast<int32_t>(order_routing_layer_list.size())) {
        cut_to_adjacent_routing_map[std::get<2>(order_routing_layer_list[i])].push_back(std::get<2>(order_routing_layer_list[i + 1]));
      }
    }
  }
}

void DataManager::buildGCellAxis()
{
  makeGCellAxis();
  checkGCellAxis();
}

void DataManager::makeGCellAxis()
{
  ScaleAxis& gcell_axis = _database.get_gcell_axis();

  gcell_axis.set_x_grid_list(makeGCellGridList(Direction::kVertical));
  gcell_axis.set_y_grid_list(makeGCellGridList(Direction::kHorizontal));
}

std::vector<ScaleGrid> DataManager::makeGCellGridList(Direction direction)
{
  Die& die = _database.get_die();
  Row& row = _database.get_row();
  int32_t row_height = row.get_height();
  int32_t only_offset = getOnlyOffset(direction);
  int32_t only_pitch = getOnlyPitch();

  int32_t die_start_scale = (direction == Direction::kVertical ? die.get_real_ll_x() : die.get_real_ll_y());
  int32_t die_end_scale = (direction == Direction::kVertical ? die.get_real_ur_x() : die.get_real_ur_y());
  int32_t step_length = (row_height / only_pitch) * only_pitch + ((row_height % only_pitch) >= (only_pitch / 2) ? only_pitch : 0);

  std::vector<int32_t> gcell_scale_list;
  gcell_scale_list.push_back(die_start_scale);
  for (int32_t gcell_scale = only_offset - (only_pitch / 2); gcell_scale <= die_end_scale; gcell_scale += step_length) {
    gcell_scale_list.push_back(gcell_scale);
  }
  gcell_scale_list.push_back(die_end_scale);

  std::sort(gcell_scale_list.begin(), gcell_scale_list.end());
  // 删除小于step_length的
  for (int32_t i = 2; i < static_cast<int32_t>(gcell_scale_list.size()); i++) {
    if (std::abs(gcell_scale_list[i - 2] - gcell_scale_list[i - 1]) < step_length || std::abs(gcell_scale_list[i - 1] - gcell_scale_list[i]) < step_length) {
      gcell_scale_list[i - 1] = gcell_scale_list[i - 2];
    }
  }
  gcell_scale_list.erase(std::unique(gcell_scale_list.begin(), gcell_scale_list.end()), gcell_scale_list.end());
  return RTUTIL.makeScaleGridList(gcell_scale_list);
}

void DataManager::checkGCellAxis()
{
  ScaleAxis& gcell_axis = _database.get_gcell_axis();
  std::vector<ScaleGrid>& x_grid_list = gcell_axis.get_x_grid_list();
  std::vector<ScaleGrid>& y_grid_list = gcell_axis.get_y_grid_list();

  if (x_grid_list.empty() || y_grid_list.empty()) {
    RTLOG.error(Loc::current(), "The gcell grid list is empty!");
  }
  for (size_t i = 0; i < x_grid_list.size(); i++) {
    if (x_grid_list[i].get_step_length() <= 0) {
      RTLOG.error(Loc::current(), "The step length of x grid '", x_grid_list[i].get_step_length(), "' is wrong!");
    }
  }
  for (size_t i = 0; i < y_grid_list.size(); i++) {
    if (y_grid_list[i].get_step_length() <= 0) {
      RTLOG.error(Loc::current(), "The step length of y grid '", y_grid_list[i].get_step_length(), "' is wrong!");
    }
  }
  for (size_t i = 1; i < x_grid_list.size(); i++) {
    if (x_grid_list[i - 1].get_end_line() < x_grid_list[i].get_start_line()) {
      RTLOG.error(Loc::current(), "The x grid with gap '", x_grid_list[i - 1].get_end_line(), " < ", x_grid_list[i].get_start_line(), "'!");
    } else if (x_grid_list[i - 1].get_end_line() > x_grid_list[i].get_start_line()) {
      RTLOG.error(Loc::current(), "The x grid with overlapping '", x_grid_list[i - 1].get_end_line(), " < ", x_grid_list[i].get_start_line(), "'!");
    }
  }
  for (size_t i = 1; i < y_grid_list.size(); i++) {
    if (y_grid_list[i - 1].get_end_line() < y_grid_list[i].get_start_line()) {
      RTLOG.error(Loc::current(), "The y grid with gap '", y_grid_list[i - 1].get_end_line(), " < ", y_grid_list[i].get_start_line(), "'!");
    } else if (y_grid_list[i - 1].get_end_line() > y_grid_list[i].get_start_line()) {
      RTLOG.error(Loc::current(), "The y grid with overlapping '", y_grid_list[i - 1].get_end_line(), " > ", y_grid_list[i].get_start_line(), "'!");
    }
  }
}

void DataManager::buildDie()
{
  makeDie();
  checkDie();
}

void DataManager::makeDie()
{
  Die& die = _database.get_die();
  ScaleAxis& gcell_axis = _database.get_gcell_axis();
  die.set_grid_rect(RTUTIL.getOpenGCellGridRect(die.get_real_rect(), gcell_axis));
}

void DataManager::checkDie()
{
  Die& die = _database.get_die();

  if (die.get_real_ll_x() < 0 || die.get_real_ll_y() < 0 || die.get_real_ur_x() < 0 || die.get_real_ur_y() < 0) {
    RTLOG.error(Loc::current(), "The die '(", die.get_real_ll_x(), " , ", die.get_real_ll_y(), ") - (", die.get_real_ur_x(), " , ", die.get_real_ur_y(),
                ")' is wrong!");
  }
  if ((die.get_real_ur_x() <= die.get_real_ll_x()) || (die.get_real_ur_y() <= die.get_real_ll_y())) {
    RTLOG.error(Loc::current(), "The die '(", die.get_real_ll_x(), " , ", die.get_real_ll_y(), ") - (", die.get_real_ur_x(), " , ", die.get_real_ur_y(),
                ")' is wrong!");
  }
}

void DataManager::buildMacroList()
{
  makeMacroList();
  checkMacroList();
}

void DataManager::makeMacroList()
{
  Die& die = _database.get_die();
  std::vector<Macro>& macro_list = _database.get_macro_list();
  std::vector<Macro> valid_macro_list;
  valid_macro_list.reserve(macro_list.size());

  for (Macro& macro : macro_list) {
    if (!RTUTIL.hasRegularRect(macro.get_body_rect(), die.get_real_rect())) {
      continue;
    }
    macro.set_body_rect(RTUTIL.getRegularRect(macro.get_body_rect(), die.get_real_rect()));
    valid_macro_list.push_back(macro);
  }
  macro_list = valid_macro_list;
}

void DataManager::checkMacroList()
{
  Die& die = _database.get_die();
  std::vector<Macro>& macro_list = _database.get_macro_list();

  for (Macro& macro : macro_list) {
    if (!RTUTIL.hasRegularRect(macro.get_body_rect(), die.get_real_rect())) {
      RTLOG.error(Loc::current(), "The macro is outside die for instance ", macro.get_inst_name());
    }
  }
}

void DataManager::buildLayerViaMasterList()
{
  transLayerViaMasterList();
  makeLayerViaMasterList();
}

void DataManager::transLayerViaMasterList()
{
  std::map<int32_t, int32_t>& routing_idb_layer_id_to_idx_map = _database.get_routing_idb_layer_id_to_idx_map();
  std::map<int32_t, int32_t>& cut_idb_layer_id_to_idx_map = _database.get_cut_idb_layer_id_to_idx_map();
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = _database.get_layer_via_master_list();

  for (std::vector<ViaMaster>& via_master_list : layer_via_master_list) {
    for (ViaMaster& via_master : via_master_list) {
      // above
      LayerRect& above_enclosure = via_master.get_above_enclosure();
      above_enclosure.set_layer_idx(routing_idb_layer_id_to_idx_map[above_enclosure.get_layer_idx()]);
      // below
      LayerRect& below_enclosure = via_master.get_below_enclosure();
      below_enclosure.set_layer_idx(routing_idb_layer_id_to_idx_map[below_enclosure.get_layer_idx()]);
      // cut
      via_master.set_cut_layer_idx(cut_idb_layer_id_to_idx_map[via_master.get_cut_layer_idx()]);
    }
  }
}

void DataManager::makeLayerViaMasterList()
{
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = _database.get_layer_via_master_list();
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  std::vector<Net>& net_list = _database.get_net_list();
  {
    std::vector<ViaMaster> first_via_master_list;
    for (ViaMaster& via_master : layer_via_master_list.front()) {
      int32_t below_layer_idx = via_master.get_below_enclosure().get_layer_idx();
      if (below_layer_idx == 0) {
        first_via_master_list.push_back(via_master);
      } else {
        layer_via_master_list[below_layer_idx].push_back(via_master);
      }
    }
    layer_via_master_list[0] = first_via_master_list;
  }
  for (size_t layer_idx = 0; layer_idx < layer_via_master_list.size(); layer_idx++) {
    std::vector<ViaMaster>& via_master_list = layer_via_master_list[layer_idx];
    for (ViaMaster& via_master : via_master_list) {
      // above
      LayerRect& above_enclosure = via_master.get_above_enclosure();
      via_master.set_above_direction(above_enclosure.getRectDirection(Direction::kNone));
      // below
      LayerRect& below_enclosure = via_master.get_below_enclosure();
      via_master.set_below_direction(below_enclosure.getRectDirection(Direction::kNone));
    }
  }
  std::vector<Direction> direction_list;
  {
    for (RoutingLayer& routing_layer : routing_layer_list) {
      direction_list.push_back(routing_layer.get_prefer_direction());
    }
    // 只对第一层进行方向分析
    std::map<Direction, int32_t> direction_num_map;
    for (Net& net : net_list) {
      for (Pin& pin : net.get_pin_list()) {
        for (EXTLayerRect& routing_shape : pin.get_routing_shape_list()) {
          if (routing_shape.get_layer_idx() != 0) {
            continue;
          }
          direction_num_map[routing_shape.get_real_rect().getRectDirection(routing_layer_list[routing_shape.get_layer_idx()].get_prefer_direction())]++;
        }
      }
    }
    Direction first_direction = direction_list.front();
    int32_t max_num = INT32_MIN;
    for (auto& [direction, num] : direction_num_map) {
      if (max_num < num) {
        first_direction = direction;
        max_num = num;
      }
    }
    direction_list.front() = first_direction;
  }
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(layer_via_master_list.size()); layer_idx++) {
    std::vector<ViaMaster>& via_master_list = layer_via_master_list[layer_idx];
    std::sort(via_master_list.begin(), via_master_list.end(), [&direction_list](ViaMaster& a, ViaMaster& b) { return CmpViaMaster()(a, b, direction_list); });
    for (int32_t i = 0; i < static_cast<int32_t>(via_master_list.size()); i++) {
      via_master_list[i].set_via_master_idx(layer_idx, i);
    }
  }
}

void DataManager::buildLayerViaMasterInfo()
{
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = _database.get_layer_via_master_list();
  std::map<int32_t, PlanarRect>& layer_enclosure_map = _database.get_layer_enclosure_map();
  std::map<int32_t, PlanarRect>& layer_cut_shape_map = _database.get_layer_cut_shape_map();

  int32_t start_layer_idx = 0;
  int32_t end_layer_idx = static_cast<int32_t>(routing_layer_list.size()) - 1;

  layer_enclosure_map[start_layer_idx] = layer_via_master_list[start_layer_idx].front().get_below_enclosure();
  for (int32_t layer_idx = 1; layer_idx < end_layer_idx; layer_idx++) {
    std::vector<PlanarRect> rect_list;
    rect_list.push_back(layer_via_master_list[layer_idx - 1].front().get_above_enclosure());
    rect_list.push_back(layer_via_master_list[layer_idx].front().get_below_enclosure());
    layer_enclosure_map[layer_idx] = RTUTIL.getBoundingBox(rect_list);
  }
  layer_enclosure_map[end_layer_idx] = layer_via_master_list[end_layer_idx - 1].front().get_above_enclosure();

  for (int32_t layer_idx = 0; layer_idx < end_layer_idx; layer_idx++) {
    ViaMaster& via_master = layer_via_master_list[layer_idx].front();
    layer_cut_shape_map[via_master.get_cut_layer_idx()] = via_master.get_cut_shape_list().front();
  }
}

void DataManager::buildObstacleList()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  transObstacleList();
  makeObstacleList();
  checkObstacleList();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::transObstacleList()
{
  std::map<int32_t, int32_t>& routing_idb_layer_id_to_idx_map = _database.get_routing_idb_layer_id_to_idx_map();
  std::map<int32_t, int32_t>& cut_idb_layer_id_to_idx_map = _database.get_cut_idb_layer_id_to_idx_map();
  std::vector<Obstacle>& routing_obstacle_list = _database.get_routing_obstacle_list();
  std::vector<Obstacle>& cut_obstacle_list = _database.get_cut_obstacle_list();

#pragma omp parallel for
  for (Obstacle& obstacle : routing_obstacle_list) {
    obstacle.set_layer_idx(routing_idb_layer_id_to_idx_map[obstacle.get_layer_idx()]);
  }
#pragma omp parallel for
  for (Obstacle& obstacle : cut_obstacle_list) {
    obstacle.set_layer_idx(cut_idb_layer_id_to_idx_map[obstacle.get_layer_idx()]);
  }
}

void DataManager::makeObstacleList()
{
  ScaleAxis& gcell_axis = _database.get_gcell_axis();
  Die& die = _database.get_die();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = _database.get_cut_to_adjacent_routing_map();
  std::vector<Obstacle>& routing_obstacle_list = _database.get_routing_obstacle_list();
  std::vector<Obstacle>& cut_obstacle_list = _database.get_cut_obstacle_list();

#pragma omp parallel for
  for (Obstacle& routing_obstacle : routing_obstacle_list) {
    if (!RTUTIL.hasRegularRect(routing_obstacle.get_real_rect(), die.get_real_rect())) {
      RTLOG.error(Loc::current(), "This shape is outside the die!");
    }
    routing_obstacle.set_real_rect(RTUTIL.getRegularRect(routing_obstacle.get_real_rect(), die.get_real_rect()));
    routing_obstacle.set_grid_rect(RTUTIL.getClosedGCellGridRect(routing_obstacle.get_real_rect(), gcell_axis));
  }
#pragma omp parallel for
  for (Obstacle& cut_obstacle : cut_obstacle_list) {
    if (!RTUTIL.hasRegularRect(cut_obstacle.get_real_rect(), die.get_real_rect())) {
      RTLOG.error(Loc::current(), "This shape is outside the die!");
    }
    cut_obstacle.set_real_rect(RTUTIL.getRegularRect(cut_obstacle.get_real_rect(), die.get_real_rect()));
    cut_obstacle.set_grid_rect(RTUTIL.getClosedGCellGridRect(cut_obstacle.get_real_rect(), gcell_axis));
  }
  std::set<int32_t> ignore_cut_layer_idx_set;
  for (auto& [cut_layer_idx, routing_layer_idx_list] : cut_to_adjacent_routing_map) {
    if (routing_layer_idx_list.size() == 1) {
      ignore_cut_layer_idx_set.insert(cut_layer_idx);
    }
  }
  std::vector<Obstacle> cut_obstacle_list_temp;
  for (Obstacle& cut_obstacle : cut_obstacle_list) {
    if (!RTUTIL.exist(ignore_cut_layer_idx_set, cut_obstacle.get_layer_idx())) {
      cut_obstacle_list_temp.push_back(cut_obstacle);
    }
  }
  cut_obstacle_list = cut_obstacle_list_temp;
}

void DataManager::checkObstacleList()
{
  Die& die = _database.get_die();
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  std::vector<Obstacle>& routing_obstacle_list = _database.get_routing_obstacle_list();
  std::vector<Obstacle>& cut_obstacle_list = _database.get_cut_obstacle_list();

#pragma omp parallel for
  for (Obstacle& obstacle : routing_obstacle_list) {
    if (obstacle.get_real_ll_x() < die.get_real_ll_x() || obstacle.get_real_ll_y() < die.get_real_ll_y() || die.get_real_ur_x() < obstacle.get_real_ur_x()
        || die.get_real_ur_y() < obstacle.get_real_ur_y()) {
      // log
      RTLOG.error(Loc::current(), "The obstacle '(", obstacle.get_real_ll_x(), " , ", obstacle.get_real_ll_y(), ") - (", obstacle.get_real_ur_x(), " , ",
                  obstacle.get_real_ur_y(), ") ", routing_layer_list[obstacle.get_layer_idx()].get_layer_name(), "' is wrong! Die '(", die.get_real_ll_x(),
                  " , ", die.get_real_ll_y(), ") - (", die.get_real_ur_x(), " , ", die.get_real_ur_y(), ")'");
    }
  }
#pragma omp parallel for
  for (Obstacle& obstacle : cut_obstacle_list) {
    if (obstacle.get_real_ll_x() < die.get_real_ll_x() || obstacle.get_real_ll_y() < die.get_real_ll_y() || die.get_real_ur_x() < obstacle.get_real_ur_x()
        || die.get_real_ur_y() < obstacle.get_real_ur_y()) {
      // log
      RTLOG.error(Loc::current(), "The obstacle '(", obstacle.get_real_ll_x(), " , ", obstacle.get_real_ll_y(), ") - (", obstacle.get_real_ur_x(), " , ",
                  obstacle.get_real_ur_y(), ") ", routing_layer_list[obstacle.get_layer_idx()].get_layer_name(), "' is wrong! Die '(", die.get_real_ll_x(),
                  " , ", die.get_real_ll_y(), ") - (", die.get_real_ur_x(), " , ", die.get_real_ur_y(), ")'");
    }
  }
}

void DataManager::buildNetList()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  std::vector<Net>& net_list = _database.get_net_list();
#pragma omp parallel for
  for (size_t net_idx = 0; net_idx < net_list.size(); net_idx++) {
    Net& net = net_list[net_idx];
    net.set_net_idx(static_cast<int32_t>(net_idx));
    buildPinList(net);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::buildPinList(Net& net)
{
  transPinList(net);
  makePinList(net);
  checkPinList(net);
}

void DataManager::transPinList(Net& net)
{
  std::map<int32_t, int32_t>& routing_idb_layer_id_to_idx_map = _database.get_routing_idb_layer_id_to_idx_map();
  std::map<int32_t, int32_t>& cut_idb_layer_id_to_idx_map = _database.get_cut_idb_layer_id_to_idx_map();

  for (Pin& pin : net.get_pin_list()) {
    for (EXTLayerRect& routing_shape : pin.get_routing_shape_list()) {
      routing_shape.set_layer_idx(routing_idb_layer_id_to_idx_map[routing_shape.get_layer_idx()]);
    }
    for (EXTLayerRect& cut_shape : pin.get_cut_shape_list()) {
      cut_shape.set_layer_idx(cut_idb_layer_id_to_idx_map[cut_shape.get_layer_idx()]);
    }
    if (pin.get_preferred_conn_layer_idx() != -1) {
      auto iter = routing_idb_layer_id_to_idx_map.find(pin.get_preferred_conn_layer_idx());
      if (iter != routing_idb_layer_id_to_idx_map.end()) {
        pin.set_preferred_conn_layer_idx(iter->second);
      } else {
        pin.set_preferred_conn_layer_idx(-1);
      }
    }
  }
}

void DataManager::makePinList(Net& net)
{
  Die& die = _database.get_die();
  ScaleAxis& gcell_axis = _database.get_gcell_axis();
  std::vector<Pin>& pin_list = net.get_pin_list();

  for (size_t pin_idx = 0; pin_idx < pin_list.size(); pin_idx++) {
    Pin& pin = pin_list[pin_idx];
    pin.set_pin_idx(static_cast<int32_t>(pin_idx));
    for (EXTLayerRect& routing_shape : pin.get_routing_shape_list()) {
      if (!RTUTIL.hasRegularRect(routing_shape.get_real_rect(), die.get_real_rect())) {
        RTLOG.error(Loc::current(), "This shape is outside the die!");
      }
      routing_shape.set_real_rect(RTUTIL.getRegularRect(routing_shape.get_real_rect(), die.get_real_rect()));
      routing_shape.set_grid_rect(RTUTIL.getClosedGCellGridRect(routing_shape.get_real_rect(), gcell_axis));
    }
    for (EXTLayerRect& cut_shape : pin.get_cut_shape_list()) {
      if (!RTUTIL.hasRegularRect(cut_shape.get_real_rect(), die.get_real_rect())) {
        RTLOG.error(Loc::current(), "This shape is outside the die!");
      }
      cut_shape.set_real_rect(RTUTIL.getRegularRect(cut_shape.get_real_rect(), die.get_real_rect()));
      cut_shape.set_grid_rect(RTUTIL.getClosedGCellGridRect(cut_shape.get_real_rect(), gcell_axis));
    }
  }
}

void DataManager::checkPinList(Net& net)
{
  Die& die = _database.get_die();
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  std::vector<CutLayer>& cut_layer_list = _database.get_cut_layer_list();

  for (Pin& pin : net.get_pin_list()) {
    for (EXTLayerRect& routing_shape : pin.get_routing_shape_list()) {
      if (routing_shape.get_real_ll_x() < die.get_real_ll_x() || routing_shape.get_real_ll_y() < die.get_real_ll_y()
          || die.get_real_ur_x() < routing_shape.get_real_ur_x() || die.get_real_ur_y() < routing_shape.get_real_ur_y()) {
        RTLOG.error(Loc::current(), "The pin_shape '(", routing_shape.get_real_ll_x(), " , ", routing_shape.get_real_ll_y(), ") - (",
                    routing_shape.get_real_ur_x(), " , ", routing_shape.get_real_ur_y(), ") ",
                    routing_layer_list[routing_shape.get_layer_idx()].get_layer_name(), "' is wrong! Die '(", die.get_real_ll_x(), " , ", die.get_real_ll_y(),
                    ") - (", die.get_real_ur_x(), " , ", die.get_real_ur_y(), ")'");
      }
    }
    for (EXTLayerRect& cut_shape : pin.get_cut_shape_list()) {
      if (cut_shape.get_real_ll_x() < die.get_real_ll_x() || cut_shape.get_real_ll_y() < die.get_real_ll_y() || die.get_real_ur_x() < cut_shape.get_real_ur_x()
          || die.get_real_ur_y() < cut_shape.get_real_ur_y()) {
        RTLOG.error(Loc::current(), "The pin_shape '(", cut_shape.get_real_ll_x(), " , ", cut_shape.get_real_ll_y(), ") - (", cut_shape.get_real_ur_x(), " , ",
                    cut_shape.get_real_ur_y(), ") ", cut_layer_list[cut_shape.get_layer_idx()].get_layer_name(), "' is wrong! Die '(", die.get_real_ll_x(),
                    " , ", die.get_real_ll_y(), ") - (", die.get_real_ur_x(), " , ", die.get_real_ur_y(), ")'");
      }
    }
  }
}

void DataManager::buildDetectionDistance()
{
  _database.set_detection_distance(5 * getOnlyPitch());
}

void DataManager::buildGCellMap()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  initGCellMap();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::initGCellMap()
{
  ScaleAxis& gcell_axis = _database.get_gcell_axis();
  Die& die = _database.get_die();

  GridMap<PlanarRect>& gcell_map = _database.get_gcell_map();
  gcell_map.init(die.getXSize(), die.getYSize());

#pragma omp parallel for collapse(2)
  for (int32_t x = 0; x < gcell_map.get_x_size(); x++) {
    for (int32_t y = 0; y < gcell_map.get_y_size(); y++) {
      PlanarRect real_rect = RTUTIL.getRealRectByGCell(PlanarCoord(x, y), gcell_axis);
      gcell_map[x][y].set_ll(real_rect.get_ll());
      gcell_map[x][y].set_ur(real_rect.get_ur());
    }
  }
}

void DataManager::buildFixRectMap()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  Die& die = _database.get_die();
  std::vector<Obstacle>& routing_obstacle_list = _database.get_routing_obstacle_list();
  std::vector<Obstacle>& cut_obstacle_list = _database.get_cut_obstacle_list();
  std::vector<Net>& net_list = _database.get_net_list();
  int32_t detection_distance = _database.get_detection_distance();
  std::array<std::map<int32_t, std::vector<Database::FixedRectRTree::value_type>>, 2> type_layer_value_list_map;

  for (Obstacle& obstacle : routing_obstacle_list) {
    PlanarRect real_rect = RTUTIL.getEnlargedRect(obstacle.get_real_rect(), detection_distance);
    if (RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
      type_layer_value_list_map[true][obstacle.get_layer_idx()].emplace_back(RTUTIL.convertToBGRectInt(obstacle.get_real_rect()),
                                                                             std::make_pair(-1, &obstacle));
    }
  }
  for (Obstacle& obstacle : cut_obstacle_list) {
    PlanarRect real_rect = RTUTIL.getEnlargedRect(obstacle.get_real_rect(), detection_distance);
    if (RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
      type_layer_value_list_map[false][obstacle.get_layer_idx()].emplace_back(RTUTIL.convertToBGRectInt(obstacle.get_real_rect()),
                                                                              std::make_pair(-1, &obstacle));
    }
  }
  for (Net& net : net_list) {
    for (Pin& pin : net.get_pin_list()) {
      for (EXTLayerRect& shape : pin.get_routing_shape_list()) {
        PlanarRect real_rect = RTUTIL.getEnlargedRect(shape.get_real_rect(), detection_distance);
        if (RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
          type_layer_value_list_map[true][shape.get_layer_idx()].emplace_back(RTUTIL.convertToBGRectInt(shape.get_real_rect()),
                                                                              std::make_pair(net.get_net_idx(), &shape));
        }
      }
      for (EXTLayerRect& shape : pin.get_cut_shape_list()) {
        PlanarRect real_rect = RTUTIL.getEnlargedRect(shape.get_real_rect(), detection_distance);
        if (RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
          type_layer_value_list_map[false][shape.get_layer_idx()].emplace_back(RTUTIL.convertToBGRectInt(shape.get_real_rect()),
                                                                               std::make_pair(net.get_net_idx(), &shape));
        }
      }
    }
  }
  auto& type_layer_fixed_rect_rtree_map = _database.get_type_layer_fixed_rect_rtree_map();
  for (bool is_routing : {false, true}) {
    for (auto& [layer_idx, value_list] : type_layer_value_list_map[is_routing]) {
      type_layer_fixed_rect_rtree_map[is_routing][layer_idx] = Database::FixedRectRTree(value_list.begin(), value_list.end());
    }
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::printConfig()
{
  /////////////////////////////////////////////
  // **********        RT         ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(0), "RT_CONFIG_INPUT");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "thread_number");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _config.thread_number);
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "bottom_routing_layer");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _config.bottom_routing_layer);
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "top_routing_layer");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _config.top_routing_layer);
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "output_inter_result");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _config.output_inter_result);
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "enable_timing");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _config.enable_timing);
  // **********        RT         ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(0), "RT_CONFIG_BUILD");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "log_file_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _config.log_file_path);
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "bottom_routing_layer_idx");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _config.bottom_routing_layer_idx);
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "top_routing_layer_idx");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _config.top_routing_layer_idx);
  // **********     DataManager     ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "DataManager");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "dm_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.dm_temp_directory_path);
  // **********     DRCEngine     ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "DRCEngine");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "de_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.de_temp_directory_path);
  // **********    GDSPlotter     ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "GDSPlotter");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "gp_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.gp_temp_directory_path);
  // **********    PinAccessor    ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "PinAccessor");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "pa_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.pa_temp_directory_path);
  // **********  SupplyAnalyzer   ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "SupplyAnalyzer");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "sa_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.sa_temp_directory_path);
  // **********    PlanarRouter    ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "PlanarRouter");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "pr_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.pr_temp_directory_path);
  // **********   LayerAssigner   ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "LayerAssigner");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "la_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.la_temp_directory_path);
  // **********    SpaceRouter    ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "SpaceRouter");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "sr_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.sr_temp_directory_path);
  // **********   TrackAssigner   ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "TrackAssigner");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "ta_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.ta_temp_directory_path);
  // **********  DetailedRouter   ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "DetailedRouter");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "dr_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.dr_temp_directory_path);
  // ********** ViolationReporter ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "ViolationReporter");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "vr_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.vr_temp_directory_path);
  // **********    EarlyRouter    ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "EarlyRouter");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "er_temp_directory_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), _config.er_temp_directory_path);
  /////////////////////////////////////////////
}

void DataManager::printDatabase()
{
  /////////////////////////////////////////////
  // **********        RT         ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(0), "RT_DATABASE");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "design_name");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _database.get_design_name());
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "lef_file_path_list");
  for (std::string& lef_file_path : _database.get_lef_file_path_list()) {
    RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), lef_file_path);
  }
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "def_file_path");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _database.get_def_file_path());
  // **********     MicronDBU     ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "micron_dbu");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _database.get_micron_dbu());
  // **********  ManufactureGrid  ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "manufacture_grid");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _database.get_manufacture_grid());
  // **********     GCellAxis     ********** //
  ScaleAxis& gcell_axis = _database.get_gcell_axis();
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "gcell_axis");
  std::vector<ScaleGrid>& x_grid_list = gcell_axis.get_x_grid_list();
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "x_grid_list");
  for (ScaleGrid& x_grid : x_grid_list) {
    RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), "start:", x_grid.get_start_line(), " step_length:", x_grid.get_step_length(),
               " step_num:", x_grid.get_step_num(), " end:", x_grid.get_end_line());
  }
  std::vector<ScaleGrid>& y_grid_list = gcell_axis.get_y_grid_list();
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "y_grid_list");
  for (ScaleGrid& y_grid : y_grid_list) {
    RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), "start:", y_grid.get_start_line(), " step_length:", y_grid.get_step_length(),
               " step_num:", y_grid.get_step_num(), " end:", y_grid.get_end_line());
  }
  // **********        Die        ********** //
  Die& die = _database.get_die();
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "die");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "real:(", die.get_real_ll_x(), ",", die.get_real_ll_y(), ")-(", die.get_real_ur_x(), ",",
             die.get_real_ur_y(), ")");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "grid:(", die.get_grid_ll_x(), ",", die.get_grid_ll_y(), ")-(", die.get_grid_ur_x(), ",",
             die.get_grid_ur_y(), ")");
  // **********    RoutingLayer   ********** //
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "routing_layer_num");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), routing_layer_list.size());
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "routing_layer");
  for (RoutingLayer& routing_layer : routing_layer_list) {
    RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "idx:", routing_layer.get_layer_idx(), " order:", routing_layer.get_layer_order(),
               " name:", routing_layer.get_layer_name(), " prefer_direction:", GetDirectionName()(routing_layer.get_prefer_direction()));

    ScaleAxis& track_axis = routing_layer.get_track_axis();
    RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "track_axis");
    std::vector<ScaleGrid>& x_grid_list = track_axis.get_x_grid_list();
    RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), "x_grid_list");
    for (ScaleGrid& x_grid : x_grid_list) {
      RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(4), "start:", x_grid.get_start_line(), " step_length:", x_grid.get_step_length(),
                 " step_num:", x_grid.get_step_num(), " end:", x_grid.get_end_line());
    }
    std::vector<ScaleGrid>& y_grid_list = track_axis.get_y_grid_list();
    RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(3), "y_grid_list");
    for (ScaleGrid& y_grid : y_grid_list) {
      RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(4), "start:", y_grid.get_start_line(), " step_length:", y_grid.get_step_length(),
                 " step_num:", y_grid.get_step_num(), " end:", y_grid.get_end_line());
    }
  }
  // **********      CutLayer     ********** //
  std::vector<CutLayer>& cut_layer_list = _database.get_cut_layer_list();
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "cut_layer_num");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), cut_layer_list.size());
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "cut_layer");
  for (CutLayer& cut_layer : cut_layer_list) {
    RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), "idx:", cut_layer.get_layer_idx(), " order:", cut_layer.get_layer_order(),
               " name:", cut_layer.get_layer_name());
  }
  // **********      ViaMaster    ********** //
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = _database.get_layer_via_master_list();
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "layer_via_master_list");
  for (size_t below_layer_idx = 0; below_layer_idx < layer_via_master_list.size(); below_layer_idx++) {
    std::string via_master_name_string = (routing_layer_list[below_layer_idx].get_layer_name() + ": ");
    for (ViaMaster& via_master : layer_via_master_list[below_layer_idx]) {
      via_master_name_string += (via_master.get_via_name() + " ");
    }
    RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), via_master_name_string);
  }
  // **********      Obstacle     ********** //
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "routing_obstacle_num");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _database.get_routing_obstacle_list().size());
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "cut_obstacle_num");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), _database.get_cut_obstacle_list().size());
  // **********       Macro       ********** //
  std::vector<Macro>& macro_list = _database.get_macro_list();
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "macro_num");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), macro_list.size());
  // **********        Net        ********** //
  std::vector<Net>& net_list = _database.get_net_list();
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "net_num");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), net_list.size());
  size_t total_pin_num = 0;
  for (Net& net : net_list) {
    total_pin_num += net.get_pin_list().size();
  }
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "pin_num");
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), total_pin_num);
  RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(1), "pin_num_ratio");

  size_t pin_num_upper_limit = 100;
  std::map<size_t, size_t> pin_net_map;
  for (Net& net : net_list) {
    pin_net_map[std::min(net.get_pin_list().size(), pin_num_upper_limit)]++;
  }
  for (auto& [pin_num, net_num] : pin_net_map) {
    std::string head_info = "net with ";
    if (pin_num == pin_num_upper_limit) {
      head_info += ">=";
    }
    RTLOG.info(Loc::current(), RTUTIL.getSpaceByTabNum(2), head_info, pin_num, " pins: ", net_num, "(", RTUTIL.getPercentage(net_num, net_list.size()), ")");
  }
  /////////////////////////////////////////////
}

void DataManager::outputScript()
{
  std::string& dm_temp_directory_path = _config.dm_temp_directory_path;

  std::ofstream* python_file = RTUTIL.getOutputFileStream(RTUTIL.getString(dm_temp_directory_path, "plot.py"));
  RTUTIL.pushStream(python_file, "import os", "\n");
  RTUTIL.pushStream(python_file, "import pandas as pd", "\n");
  RTUTIL.pushStream(python_file, "import matplotlib.pyplot as plt", "\n");
  RTUTIL.pushStream(python_file, "import seaborn as sns", "\n");
  RTUTIL.pushStream(python_file, "import numpy as np", "\n");
  RTUTIL.pushStream(python_file, "from multiprocessing import Pool", "\n");
  RTUTIL.pushStream(python_file, "directory = os.getcwd()", "\n");
  RTUTIL.pushStream(python_file, "output_dir = os.path.join(directory, 'output_png')", "\n");
  RTUTIL.pushStream(python_file, "os.makedirs(output_dir, exist_ok=True)", "\n");
  RTUTIL.pushStream(python_file, "def process_csv(filename):", "\n");
  RTUTIL.pushStream(python_file, "    output_name = os.path.basename(os.path.dirname(filename)) + \"_\" + os.path.splitext(os.path.basename(filename))[0]",
                    "\n");
  RTUTIL.pushStream(python_file, "    plt.figure()  ", "\n");
  RTUTIL.pushStream(python_file, "    temp = sns.heatmap(np.array(pd.read_csv(filename)), cmap='hot_r')", "\n");
  RTUTIL.pushStream(python_file, "    temp.set_title(output_name)", "\n");
  RTUTIL.pushStream(python_file, "    plt.savefig(os.path.join(output_dir, output_name + \".png\"), dpi=300, bbox_inches='tight')", "\n");
  RTUTIL.pushStream(python_file, "    plt.close()  ", "\n");
  RTUTIL.pushStream(python_file, "csv_files = [os.path.join(root, file) for root, _, files in os.walk(directory) for file in files if file.endswith(\".csv\")]",
                    "\n");
  RTUTIL.pushStream(python_file, "with Pool() as pool:", "\n");
  RTUTIL.pushStream(python_file, "    pool.map(process_csv, csv_files)", "\n");
  RTUTIL.closeFileStream(python_file);
}

#endif

#if 1  // destroy

void DataManager::destroyDatabaseResult()
{
  _database.get_net_global_result_map().clear();
  _database.get_net_detailed_result_map().clear();
  _database.get_net_detailed_patch_map().clear();
  _database.get_access_point_rtree().clear();
  _database.get_global_result_rtree().clear();
  _database.get_violation_rtree().clear();
}

#endif

}  // namespace irt
