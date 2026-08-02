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
#include "MetalInserter.hpp"

#include "GeometryBuilder.h"
#include "IdbDesign.h"
#include "IdbFill.h"
#include "IdbLayout.h"
#include "IdbTrackGrid.h"
#include "Utility.hpp"
#include "idm.h"

namespace izh {

// public

void MetalInserter::initInst()
{
  if (_mi_instance == nullptr) {
    _mi_instance = new MetalInserter();
  }
}

MetalInserter& MetalInserter::getInst()
{
  if (_mi_instance == nullptr) {
    ZHLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_mi_instance;
}

void MetalInserter::destroyInst()
{
  if (_mi_instance != nullptr) {
    delete _mi_instance;
    _mi_instance = nullptr;
  }
}

// function

void MetalInserter::insert(std::map<std::string, std::any> config_map)
{
  Monitor monitor;
  ZHLOG.info(Loc::current(), "Starting...");

  MIModel mi_model = initMIModel(config_map);
  buildMetalFill(mi_model);
  writeMetalFill(mi_model);
  printResult(mi_model);

  ZHLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

#if 1  // 初始化

MIModel MetalInserter::initMIModel(std::map<std::string, std::any>& config_map)
{
  MIModel mi_model;
  setMIComParam(mi_model, config_map);
  initDatabaseInfo(mi_model);
  initMILayerList(mi_model);
  return mi_model;
}

void MetalInserter::setMIComParam(MIModel& mi_model, std::map<std::string, std::any>& config_map)
{
  MIComParam mi_com_param(100.0, 50.0, 0.10, 0.80);
  if (Utility::exist(config_map, std::string("-min_fill_layer"))) {
    mi_com_param.set_min_fill_layer(ZHUTIL.getConfigValue<std::string>(config_map, "-min_fill_layer", ""));
  }
  if (Utility::exist(config_map, std::string("-max_fill_layer"))) {
    mi_com_param.set_max_fill_layer(ZHUTIL.getConfigValue<std::string>(config_map, "-max_fill_layer", ""));
  }
  mi_model.set_mi_com_param(mi_com_param);
}

void MetalInserter::initDatabaseInfo(MIModel& mi_model)
{
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_units() == nullptr || idb_layout->get_die() == nullptr
      || idb_layout->get_die()->get_bounding_box() == nullptr) {
    ZHLOG.error(Loc::current(), "The idb layout data is incomplete!");
  }

  idb::IdbRect* die_rect = idb_layout->get_die()->get_bounding_box();
  mi_model.set_die(MIRect(die_rect->get_low_x(), die_rect->get_low_y(), die_rect->get_high_x(), die_rect->get_high_y()));
  mi_model.set_micron_dbu(idb_layout->get_units()->get_micron_dbu());
  mi_model.set_manufacture_grid(std::max(idb_layout->get_munufacture_grid(), 1));
  if (!mi_model.get_die().is_valid() || mi_model.get_micron_dbu() <= 0) {
    ZHLOG.error(Loc::current(), "The iDB die or DBU is invalid!");
  }
}

void MetalInserter::initMILayerList(MIModel& mi_model)
{
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_layers() == nullptr) {
    ZHLOG.error(Loc::current(), "The iDB routing layer list is null!");
  }

  ecc::geometry::GeometryBuilder geometry_builder;
  std::vector<ecc::geometry::GeometryLayerMetadata> geometry_layer_list = geometry_builder.collect_layer_metadata(*idb_layout);
  std::vector<idb::IdbLayer*>& idb_routing_layer_list = idb_layout->get_layers()->get_routing_layers();
  if (idb_routing_layer_list.empty()) {
    ZHLOG.error(Loc::current(), "The iDB routing layer list is empty!");
  }

  MIComParam& mi_com_param = mi_model.get_mi_com_param();
  if (mi_com_param.get_min_fill_layer().empty()) {
    mi_com_param.set_min_fill_layer(idb_routing_layer_list.front()->get_name());
  }
  if (mi_com_param.get_max_fill_layer().empty()) {
    mi_com_param.set_max_fill_layer(idb_routing_layer_list.back()->get_name());
  }
  int32_t min_fill_layer_idx = getRoutingLayerIdx(idb_routing_layer_list, mi_com_param.get_min_fill_layer());
  int32_t max_fill_layer_idx = getRoutingLayerIdx(idb_routing_layer_list, mi_com_param.get_max_fill_layer());
  if (min_fill_layer_idx < 0 || max_fill_layer_idx < 0 || min_fill_layer_idx > max_fill_layer_idx) {
    ZHLOG.error(Loc::current(), "The metal fill layer range is invalid: ", mi_com_param.get_min_fill_layer(), " to ",
                mi_com_param.get_max_fill_layer());
  }

  std::vector<MILayer> mi_layer_list;
  for (int32_t layer_idx = min_fill_layer_idx; layer_idx <= max_fill_layer_idx; ++layer_idx) {
    idb::IdbLayer* idb_layer = idb_routing_layer_list[layer_idx];
    idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
    if (idb_routing_layer == nullptr) {
      continue;
    }
    mi_layer_list.push_back(initMILayer(idb_routing_layer, geometry_layer_list));
  }
  if (mi_layer_list.empty()) {
    ZHLOG.error(Loc::current(), "The iDB routing layer list is empty!");
  }
  mi_model.set_mi_layer_list(mi_layer_list);
}

int32_t MetalInserter::getRoutingLayerIdx(const std::vector<idb::IdbLayer*>& idb_routing_layer_list, const std::string& layer_name)
{
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(idb_routing_layer_list.size()); ++layer_idx) {
    if (idb_routing_layer_list[layer_idx]->get_name() == layer_name) {
      return layer_idx;
    }
  }
  return -1;
}

MILayer MetalInserter::initMILayer(idb::IdbLayerRouting* idb_routing_layer,
                                   const std::vector<ecc::geometry::GeometryLayerMetadata>& geometry_layer_list)
{
  MILayer mi_layer;
  mi_layer.set_layer_name(idb_routing_layer->get_name());
  mi_layer.set_geometry_layer_idx(getGeometryLayerIdx(mi_layer.get_layer_name(), geometry_layer_list));
  mi_layer.set_is_horizontal(idb_routing_layer->is_horizontal());
  mi_layer.set_wire_width(idb_routing_layer->get_min_width());
  mi_layer.set_min_wire_length(std::max(mi_layer.get_wire_width(), getCeilDiv(getMinArea(idb_routing_layer), mi_layer.get_wire_width())));

  idb::IdbTrackGrid* prefer_track_grid = idb_routing_layer->get_prefer_track_grid();
  if (prefer_track_grid != nullptr && prefer_track_grid->get_track() != nullptr && prefer_track_grid->get_track()->get_pitch() > 0) {
    mi_layer.set_track_start(static_cast<int32_t>(prefer_track_grid->get_track()->get_start()));
    mi_layer.set_track_pitch(static_cast<int32_t>(prefer_track_grid->get_track()->get_pitch()));
  } else {
    mi_layer.set_track_start(idb_routing_layer->get_offset_prefer());
    mi_layer.set_track_pitch(idb_routing_layer->get_pitch_prefer());
  }
  mi_layer.set_max_spacing(getMaxSpacing(idb_routing_layer));
  if (mi_layer.get_geometry_layer_idx() < 0 || mi_layer.get_wire_width() <= 0 || mi_layer.get_track_pitch() <= 0) {
    ZHLOG.error(Loc::current(), "The routing layer data is invalid: ", mi_layer.get_layer_name());
  }
  return mi_layer;
}

int32_t MetalInserter::getGeometryLayerIdx(const std::string& layer_name,
                                            const std::vector<ecc::geometry::GeometryLayerMetadata>& geometry_layer_list)
{
  for (const ecc::geometry::GeometryLayerMetadata& geometry_layer : geometry_layer_list) {
    if (geometry_layer.name == layer_name) {
      return static_cast<int32_t>(geometry_layer.layer_id);
    }
  }
  return -1;
}

int32_t MetalInserter::getMinArea(idb::IdbLayerRouting* idb_routing_layer)
{
  int32_t min_area = idb_routing_layer->get_area();
  idb::IdbMinEncloseAreaList* min_enclose_area_list = idb_routing_layer->get_min_enclose_area_list();
  if (min_enclose_area_list != nullptr) {
    for (const idb::IdbMinEncloseArea& min_enclose_area : min_enclose_area_list->get_min_area_list()) {
      if (min_enclose_area._area > 0 && (min_area <= 0 || min_enclose_area._area < min_area)) {
        min_area = min_enclose_area._area;
      }
    }
  }
  return std::max(min_area, 0);
}

int32_t MetalInserter::getCeilDiv(int32_t dividend, int32_t divisor)
{
  if (dividend <= 0 || divisor <= 0) {
    return 0;
  }
  return (dividend - 1) / divisor + 1;
}

int32_t MetalInserter::getMaxSpacing(idb::IdbLayerRouting* idb_routing_layer)
{
  int32_t max_spacing = 0;
  idb::IdbLayerSpacingList* spacing_list = idb_routing_layer->get_spacing_list();
  if (spacing_list != nullptr) {
    for (idb::IdbLayerSpacing* spacing : spacing_list->get_spacing_list()) {
      if (spacing != nullptr) {
        max_spacing = std::max(max_spacing, spacing->get_min_spacing());
      }
    }
  }
  std::shared_ptr<idb::IdbLayerSpacingTable> spacing_table = idb_routing_layer->get_spacing_table();
  if (spacing_table != nullptr && spacing_table->is_parallel() && spacing_table->get_parallel() != nullptr) {
    for (const std::vector<int32_t>& spacing_row : spacing_table->get_parallel()->get_spacing_table()) {
      for (int32_t spacing : spacing_row) {
        max_spacing = std::max(max_spacing, spacing);
      }
    }
  }
  return std::max(max_spacing, 0);
}

#endif

#if 1  // 构建

void MetalInserter::buildMetalFill(MIModel& mi_model)
{
  ecc::geometry::GeometryStore geometry_store;
  buildGeometryStore(geometry_store);
  for (MILayer& mi_layer : mi_model.get_mi_layer_list()) {
    buildLayerMetalFill(mi_model, geometry_store, mi_layer);
  }
}

void MetalInserter::buildGeometryStore(ecc::geometry::GeometryStore& geometry_store)
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_design == nullptr || idb_layout == nullptr) {
    ZHLOG.error(Loc::current(), "The iDB design or layout is null!");
  }
  ecc::geometry::GeometryBuilder geometry_builder;
  geometry_builder.rebuild_from_design(*idb_design, *idb_layout, geometry_store);
}

void MetalInserter::buildLayerMetalFill(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store, MILayer& mi_layer)
{
  Monitor monitor;
  ZHLOG.info(Loc::current(), "Building metal fill: ", mi_layer.get_layer_name());

  buildDensityWindowList(mi_model, geometry_store, mi_layer);
  for (MIDensityWindow& density_window : mi_layer.get_density_window_list()) {
    if (density_window.get_density() < mi_model.get_mi_com_param().get_min_density()) {
      buildDensityWindowFill(mi_model, geometry_store, mi_layer, density_window);
    }
  }

  ZHLOG.info(Loc::current(), "Completed metal fill: ", mi_layer.get_layer_name(), " inserted_metal_num: ",
             mi_layer.get_inserted_metal_num(), " ", monitor.getStatsInfo());
}

void MetalInserter::buildDensityWindowList(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store, MILayer& mi_layer)
{
  int32_t density_window_size = static_cast<int32_t>(
      std::llround(mi_model.get_mi_com_param().get_density_window_size_micron() * mi_model.get_micron_dbu()));
  int32_t density_window_step = static_cast<int32_t>(
      std::llround(mi_model.get_mi_com_param().get_density_window_step_micron() * mi_model.get_micron_dbu()));
  if (density_window_size <= 0 || density_window_step <= 0) {
    ZHLOG.error(Loc::current(), "The metal density window is invalid!");
  }

  const MIRect& die = mi_model.get_die();
  std::vector<MIDensityWindow> density_window_list;
  int32_t density_window_y_num = 0;
  for (int32_t y = die.get_ll_y(); y < die.get_ur_y(); y += density_window_step) {
    int32_t density_window_x_num = 0;
    for (int32_t x = die.get_ll_x(); x < die.get_ur_x(); x += density_window_step) {
      MIRect density_window_rect(x, y, std::min(x + density_window_size, die.get_ur_x()), std::min(y + density_window_size, die.get_ur_y()));
      density_window_list.emplace_back(density_window_rect, getMetalArea(geometry_store, mi_layer.get_geometry_layer_idx(), density_window_rect));
      ++density_window_x_num;
    }
    mi_layer.set_density_window_x_num(density_window_x_num);
    ++density_window_y_num;
  }
  mi_layer.set_density_window_y_num(density_window_y_num);
  mi_layer.set_density_window_list(density_window_list);
}

double MetalInserter::getMetalArea(ecc::geometry::GeometryStore& geometry_store, int32_t geometry_layer_idx, const MIRect& rect)
{
  std::vector<MIRect> metal_rect_list;
  std::vector<ecc::geometry::ShapeId> shape_id_list =
      geometry_store.query_intersect(static_cast<ecc::geometry::LayerId>(geometry_layer_idx),
                                     ecc::geometry::Rect32{rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y()});
  for (ecc::geometry::ShapeId shape_id : shape_id_list) {
    if (!isMetalShape(geometry_store.owner_of(shape_id).type)) {
      continue;
    }
    const ecc::geometry::ShapeRecord* shape_record = geometry_store.find_shape(shape_id);
    if (shape_record == nullptr) {
      continue;
    }
    MIRect metal_rect(shape_record->bbox.lx, shape_record->bbox.ly, shape_record->bbox.hx, shape_record->bbox.hy);
    MIRect intersect_rect = metal_rect.get_intersect_rect(rect);
    if (intersect_rect.is_valid()) {
      metal_rect_list.push_back(intersect_rect);
    }
  }
  return getUnionArea(metal_rect_list);
}

bool MetalInserter::isMetalShape(ecc::geometry::OwnerType owner_type)
{
  return owner_type == ecc::geometry::OwnerType::kNetWireSegment || owner_type == ecc::geometry::OwnerType::kSpecialWireSegment
         || owner_type == ecc::geometry::OwnerType::kPinPortShape || owner_type == ecc::geometry::OwnerType::kFill
         || owner_type == ecc::geometry::OwnerType::kObs || owner_type == ecc::geometry::OwnerType::kInstancePinPortShape
         || owner_type == ecc::geometry::OwnerType::kIoPinPortShape;
}

double MetalInserter::getUnionArea(const std::vector<MIRect>& rect_list)
{
  std::vector<int32_t> x_coord_list;
  for (const MIRect& rect : rect_list) {
    x_coord_list.push_back(rect.get_ll_x());
    x_coord_list.push_back(rect.get_ur_x());
  }
  std::sort(x_coord_list.begin(), x_coord_list.end());
  x_coord_list.erase(std::unique(x_coord_list.begin(), x_coord_list.end()), x_coord_list.end());

  double union_area = 0.0;
  for (int32_t x_idx = 0; x_idx + 1 < static_cast<int32_t>(x_coord_list.size()); ++x_idx) {
    int32_t ll_x = x_coord_list[x_idx];
    int32_t ur_x = x_coord_list[x_idx + 1];
    std::vector<std::pair<int32_t, int32_t>> y_interval_list;
    for (const MIRect& rect : rect_list) {
      if (rect.get_ll_x() < ur_x && ll_x < rect.get_ur_x()) {
        y_interval_list.emplace_back(rect.get_ll_y(), rect.get_ur_y());
      }
    }
    std::sort(y_interval_list.begin(), y_interval_list.end());
    int32_t current_ll_y = 0;
    int32_t current_ur_y = 0;
    for (const std::pair<int32_t, int32_t>& y_interval : y_interval_list) {
      if (current_ll_y == current_ur_y) {
        current_ll_y = y_interval.first;
        current_ur_y = y_interval.second;
      } else if (y_interval.first <= current_ur_y) {
        current_ur_y = std::max(current_ur_y, y_interval.second);
      } else {
        union_area += static_cast<double>(ur_x - ll_x) * static_cast<double>(current_ur_y - current_ll_y);
        current_ll_y = y_interval.first;
        current_ur_y = y_interval.second;
      }
    }
    union_area += static_cast<double>(ur_x - ll_x) * static_cast<double>(current_ur_y - current_ll_y);
  }
  return union_area;
}

void MetalInserter::buildDensityWindowFill(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store, MILayer& mi_layer,
                                           MIDensityWindow& density_window)
{
  std::vector<int32_t> track_coord_list = getTrackCoordList(mi_layer, density_window.get_rect());
  for (int32_t track_coord : track_coord_list) {
    if (density_window.get_density() >= mi_model.get_mi_com_param().get_min_density()) {
      return;
    }
    std::vector<MIRect> fill_rect_list = getFillRectList(mi_model, geometry_store, mi_layer, density_window.get_rect(), track_coord);
    for (const MIRect& fill_rect : fill_rect_list) {
      if (density_window.get_density() >= mi_model.get_mi_com_param().get_min_density()) {
        return;
      }
      if (!isLegalFillRect(geometry_store, mi_layer, fill_rect) || !isDensityLegal(mi_model, mi_layer, fill_rect)) {
        continue;
      }
      addFillRect(mi_model, geometry_store, mi_layer, fill_rect);
    }
  }
}

std::vector<int32_t> MetalInserter::getTrackCoordList(MILayer& mi_layer, const MIRect& rect)
{
  int32_t lower_half_width = mi_layer.get_wire_width() / 2;
  int32_t upper_half_width = mi_layer.get_wire_width() - lower_half_width;
  int32_t lower_coord = mi_layer.get_is_horizontal() ? rect.get_ll_y() + lower_half_width : rect.get_ll_x() + lower_half_width;
  int32_t upper_coord = mi_layer.get_is_horizontal() ? rect.get_ur_y() - upper_half_width : rect.get_ur_x() - upper_half_width;
  std::vector<int32_t> track_coord_list;
  for (int32_t track_coord = getFirstTrackCoord(lower_coord, mi_layer.get_track_start(), mi_layer.get_track_pitch()); track_coord <= upper_coord;
       track_coord += mi_layer.get_track_pitch()) {
    track_coord_list.push_back(track_coord);
  }
  return track_coord_list;
}

int32_t MetalInserter::getFirstTrackCoord(int32_t coordinate, int32_t track_start, int32_t track_pitch)
{
  if (coordinate <= track_start) {
    return track_start;
  }
  return track_start + getCeilDiv(coordinate - track_start, track_pitch) * track_pitch;
}

std::vector<MIRect> MetalInserter::getFillRectList(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store,
                                                    MILayer& mi_layer, const MIRect& density_window_rect, int32_t track_coord)
{
  MIRect fill_rect = getFillRect(mi_model, mi_layer, density_window_rect, track_coord);
  if (!fill_rect.is_valid()) {
    return {};
  }

  std::vector<std::pair<int32_t, int32_t>> blocked_coord_interval_list = getBlockedCoordIntervalList(geometry_store, mi_layer, fill_rect);
  std::sort(blocked_coord_interval_list.begin(), blocked_coord_interval_list.end());
  std::vector<std::pair<int32_t, int32_t>> merged_blocked_coord_interval_list;
  for (const std::pair<int32_t, int32_t>& blocked_coord_interval : blocked_coord_interval_list) {
    if (merged_blocked_coord_interval_list.empty()
        || merged_blocked_coord_interval_list.back().second < blocked_coord_interval.first) {
      merged_blocked_coord_interval_list.push_back(blocked_coord_interval);
    } else {
      merged_blocked_coord_interval_list.back().second = std::max(merged_blocked_coord_interval_list.back().second, blocked_coord_interval.second);
    }
  }

  int32_t begin_coord = mi_layer.get_is_horizontal() ? fill_rect.get_ll_x() : fill_rect.get_ll_y();
  int32_t end_coord = mi_layer.get_is_horizontal() ? fill_rect.get_ur_x() : fill_rect.get_ur_y();
  std::vector<MIRect> fill_rect_list;
  for (const std::pair<int32_t, int32_t>& blocked_coord_interval : merged_blocked_coord_interval_list) {
    if (begin_coord < blocked_coord_interval.first) {
      if (mi_layer.get_is_horizontal()) {
        fill_rect_list.emplace_back(begin_coord, fill_rect.get_ll_y(), blocked_coord_interval.first, fill_rect.get_ur_y());
      } else {
        fill_rect_list.emplace_back(fill_rect.get_ll_x(), begin_coord, fill_rect.get_ur_x(), blocked_coord_interval.first);
      }
    }
    begin_coord = std::max(begin_coord, blocked_coord_interval.second);
  }
  if (begin_coord < end_coord) {
    if (mi_layer.get_is_horizontal()) {
      fill_rect_list.emplace_back(begin_coord, fill_rect.get_ll_y(), end_coord, fill_rect.get_ur_y());
    } else {
      fill_rect_list.emplace_back(fill_rect.get_ll_x(), begin_coord, fill_rect.get_ur_x(), end_coord);
    }
  }

  std::vector<MIRect> legal_fill_rect_list;
  for (const MIRect& split_fill_rect : fill_rect_list) {
    if (std::max(split_fill_rect.get_width(), split_fill_rect.get_height()) >= mi_layer.get_min_wire_length()) {
      legal_fill_rect_list.push_back(split_fill_rect);
    }
  }
  return legal_fill_rect_list;
}

std::vector<std::pair<int32_t, int32_t>> MetalInserter::getBlockedCoordIntervalList(ecc::geometry::GeometryStore& geometry_store,
                                                                                      MILayer& mi_layer, const MIRect& fill_rect)
{
  MIRect query_rect = fill_rect.get_expand_rect(mi_layer.get_max_spacing());
  std::vector<ecc::geometry::ShapeId> shape_id_list =
      geometry_store.query_intersect(static_cast<ecc::geometry::LayerId>(mi_layer.get_geometry_layer_idx()),
                                     ecc::geometry::Rect32{query_rect.get_ll_x(), query_rect.get_ll_y(), query_rect.get_ur_x(), query_rect.get_ur_y()});
  std::vector<std::pair<int32_t, int32_t>> blocked_coord_interval_list;
  for (ecc::geometry::ShapeId shape_id : shape_id_list) {
    if (isIgnoredShape(geometry_store.owner_of(shape_id).type)) {
      continue;
    }
    const ecc::geometry::ShapeRecord* shape_record = geometry_store.find_shape(shape_id);
    if (shape_record == nullptr) {
      continue;
    }
    MIRect occupied_rect(shape_record->bbox.lx, shape_record->bbox.ly, shape_record->bbox.hx, shape_record->bbox.hy);
    int32_t required_spacing = getRequiredSpacing(mi_layer, fill_rect, occupied_rect);
    if (mi_layer.get_is_horizontal()) {
      if (occupied_rect.get_ur_y() + required_spacing <= fill_rect.get_ll_y()
          || fill_rect.get_ur_y() + required_spacing <= occupied_rect.get_ll_y()) {
        continue;
      }
      int32_t begin_coord = std::max(fill_rect.get_ll_x(), occupied_rect.get_ll_x() - required_spacing);
      int32_t end_coord = std::min(fill_rect.get_ur_x(), occupied_rect.get_ur_x() + required_spacing);
      if (begin_coord < end_coord) {
        blocked_coord_interval_list.emplace_back(begin_coord, end_coord);
      }
    } else {
      if (occupied_rect.get_ur_x() + required_spacing <= fill_rect.get_ll_x()
          || fill_rect.get_ur_x() + required_spacing <= occupied_rect.get_ll_x()) {
        continue;
      }
      int32_t begin_coord = std::max(fill_rect.get_ll_y(), occupied_rect.get_ll_y() - required_spacing);
      int32_t end_coord = std::min(fill_rect.get_ur_y(), occupied_rect.get_ur_y() + required_spacing);
      if (begin_coord < end_coord) {
        blocked_coord_interval_list.emplace_back(begin_coord, end_coord);
      }
    }
  }
  return blocked_coord_interval_list;
}

MIRect MetalInserter::getFillRect(MIModel& mi_model, MILayer& mi_layer, const MIRect& density_window_rect, int32_t track_coord)
{
  int32_t lower_half_width = mi_layer.get_wire_width() / 2;
  int32_t upper_half_width = mi_layer.get_wire_width() - lower_half_width;
  MIRect fill_rect;
  if (mi_layer.get_is_horizontal()) {
    fill_rect = MIRect(getAlignUp(density_window_rect.get_ll_x(), mi_model.get_manufacture_grid()), track_coord - lower_half_width,
                       getAlignDown(density_window_rect.get_ur_x(), mi_model.get_manufacture_grid()), track_coord + upper_half_width);
  } else {
    fill_rect = MIRect(track_coord - lower_half_width, getAlignUp(density_window_rect.get_ll_y(), mi_model.get_manufacture_grid()),
                       track_coord + upper_half_width, getAlignDown(density_window_rect.get_ur_y(), mi_model.get_manufacture_grid()));
  }
  if (!fill_rect.is_valid() || std::max(fill_rect.get_width(), fill_rect.get_height()) < mi_layer.get_min_wire_length()) {
    return MIRect();
  }
  return fill_rect;
}

int32_t MetalInserter::getAlignUp(int32_t coordinate, int32_t grid)
{
  if (grid <= 1) {
    return coordinate;
  }
  return getCeilDiv(coordinate, grid) * grid;
}

int32_t MetalInserter::getAlignDown(int32_t coordinate, int32_t grid)
{
  if (grid <= 1) {
    return coordinate;
  }
  return coordinate / grid * grid;
}

bool MetalInserter::isLegalFillRect(ecc::geometry::GeometryStore& geometry_store, MILayer& mi_layer, const MIRect& fill_rect)
{
  MIRect query_rect = fill_rect.get_expand_rect(mi_layer.get_max_spacing());
  std::vector<ecc::geometry::ShapeId> shape_id_list =
      geometry_store.query_intersect(static_cast<ecc::geometry::LayerId>(mi_layer.get_geometry_layer_idx()),
                                     ecc::geometry::Rect32{query_rect.get_ll_x(), query_rect.get_ll_y(), query_rect.get_ur_x(), query_rect.get_ur_y()});
  for (ecc::geometry::ShapeId shape_id : shape_id_list) {
    if (isIgnoredShape(geometry_store.owner_of(shape_id).type)) {
      continue;
    }
    const ecc::geometry::ShapeRecord* shape_record = geometry_store.find_shape(shape_id);
    if (shape_record == nullptr) {
      continue;
    }
    MIRect occupied_rect(shape_record->bbox.lx, shape_record->bbox.ly, shape_record->bbox.hx, shape_record->bbox.hy);
    if (fill_rect.is_intersect(occupied_rect)) {
      return false;
    }
    int32_t required_spacing = getRequiredSpacing(mi_layer, fill_rect, occupied_rect);
    if (getRectDistance(fill_rect, occupied_rect) < static_cast<double>(required_spacing)) {
      return false;
    }
  }
  return true;
}

bool MetalInserter::isIgnoredShape(ecc::geometry::OwnerType owner_type)
{
  return owner_type == ecc::geometry::OwnerType::kDie || owner_type == ecc::geometry::OwnerType::kCore
         || owner_type == ecc::geometry::OwnerType::kRow || owner_type == ecc::geometry::OwnerType::kTrackGrid
         || owner_type == ecc::geometry::OwnerType::kGCellGrid;
}

double MetalInserter::getRectDistance(const MIRect& first_rect, const MIRect& second_rect)
{
  int32_t x_spacing = std::max(second_rect.get_ll_x() - first_rect.get_ur_x(), first_rect.get_ll_x() - second_rect.get_ur_x());
  int32_t y_spacing = std::max(second_rect.get_ll_y() - first_rect.get_ur_y(), first_rect.get_ll_y() - second_rect.get_ur_y());
  if (x_spacing > 0 && y_spacing > 0) {
    return std::sqrt(static_cast<double>(x_spacing) * static_cast<double>(x_spacing)
                     + static_cast<double>(y_spacing) * static_cast<double>(y_spacing));
  }
  return static_cast<double>(std::max(std::max(x_spacing, y_spacing), 0));
}

int32_t MetalInserter::getRequiredSpacing(MILayer& mi_layer, const MIRect& first_rect, const MIRect& second_rect)
{
  idb::IdbLayerRouting* idb_routing_layer = getRoutingLayer(mi_layer.get_layer_name());
  int32_t wire_width = std::max(std::min(first_rect.get_width(), first_rect.get_height()),
                                std::min(second_rect.get_width(), second_rect.get_height()));
  int32_t default_spacing = getDefaultSpacing(idb_routing_layer, wire_width);
  int32_t prl_spacing = getPRLSpacing(idb_routing_layer, wire_width, getParallelLength(first_rect, second_rect));
  return std::max(default_spacing, prl_spacing);
}

idb::IdbLayerRouting* MetalInserter::getRoutingLayer(const std::string& layer_name)
{
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_layers() == nullptr) {
    ZHLOG.error(Loc::current(), "The iDB layer list is null!");
  }
  idb::IdbLayer* idb_layer = idb_layout->get_layers()->find_layer(layer_name);
  idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
  if (idb_routing_layer == nullptr) {
    ZHLOG.error(Loc::current(), "The routing layer is null: ", layer_name);
  }
  return idb_routing_layer;
}

int32_t MetalInserter::getDefaultSpacing(idb::IdbLayerRouting* idb_routing_layer, int32_t wire_width)
{
  int32_t default_spacing = 0;
  idb::IdbLayerSpacingList* spacing_list = idb_routing_layer->get_spacing_list();
  if (spacing_list == nullptr) {
    return default_spacing;
  }
  for (idb::IdbLayerSpacing* spacing : spacing_list->get_spacing_list()) {
    if (spacing == nullptr) {
      continue;
    }
    if (spacing->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingDefault) {
      default_spacing = std::max(default_spacing, spacing->get_min_spacing());
    } else if (spacing->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingRange
               && spacing->get_min_width() <= wire_width && wire_width <= spacing->get_max_width()) {
      default_spacing = std::max(default_spacing, spacing->get_min_spacing());
    }
  }
  return default_spacing;
}

int32_t MetalInserter::getPRLSpacing(idb::IdbLayerRouting* idb_routing_layer, int32_t wire_width, int32_t parallel_length)
{
  std::shared_ptr<idb::IdbLayerSpacingTable> spacing_table = idb_routing_layer->get_spacing_table();
  if (spacing_table == nullptr || !spacing_table->is_parallel() || spacing_table->get_parallel() == nullptr) {
    return 0;
  }
  return spacing_table->get_parallel_spacing(wire_width, std::max(parallel_length, 0));
}

int32_t MetalInserter::getParallelLength(const MIRect& first_rect, const MIRect& second_rect)
{
  int32_t x_parallel_length = std::min(first_rect.get_ur_x(), second_rect.get_ur_x()) - std::max(first_rect.get_ll_x(), second_rect.get_ll_x());
  int32_t y_parallel_length = std::min(first_rect.get_ur_y(), second_rect.get_ur_y()) - std::max(first_rect.get_ll_y(), second_rect.get_ll_y());
  return std::max(x_parallel_length, y_parallel_length);
}

bool MetalInserter::isDensityLegal(MIModel& mi_model, MILayer& mi_layer, const MIRect& fill_rect)
{
  for (int32_t density_window_idx : getAffectedDensityWindowIdxList(mi_model, mi_layer, fill_rect)) {
    MIDensityWindow& density_window = mi_layer.get_density_window_list()[density_window_idx];
    double added_metal_area = fill_rect.get_intersect_rect(density_window.get_rect()).get_area();
    if ((density_window.get_metal_area() + added_metal_area) / density_window.get_rect().get_area()
        > mi_model.get_mi_com_param().get_max_density()) {
      return false;
    }
  }
  return true;
}

std::vector<int32_t> MetalInserter::getAffectedDensityWindowIdxList(MIModel& mi_model, MILayer& mi_layer, const MIRect& rect)
{
  int32_t density_window_size = static_cast<int32_t>(
      std::llround(mi_model.get_mi_com_param().get_density_window_size_micron() * mi_model.get_micron_dbu()));
  int32_t density_window_step = static_cast<int32_t>(
      std::llround(mi_model.get_mi_com_param().get_density_window_step_micron() * mi_model.get_micron_dbu()));
  const MIRect& die = mi_model.get_die();
  int32_t x_delta = rect.get_ll_x() - die.get_ll_x() - density_window_size;
  int32_t y_delta = rect.get_ll_y() - die.get_ll_y() - density_window_size;
  int32_t begin_x_idx = x_delta < 0 ? 0 : x_delta / density_window_step + 1;
  int32_t begin_y_idx = y_delta < 0 ? 0 : y_delta / density_window_step + 1;
  int32_t end_x_idx = (rect.get_ur_x() - die.get_ll_x() - 1) / density_window_step;
  int32_t end_y_idx = (rect.get_ur_y() - die.get_ll_y() - 1) / density_window_step;
  begin_x_idx = std::max(begin_x_idx, 0);
  begin_y_idx = std::max(begin_y_idx, 0);
  end_x_idx = std::min(end_x_idx, mi_layer.get_density_window_x_num() - 1);
  end_y_idx = std::min(end_y_idx, mi_layer.get_density_window_y_num() - 1);

  std::vector<int32_t> density_window_idx_list;
  for (int32_t y_idx = begin_y_idx; y_idx <= end_y_idx; ++y_idx) {
    for (int32_t x_idx = begin_x_idx; x_idx <= end_x_idx; ++x_idx) {
      density_window_idx_list.push_back(mi_layer.get_density_window_idx(x_idx, y_idx));
    }
  }
  return density_window_idx_list;
}

void MetalInserter::addFillRect(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store, MILayer& mi_layer,
                                const MIRect& fill_rect)
{
  ecc::geometry::OwnerRef owner;
  owner.type = ecc::geometry::OwnerType::kFill;
  owner.owner_id = static_cast<ecc::geometry::OwnerId>(mi_model.get_inserted_metal_num());
  geometry_store.add_rect(static_cast<ecc::geometry::LayerId>(mi_layer.get_geometry_layer_idx()),
                          ecc::geometry::Rect32{fill_rect.get_ll_x(), fill_rect.get_ll_y(), fill_rect.get_ur_x(), fill_rect.get_ur_y()}, owner);
  mi_layer.get_fill_rect_list().push_back(fill_rect);
  mi_layer.add_inserted_metal_num();
  mi_model.add_inserted_metal_num();
  updateDensityWindowList(mi_model, mi_layer, fill_rect);
}

void MetalInserter::updateDensityWindowList(MIModel& mi_model, MILayer& mi_layer, const MIRect& fill_rect)
{
  for (int32_t density_window_idx : getAffectedDensityWindowIdxList(mi_model, mi_layer, fill_rect)) {
    MIDensityWindow& density_window = mi_layer.get_density_window_list()[density_window_idx];
    density_window.add_metal_area(fill_rect.get_intersect_rect(density_window.get_rect()).get_area());
  }
}

#endif

#if 1  // 输出

void MetalInserter::writeMetalFill(MIModel& mi_model)
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_design == nullptr || idb_design->get_fill_list() == nullptr || idb_layout == nullptr || idb_layout->get_layers() == nullptr) {
    ZHLOG.error(Loc::current(), "The iDB metal fill data is null!");
  }

  for (MILayer& mi_layer : mi_model.get_mi_layer_list()) {
    if (mi_layer.get_fill_rect_list().empty()) {
      continue;
    }
    idb::IdbLayer* idb_layer = idb_layout->get_layers()->find_layer(mi_layer.get_layer_name());
    idb::IdbFillLayer* idb_fill_layer = idb_design->get_fill_list()->add_fill_layer(idb_layer);
    for (const MIRect& fill_rect : mi_layer.get_fill_rect_list()) {
      idb_fill_layer->add_rect(fill_rect.get_ll_x(), fill_rect.get_ll_y(), fill_rect.get_ur_x(), fill_rect.get_ur_y());
    }
  }
}

void MetalInserter::printResult(MIModel& mi_model)
{
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(0), "ZH insertMetal");
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "density_window_size_micron: ",
             mi_model.get_mi_com_param().get_density_window_size_micron());
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "density_window_step_micron: ",
             mi_model.get_mi_com_param().get_density_window_step_micron());
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "min_density: ", mi_model.get_mi_com_param().get_min_density());
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "max_density: ", mi_model.get_mi_com_param().get_max_density());
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "min_fill_layer: ", mi_model.get_mi_com_param().get_min_fill_layer());
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "max_fill_layer: ", mi_model.get_mi_com_param().get_max_fill_layer());
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), "inserted_metal_num: ", mi_model.get_inserted_metal_num());
  for (MILayer& mi_layer : mi_model.get_mi_layer_list()) {
    printLayerResult(mi_model, mi_layer);
  }
}

void MetalInserter::printLayerResult(MIModel& mi_model, MILayer& mi_layer)
{
  double min_density = 0.0;
  double max_density = 0.0;
  int32_t under_density_window_num = 0;
  std::vector<MIDensityWindow>& density_window_list = mi_layer.get_density_window_list();
  if (!density_window_list.empty()) {
    min_density = density_window_list.front().get_density();
  }
  for (const MIDensityWindow& density_window : density_window_list) {
    min_density = std::min(min_density, density_window.get_density());
    max_density = std::max(max_density, density_window.get_density());
    if (density_window.get_density() < mi_model.get_mi_com_param().get_min_density()) {
      ++under_density_window_num;
    }
  }
  ZHLOG.info(Loc::current(), ZHUTIL.getSpaceByTabNum(1), mi_layer.get_layer_name(), " inserted_metal_num: ",
             mi_layer.get_inserted_metal_num(), " min_density: ", min_density, " max_density: ", max_density,
             " under_density_window_num: ", under_density_window_num);
}

#endif

MetalInserter* MetalInserter::_mi_instance = nullptr;

}  // namespace izh
