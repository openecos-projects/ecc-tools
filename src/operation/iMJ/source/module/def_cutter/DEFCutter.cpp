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
#include "DEFCutter.hpp"

#include "DataManager.hpp"
#include "MJInterface.hpp"
#include "Utility.hpp"
#include "idm.h"

namespace imj {

void DEFCutter::initInst()
{
  if (_dc_instance == nullptr) {
    _dc_instance = new DEFCutter();
  }
}

DEFCutter& DEFCutter::getInst()
{
  if (_dc_instance == nullptr) {
    MJLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_dc_instance;
}

void DEFCutter::destroyInst()
{
  delete _dc_instance;
  _dc_instance = nullptr;
}

void DEFCutter::cut(std::map<std::string, std::any>& config_map)
{
  Monitor monitor;
  MJLOG.info(Loc::current(), "Starting...");
  DCModel dc_model = initDCModel(config_map);
  cutGridList(dc_model);
  selectObjectList(dc_model.get_instance_list(), dc_model.get_die());
  selectObjectList(dc_model.get_io_pin_list(), dc_model.get_die());
  cutNetList(dc_model);
  buildIOPinList(dc_model);
  cutObjectList(dc_model.get_blockage_list(), dc_model.get_die());
  cutObjectList(dc_model.get_fill_list(), dc_model.get_die());
  cutObjectList(dc_model.get_region_list(), dc_model.get_die());
  cutObjectList(dc_model.get_slot_list(), dc_model.get_die());
  placeIOPinList(dc_model);
  updateDesign(dc_model);
  writeReport(dc_model);
  MJLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

DCModel DEFCutter::initDCModel(std::map<std::string, std::any>& config_map)
{
  DCModel dc_model;
  MJI.wrapCutDesign(dc_model);
  for (const auto& [key, value] : config_map) {
    if (key != "-rect") {
      MJLOG.error(Loc::current(), "Unsupported cut_def option: ", key);
    }
  }
  std::istringstream stream(MJUTIL.getConfigValue<std::string>(config_map, "-rect", ""));
  std::array<int32_t, 4> coord;
  for (int32_t idx = 0; idx < 4; idx++) {
    double value = 0.0;
    if (!(stream >> value) || !std::isfinite(value)) {
      MJLOG.error(Loc::current(), "cut_def -rect requires four finite micron coordinates.");
    }
    double dbu = std::round(value * dc_model.get_micron_dbu());
    if (!std::isfinite(dbu) || dbu < INT32_MIN || dbu > INT32_MAX) {
      MJLOG.error(Loc::current(), "cut_def coordinate is outside the DBU range.");
    }
    coord[idx] = static_cast<int32_t>(dbu);
  }
  std::string extra;
  if (stream >> extra) {
    MJLOG.error(Loc::current(), "cut_def -rect requires exactly four coordinates.");
  }
  DCRect rect(coord[0], coord[1], coord[2], coord[3]);
  if (!rect.isValid() || !dc_model.get_input_die().contains(rect)) {
    MJLOG.error(Loc::current(), "cut_def rectangle must have positive area and lie inside the input die.");
  }
  dc_model.set_die(rect);
  return dc_model;
}

void DEFCutter::cutGridList(DCModel& dc_model)
{
  for (DCGrid& row : dc_model.get_row_list()) {
    cutGrid(row, dc_model.get_die(), true);
  }
  for (DCGrid& track : dc_model.get_track_list()) {
    cutGrid(track, dc_model.get_die(), false);
  }
  for (DCGrid& gcell : dc_model.get_gcell_list()) {
    cutGrid(gcell, dc_model.get_die(), false);
  }
}

void DEFCutter::cutGrid(DCGrid& grid, DCRect& boundary, bool is_row)
{
  std::array<int32_t, 2> low = {boundary.get_ll_x(), boundary.get_ll_y()};
  std::array<int32_t, 2> high = {boundary.get_ur_x(), boundary.get_ur_y()};
  for (int32_t axis = 0; axis < 2; axis++) {
    if (!is_row && axis != grid.get_axis()) {
      continue;
    }
    int32_t origin = grid.get_origin()[axis];
    int32_t count = grid.get_count()[axis];
    int32_t step = grid.get_step()[axis];
    int32_t extent = grid.get_extent()[axis];
    if (count <= 0 || step < 0 || (count > 1 && step == 0)) {
      MJLOG.error(Loc::current(), "Invalid row/track grid in the input DEF.");
    }
    if (count == 1) {
      if (origin < low[axis] || static_cast<double>(origin) + extent > high[axis]) {
        grid.get_count()[axis] = 0;
      }
      continue;
    }
    double begin = std::max(0.0, std::ceil((static_cast<double>(low[axis]) - origin) / step));
    double end = std::min(static_cast<double>(count - 1), std::floor((static_cast<double>(high[axis]) - extent - origin) / step));
    if (begin > end) {
      grid.get_count()[axis] = 0;
    } else {
      grid.get_origin()[axis] = static_cast<int32_t>(origin + begin * step);
      grid.get_count()[axis] = static_cast<int32_t>(end - begin + 1);
    }
  }
}

void DEFCutter::selectObjectList(std::vector<DCObject>& object_list, DCRect& boundary)
{
  for (DCObject& object : object_list) {
    bool retained = !object.get_rect_list().empty();
    for (DCRect& rect : object.get_rect_list()) {
      retained = retained && boundary.contains(rect);
    }
    object.set_retained(retained);
  }
}

void DEFCutter::cutNetList(DCModel& dc_model)
{
  for (DCNet& net : dc_model.get_net_list()) {
    for (DCConnection& connection : net.get_connection_list()) {
      std::vector<DCObject>& objects = connection.get_is_io() ? dc_model.get_io_pin_list() : dc_model.get_instance_list();
      connection.set_retained(objects[connection.get_object_idx()].get_retained());
      if (connection.get_retained()) {
        net.set_retained(true);
      } else {
        net.set_removed_pin_num(net.get_removed_pin_num() + 1);
        for (DCLayerRect& shape : connection.get_shape_list()) {
          DCRect rect = shape.get_rect().intersect(dc_model.get_die());
          if (rect.isValid()) {
            net.get_io_shape_list().emplace_back(shape.get_layer_name(), rect);
          }
        }
      }
    }
    std::vector<DCRoute> via_metal_list;
    for (DCRoute& route : net.get_route_list()) {
      if (route.get_via_idx() >= 0) {
        route.set_retained(dc_model.get_die().contains(route.get_rect()));
        if (!route.get_retained()) {
          // A cut via is no longer a legal via. Keep its in-die routing metal as IO access shapes.
          for (DCLayerRect& shape : route.get_via_shape_list()) {
            DCRect rect = shape.get_rect().intersect(dc_model.get_die());
            if (!rect.isValid()) {
              continue;
            }
            DCRoute metal;
            metal.set_wire_idx(route.get_wire_idx());
            metal.set_segment_idx(route.get_segment_idx());
            metal.set_layer_name(shape.get_layer_name());
            metal.set_rect(rect);
            metal.set_retained(true);
            via_metal_list.push_back(metal);
            net.get_io_shape_list().emplace_back(shape.get_layer_name(), rect);
            net.set_retained(true);
          }
        }
      } else {
        DCRect input_rect = route.get_rect();
        route.set_rect(route.get_rect().intersect(dc_model.get_die()));
        route.set_retained(route.get_rect().isValid());
        if (route.get_retained()) {
          buildBoundaryShapeList(net, route, input_rect, dc_model.get_die());
        }
      }
      net.set_retained(net.get_retained() || route.get_retained());
    }
    net.get_route_list().insert(net.get_route_list().end(), via_metal_list.begin(), via_metal_list.end());
  }
}

void DEFCutter::buildBoundaryShapeList(DCNet& net, DCRoute& route, DCRect& input_rect, DCRect& die)
{
  DCRect& rect = route.get_rect();
  int32_t depth = std::min(rect.get_ur_x() - rect.get_ll_x(), rect.get_ur_y() - rect.get_ll_y());
  if (input_rect.get_ll_x() < die.get_ll_x()) {
    net.get_io_shape_list().emplace_back(route.get_layer_name(), DCRect(rect.get_ll_x(), rect.get_ll_y(), rect.get_ll_x() + depth, rect.get_ur_y()));
  }
  if (input_rect.get_ur_x() > die.get_ur_x()) {
    net.get_io_shape_list().emplace_back(route.get_layer_name(), DCRect(rect.get_ur_x() - depth, rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y()));
  }
  if (input_rect.get_ll_y() < die.get_ll_y()) {
    net.get_io_shape_list().emplace_back(route.get_layer_name(), DCRect(rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ll_y() + depth));
  }
  if (input_rect.get_ur_y() > die.get_ur_y()) {
    net.get_io_shape_list().emplace_back(route.get_layer_name(), DCRect(rect.get_ll_x(), rect.get_ur_y() - depth, rect.get_ur_x(), rect.get_ur_y()));
  }
}

void DEFCutter::buildIOPinList(DCModel& dc_model)
{
  std::set<std::string> pin_name_set;
  for (DCObject& pin : dc_model.get_io_pin_list()) {
    if (pin.get_retained()) {
      pin_name_set.insert(pin.get_name());
    }
  }
  std::set<std::string> special_net_name_set;
  std::map<std::string, std::vector<int32_t>> net_idx_map;
  for (int32_t idx = 0; idx < static_cast<int32_t>(dc_model.get_net_list().size()); idx++) {
    DCNet& net = dc_model.get_net_list()[idx];
    if (net.get_is_special()) {
      special_net_name_set.insert(net.get_name());
    }
    if (net.get_retained()) {
      net_idx_map[net.get_name()].push_back(idx);
    }
  }
  for (auto& [net_name, net_idx_list] : net_idx_map) {
    if (special_net_name_set.contains(net_name)) {
      continue;
    }
    DCIOPin pin;
    pin.set_net_idx(net_idx_list.front());
    bool needs_io = false;
    for (int32_t net_idx : net_idx_list) {
      DCNet& net = dc_model.get_net_list()[net_idx];
      needs_io = needs_io || net.get_removed_pin_num() > 0 || !net.get_io_shape_list().empty();
      if (net.get_is_pg()) {
        pin.set_net_idx(net_idx);
      }
    }
    if (!needs_io) {
      continue;
    }
    pin.set_layer_name(getIOPinLayerName(dc_model, net_idx_list));
    std::set<std::tuple<std::string, int32_t, int32_t, int32_t, int32_t>> shape_set;
    for (int32_t net_idx : net_idx_list) {
      for (DCLayerRect& shape : dc_model.get_net_list()[net_idx].get_io_shape_list()) {
        DCRect& rect = shape.get_rect();
        if (shape.get_layer_name() == pin.get_layer_name() && isBoundaryRect(rect, dc_model.get_die())
            && shape_set.emplace(shape.get_layer_name(), rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y()).second) {
          pin.get_shape_list().push_back(shape);
        }
      }
    }
    pin.set_type(getIOPinType(dc_model, net_idx_list));
    pin.set_target_coord(getIOPinTargetCoord(dc_model, net_idx_list));
    // Keep an existing top-level port instead of adding a second logical driver.
    for (int32_t net_idx : net_idx_list) {
      for (DCConnection& connection : dc_model.get_net_list()[net_idx].get_connection_list()) {
        if (connection.get_is_io() && connection.get_retained() && pin.get_source_idx() < 0) {
          pin.set_source_idx(connection.get_object_idx());
          pin.set_name(connection.get_pin_name());
          pin.set_type(connection.get_pin_type() == DCPinType::kNone ? DCPinType::kInout : connection.get_pin_type());
          for (DCLayerRect& shape : connection.get_shape_list()) {
            DCRect& rect = shape.get_rect();
            if (shape_set.emplace(shape.get_layer_name(), rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y()).second) {
              pin.get_shape_list().push_back(shape);
            }
          }
        }
      }
    }
    if (pin.get_source_idx() < 0) {
      int32_t name_idx = 0;
      std::string pin_name = net_name;
      while (pin_name_set.contains(pin_name) || (pin_name != net_name && net_idx_map.contains(pin_name))) {
        pin_name = net_name + "__cut_" + std::to_string(name_idx++);
      }
      pin_name_set.insert(pin_name);
      pin.set_name(pin_name);
    }
    for (int32_t net_idx : net_idx_list) {
      dc_model.get_net_list()[net_idx].set_cut_pin_idx(static_cast<int32_t>(dc_model.get_cut_pin_list().size()));
    }
    dc_model.get_cut_pin_list().push_back(pin);
  }
}

std::string DEFCutter::getIOPinLayerName(DCModel& dc_model, std::vector<int32_t>& net_idx_list)
{
  std::map<int32_t, std::string> layer_order_map;
  for (idb::IdbLayer* layer : dmInst->get_idb_layout()->get_layers()->get_routing_layers()) {
    layer_order_map[layer->get_order()] = layer->get_name();
  }
  std::vector<std::string> layer_name_list;
  std::map<std::string, int32_t> layer_idx_map;
  for (auto& [order, name] : layer_order_map) {
    layer_idx_map[name] = static_cast<int32_t>(layer_name_list.size());
    layer_name_list.push_back(name);
  }
  std::set<std::tuple<bool, int32_t, std::string>> pin_set;
  double layer_idx_sum = 0.0;
  int32_t pin_num = 0;
  for (int32_t net_idx : net_idx_list) {
    DCNet& net = dc_model.get_net_list()[net_idx];
    for (DCConnection& connection : net.get_connection_list()) {
      if (!pin_set.emplace(connection.get_is_io(), connection.get_object_idx(), connection.get_pin_name()).second) {
        continue;
      }
      if (connection.get_layer_name_set().empty()) {
        MJLOG.error(Loc::current(), "Missing original routing layers for pin ", connection.get_pin_name(), " on cut net ", net.get_name());
      }
      double pin_layer_idx_sum = 0.0;
      for (const std::string& layer_name : connection.get_layer_name_set()) {
        pin_layer_idx_sum += layer_idx_map[layer_name];
      }
      layer_idx_sum += pin_layer_idx_sum / connection.get_layer_name_set().size();
      pin_num++;
    }
  }
  // A routing-only transit net has no terminal; use its boundary metal layers.
  if (pin_num == 0) {
    std::set<std::string> layer_name_set;
    for (int32_t net_idx : net_idx_list) {
      for (DCLayerRect& shape : dc_model.get_net_list()[net_idx].get_io_shape_list()) {
        if (isBoundaryRect(shape.get_rect(), dc_model.get_die())) {
          layer_name_set.insert(shape.get_layer_name());
        }
      }
    }
    for (const std::string& layer_name : layer_name_set) {
      layer_idx_sum += layer_idx_map[layer_name];
      pin_num++;
    }
  }
  if (pin_num == 0) {
    MJLOG.error(Loc::current(), "Missing routing layers for cut net ", dc_model.get_net_list()[net_idx_list.front()].get_name());
  }
  return layer_name_list[static_cast<int32_t>(std::round(layer_idx_sum / pin_num))];
}

bool DEFCutter::isBoundaryRect(const DCRect& rect, const DCRect& die)
{
  return die.contains(rect) && (rect.get_ll_x() == die.get_ll_x() || rect.get_ll_y() == die.get_ll_y()
                               || rect.get_ur_x() == die.get_ur_x() || rect.get_ur_y() == die.get_ur_y());
}

DCPinType DEFCutter::getIOPinType(DCModel& dc_model, std::vector<int32_t>& net_idx_list)
{
  bool internal_driver = false;
  bool external_driver = false;
  bool external_load = false;
  bool unresolved = false;
  for (int32_t net_idx : net_idx_list) {
    DCNet& net = dc_model.get_net_list()[net_idx];
    if (net.get_is_pg()) {
      return DCPinType::kInout;
    }
    for (DCConnection& connection : net.get_connection_list()) {
      if (connection.get_pin_type() == DCPinType::kNone || connection.get_pin_type() == DCPinType::kInout) {
        unresolved = true;
        continue;
      }
      bool is_driver = connection.get_is_io() ? connection.get_pin_type() == DCPinType::kInput : connection.get_pin_type() == DCPinType::kOutput;
      internal_driver = internal_driver || (connection.get_retained() && is_driver);
      external_driver = external_driver || (!connection.get_retained() && is_driver);
      external_load = external_load || (!connection.get_retained() && !is_driver);
    }
  }
  if (unresolved || (internal_driver && external_driver)) {
    return DCPinType::kInout;
  }
  if (external_driver) {
    return DCPinType::kInput;
  }
  if (internal_driver || external_load) {
    return DCPinType::kOutput;
  }
  return DCPinType::kInout;
}

std::array<int32_t, 2> DEFCutter::getIOPinTargetCoord(DCModel& dc_model, std::vector<int32_t>& net_idx_list)
{
  DCRect rect = dc_model.get_die();
  bool found = false;
  for (int32_t net_idx : net_idx_list) {
    for (DCConnection& connection : dc_model.get_net_list()[net_idx].get_connection_list()) {
      if (!found && connection.get_retained() && !connection.get_shape_list().empty()) {
        rect = connection.get_shape_list().front().get_rect();
        found = true;
      }
    }
  }
  return {static_cast<int32_t>((static_cast<double>(rect.get_ll_x()) + rect.get_ur_x()) / 2),
          static_cast<int32_t>((static_cast<double>(rect.get_ll_y()) + rect.get_ur_y()) / 2)};
}

void DEFCutter::cutObjectList(std::vector<DCObject>& object_list, DCRect& boundary)
{
  for (DCObject& object : object_list) {
    std::vector<DCRect> rect_list;
    for (DCRect& rect : object.get_rect_list()) {
      // Via fills are indivisible; part_idx identifies a placement of a via fill.
      DCRect clipped = object.get_part_idx() < 0 ? rect.intersect(boundary) : boundary.contains(rect) ? rect : DCRect();
      if (clipped.isValid()) {
        rect_list.push_back(clipped);
      }
    }
    object.set_rect_list(rect_list);
    object.set_retained(!rect_list.empty());
  }
}

void DEFCutter::placeIOPinList(DCModel& dc_model)
{
  int32_t unplaced_pin_num = 0;
  for (DCIOPin& pin : dc_model.get_cut_pin_list()) {
    unplaced_pin_num += pin.get_shape_list().empty();
  }
  if (unplaced_pin_num == 0) {
    return;
  }
  DCIOModel io_model;
  io_model.set_slot_list(buildIOSlotList(dc_model));
  buildIOSlotRTree(io_model);
  std::vector<DCIOSlot>& slot_list = io_model.get_slot_list();
  std::map<std::string, std::vector<DCRect>> obstacle_map = buildIOObstacleMap(dc_model);
  for (auto& [layer_name, rect_list] : obstacle_map) {
    for (DCRect& rect : rect_list) {
      DCLayerRect obstacle(layer_name, rect);
      blockIOSlotList(io_model, obstacle);
    }
  }
  for (DCIOPin& pin : dc_model.get_cut_pin_list()) {
    if (!pin.get_shape_list().empty()) {
      continue;
    }
    int32_t best_idx = -1;
    double best_distance = std::numeric_limits<double>::max();
    for (int32_t idx = 0; idx < static_cast<int32_t>(slot_list.size()); idx++) {
      DCIOSlot& slot = slot_list[idx];
      if (!slot.get_available() || pin.get_layer_name() != slot.get_shape().get_layer_name()) {
        continue;
      }
      double distance = getIOPinDistance(pin, slot.get_shape().get_rect());
      if (distance < best_distance) {
        best_distance = distance;
        best_idx = idx;
      }
    }
    if (best_idx < 0) {
      MJLOG.error(Loc::current(), "No legal boundary slot for cut IO ", pin.get_name(), " on net ", dc_model.get_net_list()[pin.get_net_idx()].get_name(),
                  "; selected routing layer: ", pin.get_layer_name(), "; remaining IO pins=", unplaced_pin_num);
    }
    DCLayerRect shape = slot_list[best_idx].get_shape();
    pin.get_shape_list().push_back(shape);
    unplaced_pin_num--;
    blockIOSlotList(io_model, shape);
  }
}

std::vector<DCIOSlot> DEFCutter::buildIOSlotList(DCModel& dc_model)
{
  idb::IdbLayout* layout = dmInst->get_idb_layout();
  std::vector<DCIOSlot> slot_list;
  for (idb::IdbLayer* base : layout->get_layers()->get_routing_layers()) {
    idb::IdbLayerRouting* layer = dynamic_cast<idb::IdbLayerRouting*>(base);
    if (!layer->is_horizontal() && !layer->is_vertical()) {
      continue;
    }
    int32_t axis = layer->is_horizontal() ? 1 : 0;
    bool has_track = false;
    for (DCGrid& grid : dc_model.get_track_list()) {
      idb::IdbTrackGrid* track = layout->get_track_grid_list()->get_track_grid_list()[grid.get_source_idx()];
      if (grid.get_axis() != axis || std::find(track->get_layer_list().begin(), track->get_layer_list().end(), layer) == track->get_layer_list().end()) {
        continue;
      }
      has_track = true;
      buildIOEdgeSlotList(dc_model, layer, grid.get_origin()[axis], grid.get_step()[axis], grid.get_count()[axis], slot_list);
    }
    if (!has_track) {
      int32_t origin = std::max(layer->get_offset_prefer(), 0);
      int32_t pitch = layer->get_pitch_prefer();
      int32_t high = axis == 0 ? dc_model.get_die().get_ur_x() : dc_model.get_die().get_ur_y();
      if (pitch > 0 && high >= origin) {
        int32_t count = static_cast<int32_t>((static_cast<double>(high) - origin) / pitch) + 1;
        buildIOEdgeSlotList(dc_model, layer, origin, pitch, count, slot_list);
      }
    }
  }
  return slot_list;
}

void DEFCutter::buildIOEdgeSlotList(DCModel& dc_model, idb::IdbLayerRouting* layer, int32_t origin, int32_t pitch, int32_t count,
                                   std::vector<DCIOSlot>& slot_list)
{
  int32_t width = layer->get_min_width() > 0 ? layer->get_min_width() : layer->get_width();
  if (width <= 0 || pitch <= 0 || count <= 0) {
    return;
  }
  DCRect& die = dc_model.get_die();
  bool horizontal = layer->is_horizontal();
  int32_t low = horizontal ? die.get_ll_y() : die.get_ll_x();
  int32_t high = horizontal ? die.get_ur_y() : die.get_ur_x();
  int32_t lower_half = width / 2;
  int32_t upper_half = width - lower_half;
  int32_t begin = static_cast<int32_t>(std::max(0.0, std::ceil((static_cast<double>(low) + lower_half - origin) / pitch)));
  int32_t end = static_cast<int32_t>(std::min(static_cast<double>(count - 1), std::floor((static_cast<double>(high) - upper_half - origin) / pitch)));
  for (int32_t side = 0; side < 2; side++) {
    int32_t depth = getIOPinDepth(dc_model, layer, width, side == 0);
    int32_t span = horizontal ? die.get_ur_x() - die.get_ll_x() : die.get_ur_y() - die.get_ll_y();
    if (depth <= 0 || depth > span) {
      continue;
    }
    for (int32_t idx = begin; idx <= end; idx++) {
      int32_t coord = static_cast<int32_t>(origin + static_cast<double>(idx) * pitch);
      DCRect rect;
      if (horizontal) {
        int32_t x = side == 0 ? die.get_ll_x() : die.get_ur_x() - depth;
        rect = DCRect(x, coord - lower_half, x + depth, coord + upper_half);
      } else {
        int32_t y = side == 0 ? die.get_ll_y() : die.get_ur_y() - depth;
        rect = DCRect(coord - lower_half, y, coord + upper_half, y + depth);
      }
      DCIOSlot slot;
      slot.set_shape(DCLayerRect(layer->get_name(), rect));
      slot_list.push_back(slot);
    }
  }
}

int32_t DEFCutter::getIOPinDepth(DCModel& dc_model, idb::IdbLayerRouting* layer, int32_t width, bool low_side)
{
  DCRect& die = dc_model.get_die();
  int32_t boundary = layer->is_horizontal() ? (low_side ? die.get_ll_x() : die.get_ur_x()) : (low_side ? die.get_ll_y() : die.get_ur_y());
  int32_t half_width = (width + 1) / 2;
  int32_t min_depth = std::max(width, static_cast<int32_t>(std::ceil(layer->get_area() / static_cast<double>(width))));
  int32_t min_distance = std::max(half_width, min_depth - half_width);
  double target = static_cast<double>(boundary) + (low_side ? min_distance : -min_distance);
  int32_t best_order_distance = std::numeric_limits<int32_t>::max();
  int32_t depth = min_depth;
  idb::IdbLayout* layout = dmInst->get_idb_layout();
  for (DCGrid& grid : dc_model.get_track_list()) {
    int32_t axis = grid.get_axis();
    if (axis != (layer->is_horizontal() ? 0 : 1) || grid.get_count()[axis] <= 0 || grid.get_step()[axis] <= 0) {
      continue;
    }
    idb::IdbTrackGrid* track = layout->get_track_grid_list()->get_track_grid_list()[grid.get_source_idx()];
    for (idb::IdbLayer* base : track->get_layer_list()) {
      idb::IdbLayerRouting* access_layer = dynamic_cast<idb::IdbLayerRouting*>(base);
      if (access_layer == nullptr || access_layer->is_horizontal() == layer->is_horizontal()) {
        continue;
      }
      double idx = (target - grid.get_origin()[axis]) / grid.get_step()[axis];
      idx = low_side ? std::max(0.0, std::ceil(idx)) : std::min(static_cast<double>(grid.get_count()[axis] - 1), std::floor(idx));
      if (idx < 0 || idx >= grid.get_count()[axis]) {
        continue;
      }
      int32_t candidate = static_cast<int32_t>(std::abs(grid.get_origin()[axis] + idx * grid.get_step()[axis] - boundary)) + half_width;
      int32_t order_distance = std::abs(static_cast<int32_t>(access_layer->get_order()) - static_cast<int32_t>(layer->get_order()));
      if (order_distance < best_order_distance || (order_distance == best_order_distance && candidate < depth)) {
        best_order_distance = order_distance;
        depth = candidate;
      }
    }
  }
  int32_t grid = std::max(layout->get_munufacture_grid(), 1);
  return static_cast<int32_t>(std::ceil(depth / static_cast<double>(grid))) * grid;
}

void DEFCutter::buildIOSlotRTree(DCIOModel& io_model)
{
  std::map<std::string, std::vector<DCIOModel::SlotRTree::value_type>> layer_value_map;
  std::vector<DCIOSlot>& slot_list = io_model.get_slot_list();
  for (int32_t idx = 0; idx < static_cast<int32_t>(slot_list.size()); idx++) {
    DCLayerRect& shape = slot_list[idx].get_shape();
    DCRect& rect = shape.get_rect();
    layer_value_map[shape.get_layer_name()].emplace_back(
        BGRectInt(BGPointInt(rect.get_ll_x(), rect.get_ll_y()), BGPointInt(rect.get_ur_x(), rect.get_ur_y())), idx);
  }
  for (auto& [layer_name, value_list] : layer_value_map) {
    io_model.get_layer_slot_rtree_map().emplace(layer_name, DCIOModel::SlotRTree(value_list.begin(), value_list.end()));
    idb::IdbLayerRouting* layer = dynamic_cast<idb::IdbLayerRouting*>(dmInst->get_idb_layout()->get_layers()->find_layer(layer_name));
    io_model.get_layer_max_spacing_map()[layer_name] = getIOMaxSpacing(layer);
  }
}

int32_t DEFCutter::getIOMaxSpacing(idb::IdbLayerRouting* layer)
{
  int32_t max_spacing = 0;
  if (layer->get_spacing_table()->is_parallel()) {
    for (const std::vector<int32_t>& spacing_list : layer->get_spacing_table()->get_parallel()->get_spacing_table()) {
      for (int32_t spacing : spacing_list) {
        max_spacing = std::max(max_spacing, spacing);
      }
    }
  } else {
    for (idb::IdbLayerSpacing* spacing : layer->get_spacing_list()->get_spacing_list()) {
      max_spacing = std::max(max_spacing, spacing->get_min_spacing());
    }
  }
  return max_spacing;
}

std::map<std::string, std::vector<DCRect>> DEFCutter::buildIOObstacleMap(DCModel& dc_model)
{
  std::map<std::string, std::vector<DCRect>> obstacle_map;
  for (DCNet& net : dc_model.get_net_list()) {
    if (!net.get_retained()) {
      continue;
    }
    for (DCRoute& route : net.get_route_list()) {
      if (!route.get_retained()) {
        continue;
      }
      if (route.get_via_idx() < 0) {
        obstacle_map[route.get_layer_name()].push_back(route.get_rect());
      } else {
        for (DCLayerRect& shape : route.get_via_shape_list()) {
          obstacle_map[shape.get_layer_name()].push_back(shape.get_rect());
        }
      }
    }
  }
  for (DCIOPin& pin : dc_model.get_cut_pin_list()) {
    for (DCLayerRect& shape : pin.get_shape_list()) {
      obstacle_map[shape.get_layer_name()].push_back(shape.get_rect());
    }
  }
  idb::IdbDesign* design = dmInst->get_idb_design();
  for (DCObject& object : dc_model.get_instance_list()) {
    if (!object.get_retained()) {
      continue;
    }
    idb::IdbInstance* instance = design->get_instance_list()->get_instance_list()[object.get_source_idx()];
    for (idb::IdbLayerShape* shape : instance->get_obs_box_list()) {
      if (!shape->get_layer()->is_routing()) {
        continue;
      }
      for (idb::IdbRect* rect : shape->get_rect_list()) {
        obstacle_map[shape->get_layer()->get_name()].emplace_back(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
      }
    }
    for (idb::IdbPin* pin : instance->get_pin_list()->get_pin_list()) {
      for (DCLayerRect& shape : MJI.wrapCutPinShapeList(pin)) {
        obstacle_map[shape.get_layer_name()].push_back(shape.get_rect());
      }
    }
  }
  for (DCObject& object : dc_model.get_io_pin_list()) {
    if (object.get_retained()) {
      for (DCLayerRect& shape : MJI.wrapCutPinShapeList(design->get_io_pin_list()->get_pin_list()[object.get_source_idx()])) {
        obstacle_map[shape.get_layer_name()].push_back(shape.get_rect());
      }
    }
  }
  for (DCObject& object : dc_model.get_blockage_list()) {
    idb::IdbBlockage* blockage = design->get_blockage_list()->get_blockage_list()[object.get_source_idx()];
    if (object.get_retained() && blockage->is_routing_blockage()) {
      std::vector<DCRect>& rect_list = obstacle_map[dynamic_cast<idb::IdbRoutingBlockage*>(blockage)->get_layer_name()];
      rect_list.insert(rect_list.end(), object.get_rect_list().begin(), object.get_rect_list().end());
    }
  }
  for (DCObject& object : dc_model.get_fill_list()) {
    if (!object.get_retained()) {
      continue;
    }
    idb::IdbFill* fill = design->get_fill_list()->get_fill_list()[object.get_source_idx()];
    if (object.get_part_idx() < 0) {
      std::vector<DCRect>& rect_list = obstacle_map[fill->get_layer()->get_layer()->get_name()];
      rect_list.insert(rect_list.end(), object.get_rect_list().begin(), object.get_rect_list().end());
    } else {
      std::unique_ptr<idb::IdbVia> via(fill->get_via()->get_via()->clone());
      idb::IdbCoordinate<int32_t>* coord = fill->get_via()->get_coordinate_list()[object.get_part_idx()];
      via->set_coordinate(coord->get_x(), coord->get_y());
      for (idb::IdbLayerShape shape : {via->get_bottom_layer_shape(), via->get_top_layer_shape()}) {
        for (idb::IdbRect* rect : shape.get_rect_list()) {
          obstacle_map[shape.get_layer()->get_name()].emplace_back(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
        }
      }
    }
  }
  return obstacle_map;
}

void DEFCutter::blockIOSlotList(DCIOModel& io_model, DCLayerRect& obstacle)
{
  auto layer_iter = io_model.get_layer_slot_rtree_map().find(obstacle.get_layer_name());
  if (layer_iter == io_model.get_layer_slot_rtree_map().end()) {
    return;
  }
  DCRect& rect = obstacle.get_rect();
  int32_t spacing = io_model.get_layer_max_spacing_map()[obstacle.get_layer_name()];
  // Use the full table maximum only for the broad phase; keep exact width/PRL checks below.
  BGRectInt query_rect(
      BGPointInt(static_cast<int32_t>(std::max(static_cast<double>(INT32_MIN), static_cast<double>(rect.get_ll_x()) - spacing)),
                 static_cast<int32_t>(std::max(static_cast<double>(INT32_MIN), static_cast<double>(rect.get_ll_y()) - spacing))),
      BGPointInt(static_cast<int32_t>(std::min(static_cast<double>(INT32_MAX), static_cast<double>(rect.get_ur_x()) + spacing)),
                 static_cast<int32_t>(std::min(static_cast<double>(INT32_MAX), static_cast<double>(rect.get_ur_y()) + spacing))));
  DCIOModel::SlotRTree& slot_rtree = layer_iter->second;
  for (auto iter = slot_rtree.qbegin(bgi::intersects(query_rect)); iter != slot_rtree.qend(); ++iter) {
    DCIOSlot& slot = io_model.get_slot_list()[iter->second];
    if (slot.get_available() && !hasIOPinSpacing(slot.get_shape(), obstacle)) {
      slot.set_available(false);
    }
  }
}

bool DEFCutter::hasIOPinSpacing(DCLayerRect& first, DCLayerRect& second)
{
  if (first.get_layer_name() != second.get_layer_name()) {
    return true;
  }
  DCRect& first_rect = first.get_rect();
  DCRect& second_rect = second.get_rect();
  idb::IdbLayerRouting* layer = dynamic_cast<idb::IdbLayerRouting*>(dmInst->get_idb_layout()->get_layers()->find_layer(first.get_layer_name()));
  int32_t first_width = std::min(first_rect.get_ur_x() - first_rect.get_ll_x(), first_rect.get_ur_y() - first_rect.get_ll_y());
  int32_t second_width = std::min(second_rect.get_ur_x() - second_rect.get_ll_x(), second_rect.get_ur_y() - second_rect.get_ll_y());
  int32_t parallel_length = layer->is_horizontal() ? std::min(first_rect.get_ur_x(), second_rect.get_ur_x()) - std::max(first_rect.get_ll_x(), second_rect.get_ll_x())
                                                 : std::min(first_rect.get_ur_y(), second_rect.get_ur_y()) - std::max(first_rect.get_ll_y(), second_rect.get_ll_y());
  int32_t spacing = layer->get_spacing(std::max(first_width, second_width), std::max(parallel_length, 0));
  double x_gap = std::max(static_cast<double>(first_rect.get_ll_x()) - second_rect.get_ur_x(), static_cast<double>(second_rect.get_ll_x()) - first_rect.get_ur_x());
  double y_gap = std::max(static_cast<double>(first_rect.get_ll_y()) - second_rect.get_ur_y(), static_cast<double>(second_rect.get_ll_y()) - first_rect.get_ur_y());
  return !first_rect.intersect(second_rect).isValid() && (x_gap >= spacing || y_gap >= spacing);
}

double DEFCutter::getIOPinDistance(DCIOPin& pin, const DCRect& rect)
{
  return std::abs((static_cast<double>(rect.get_ll_x()) + rect.get_ur_x()) / 2 - pin.get_target_coord()[0])
         + std::abs((static_cast<double>(rect.get_ll_y()) + rect.get_ur_y()) / 2 - pin.get_target_coord()[1]);
}

void DEFCutter::updateDesign(DCModel& dc_model)
{
  idb::IdbLayout layout;
  idb::IdbDesign design(dmInst->get_idb_layout());
  idb::IdbDesign* source = dmInst->get_idb_design();
  design.set_design_name(dc_model.get_design_name() + "_cut");
  design.set_version("5.8");
  design.get_units()->set_microns_dbu(dc_model.get_micron_dbu());
  design.set_bus_bit_chars(new idb::IdbBusBitChars(*source->get_bus_bit_chars()));
  buildFloorplan(dc_model, &layout);
  buildInstanceList(dc_model, &design);
  buildPinList(dc_model, &design);
  buildNetList(dc_model, &design);
  buildShapeList(dc_model, &design);

  // Cloned via placements reference these DEF masters; transfer their ownership with the result.
  design.get_via_list()->get_via_list().swap(source->get_via_list()->get_via_list());
  design.get_via_list()->set_num_via(design.get_via_list()->get_via_list().size());
  source->get_via_list()->set_num_via(0);
  source->swap(design);
  updateFloorplan(layout);
  dmInst->get_idb_builder()->buildNet();
  dmInst->get_idb_builder()->buildBus();
}

void DEFCutter::buildFloorplan(DCModel& dc_model, idb::IdbLayout* layout)
{
  idb::IdbLayout* source = dmInst->get_idb_layout();
  layout->get_units()->set_microns_dbu(dc_model.get_micron_dbu());
  DCRect& die = dc_model.get_die();
  layout->initDie(die.get_ll_x(), die.get_ll_y(), die.get_ur_x(), die.get_ur_y());
  for (DCGrid& row : dc_model.get_row_list()) {
    if (row.get_count()[0] == 0 || row.get_count()[1] == 0) {
      continue;
    }
    idb::IdbRow* source_row = source->get_rows()->get_row_list()[row.get_source_idx()];
    layout->get_rows()->createRow(source_row->get_name(), source_row->get_site(), row.get_origin()[0], row.get_origin()[1], source_row->get_orient(),
                                  row.get_count()[0], row.get_count()[1], row.get_step()[0], row.get_step()[1]);
  }
  for (DCGrid& track : dc_model.get_track_list()) {
    int32_t axis = track.get_axis();
    if (track.get_count()[axis] == 0) {
      continue;
    }
    idb::IdbTrackGrid* source_track = source->get_track_grid_list()->get_track_grid_list()[track.get_source_idx()];
    idb::IdbTrackGrid* output_track = layout->get_track_grid_list()->add_track_grid();
    output_track->get_track()->set_direction(source_track->get_track()->get_direction());
    output_track->get_track()->set_start(track.get_origin()[axis]);
    output_track->get_track()->set_pitch(track.get_step()[axis]);
    output_track->set_track_number(track.get_count()[axis]);
    for (idb::IdbLayer* layer : source_track->get_layer_list()) {
      output_track->add_layer_list(layer);
    }
  }
  for (DCGrid& gcell : dc_model.get_gcell_list()) {
    int32_t axis = gcell.get_axis();
    if (gcell.get_count()[axis] == 0) {
      continue;
    }
    idb::IdbGCellGrid* output_grid = layout->get_gcell_grid_list()->add_gcell_grid();
    output_grid->set_direction(source->get_gcell_grid_list()->get_gcell_grid_list()[gcell.get_source_idx()]->get_direction());
    output_grid->set_start(gcell.get_origin()[axis]);
    output_grid->set_num(gcell.get_count()[axis]);
    output_grid->set_space(gcell.get_step()[axis]);
  }
}

void DEFCutter::buildInstanceList(DCModel& dc_model, idb::IdbDesign* design)
{
  for (DCObject& object : dc_model.get_instance_list()) {
    if (!object.get_retained()) {
      continue;
    }
    idb::IdbInstance* source = dmInst->get_idb_design()->get_instance_list()->get_instance_list()[object.get_source_idx()];
    idb::IdbInstance* instance = design->get_instance_list()->add_instance(source->get_name());
    instance->set_cell_master(source->get_cell_master());
    instance->set_type(source->get_type());
    instance->set_orient(source->get_orient(), false);
    instance->set_coodinate(source->get_coordinate()->get_x(), source->get_coordinate()->get_y(), false);
    instance->set_status(source->get_status());
    instance->set_weight(source->get_weight());
    instance->set_bounding_box();
    instance->set_pin_list_coodinate();
    instance->set_obs_box_list();
  }
}

void DEFCutter::buildPinList(DCModel& dc_model, idb::IdbDesign* design)
{
  std::set<int32_t> replaced_pin_set;
  for (DCIOPin& pin : dc_model.get_cut_pin_list()) {
    if (pin.get_source_idx() >= 0) {
      replaced_pin_set.insert(pin.get_source_idx());
    }
  }
  for (DCObject& object : dc_model.get_io_pin_list()) {
    if (!object.get_retained() || replaced_pin_set.contains(object.get_source_idx())) {
      continue;
    }
    idb::IdbPin* source = dmInst->get_idb_design()->get_io_pin_list()->get_pin_list()[object.get_source_idx()];
    idb::IdbPin* pin = design->get_io_pin_list()->add_pin_list(source->get_pin_name());
    copyPin(source, pin);
  }
  for (DCIOPin& io_pin : dc_model.get_cut_pin_list()) {
    DCNet& net = dc_model.get_net_list()[io_pin.get_net_idx()];
    idb::IdbDesign* source = dmInst->get_idb_design();
    idb::IdbConnectType type = net.get_is_special() ? source->get_special_net_list()->get_net_list()[net.get_source_idx()]->get_connect_type()
                                                    : source->get_net_list()->get_net_list()[net.get_source_idx()]->get_connect_type();
    if (io_pin.get_source_idx() >= 0) {
      type = source->get_io_pin_list()->get_pin_list()[io_pin.get_source_idx()]->get_term()->get_type();
    }
    idb::IdbPin* pin = design->get_io_pin_list()->add_pin_list(io_pin.get_name());
    pin->set_as_io();
    pin->set_net_name(net.get_name());
    pin->set_orient(idb::IdbOrient::kN_R0);
    idb::IdbTerm* term = pin->set_term();
    term->set_name(io_pin.get_name());
    term->set_direction(GetDCPinTypeName()(io_pin.get_type()));
    term->set_type(type == idb::IdbConnectType::kNone ? idb::IdbConnectType::kSignal : type);
    term->set_has_port(true);
    term->set_special(net.get_is_special());
    term->set_placement_status_fix();
    DCRect bounding_box = io_pin.get_shape_list().front().get_rect();
    for (DCLayerRect& shape : io_pin.get_shape_list()) {
      DCRect& rect = shape.get_rect();
      idb::IdbPort* port = term->add_port();
      port->set_placement_status_fix();
      port->set_orient(idb::IdbOrient::kN_R0);
      idb::IdbLayerShape* layer_shape = port->add_layer_shape();
      layer_shape->set_type_rect();
      layer_shape->set_layer(dmInst->get_idb_layout()->get_layers()->find_layer(shape.get_layer_name()));
      int32_t x = rect.get_ll_x() + (rect.get_ur_x() - rect.get_ll_x()) / 2;
      int32_t y = rect.get_ll_y() + (rect.get_ur_y() - rect.get_ll_y()) / 2;
      layer_shape->add_rect(rect.get_ll_x() - x, rect.get_ll_y() - y, rect.get_ur_x() - x, rect.get_ur_y() - y);
      port->set_coordinate(x, y);
      bounding_box.set_ll_x(std::min(bounding_box.get_ll_x(), rect.get_ll_x()));
      bounding_box.set_ll_y(std::min(bounding_box.get_ll_y(), rect.get_ll_y()));
      bounding_box.set_ur_x(std::max(bounding_box.get_ur_x(), rect.get_ur_x()));
      bounding_box.set_ur_y(std::max(bounding_box.get_ur_y(), rect.get_ur_y()));
    }
    DCRect& rect = io_pin.get_shape_list().front().get_rect();
    int32_t x = rect.get_ll_x() + (rect.get_ur_x() - rect.get_ll_x()) / 2;
    int32_t y = rect.get_ll_y() + (rect.get_ur_y() - rect.get_ll_y()) / 2;
    pin->set_location(x, y);
    pin->set_average_coordinate(x, y);
    term->get_bounding_box()->set_rect(bounding_box.get_ll_x() - x, bounding_box.get_ll_y() - y, bounding_box.get_ur_x() - x, bounding_box.get_ur_y() - y);
    pin->set_bounding_box();
  }
}

void DEFCutter::copyPin(idb::IdbPin* source, idb::IdbPin* pin)
{
  pin->set_as_io();
  pin->set_net_name(source->get_net_name().empty() && source->get_net() != nullptr ? source->get_net()->get_net_name() : source->get_net_name());
  pin->set_orient(source->get_orient());
  pin->set_location(source->get_location()->get_x(), source->get_location()->get_y());
  pin->set_average_coordinate(source->get_average_coordinate()->get_x(), source->get_average_coordinate()->get_y());
  idb::IdbTerm* source_term = source->get_term();
  idb::IdbTerm* term = pin->set_term();
  term->set_name(source_term->get_name());
  term->set_direction(source_term->get_direction());
  term->set_type(source_term->get_type());
  term->set_shape(source_term->get_shape());
  term->set_placement_status(source_term->get_placement_status());
  term->set_has_port(source_term->is_port_exist());
  term->set_special(source_term->is_special_net());
  term->set_average_position(source_term->get_average_position().get_x(), source_term->get_average_position().get_y());
  *term->get_bounding_box() = *source_term->get_bounding_box();
  if (source_term->has_antenna_gate_area()) {
    term->set_antenna_gate_area(source_term->get_antenna_gate_area());
  }
  if (source_term->has_antenna_diff_area()) {
    term->set_antenna_diff_area(source_term->get_antenna_diff_area());
  }
  for (const auto& [layer, area] : source_term->get_antenna_partial_metal_area()) {
    term->add_antenna_partial_metal_area(layer, area);
  }
  for (const auto& [layer, area] : source_term->get_antenna_partial_metal_side_area()) {
    term->add_antenna_partial_metal_side_area(layer, area);
  }
  for (const auto& [layer, area] : source_term->get_antenna_partial_cut_area()) {
    term->add_antenna_partial_cut_area(layer, area);
  }
  for (idb::IdbPort* source_port : source_term->get_port_list()) {
    idb::IdbPort* port = term->add_port();
    port->set_port_class(source_port->get_port_class());
    port->set_placement_status(source_port->get_placement_status());
    port->set_orient(source_port->get_orient());
    port->get_coordinate()->set_xy(source_port->get_coordinate()->get_x(), source_port->get_coordinate()->get_y());
    port->set_io_average_coordinate(source_port->get_io_average_coordinate()->get_x(), source_port->get_io_average_coordinate()->get_y());
    *port->get_io_bounding_box() = *source_port->get_io_bounding_box();
    for (idb::IdbLayerShape* shape : source_port->get_layer_shape()) {
      port->add_layer_shape(new idb::IdbLayerShape(*shape));
    }
    for (idb::IdbVia* via : source_port->get_via_list()) {
      port->add_via(via->clone());
    }
  }
  pin->set_bounding_box();
}

void DEFCutter::buildNetList(DCModel& dc_model, idb::IdbDesign* design)
{
  idb::IdbDesign* source = dmInst->get_idb_design();
  for (DCNet& net : dc_model.get_net_list()) {
    if (!net.get_retained()) {
      continue;
    }
    idb::IdbNet* source_net = net.get_is_special() ? nullptr : source->get_net_list()->get_net_list()[net.get_source_idx()];
    idb::IdbSpecialNet* source_pg_net = net.get_is_special() ? source->get_special_net_list()->get_net_list()[net.get_source_idx()] : nullptr;
    idb::IdbNet* output_net = net.get_is_special() ? nullptr : design->get_net_list()->add_net(net.get_name(), source_net->get_connect_type());
    idb::IdbSpecialNet* output_pg_net = net.get_is_special() ? design->get_special_net_list()->add_net(net.get_name()) : nullptr;
    if (net.get_is_special()) {
      output_pg_net->set_connect_type(source_pg_net->get_connect_type());
    }
    for (DCConnection& connection : net.get_connection_list()) {
      if (!connection.get_retained()) {
        continue;
      }
      idb::IdbPin* pin = nullptr;
      if (connection.get_is_io()) {
        pin = design->get_io_pin_list()->find_pin(connection.get_pin_name());
      } else {
        std::string instance_name = dc_model.get_instance_list()[connection.get_object_idx()].get_name();
        pin = design->get_instance_list()->find_instance(instance_name)->get_pin(connection.get_pin_name());
      }
      if (pin == nullptr) {
        MJLOG.error(Loc::current(), "Failed to rebuild retained pin ", connection.get_pin_name(), " on ", net.get_name());
      }
      connectPin(design, pin, output_net, output_pg_net);
    }
    if (net.get_cut_pin_idx() >= 0) {
      connectPin(design, design->get_io_pin_list()->find_pin(dc_model.get_cut_pin_list()[net.get_cut_pin_idx()].get_name()), output_net, output_pg_net);
    }
    std::map<int32_t, idb::IdbRegularWire*> wire_map;
    std::map<int32_t, idb::IdbSpecialWire*> pg_wire_map;
    for (DCRoute& route : net.get_route_list()) {
      if (!route.get_retained()) {
        continue;
      }
      DCRect& rect = route.get_rect();
      if (net.get_is_special()) {
        idb::IdbSpecialWire* source_wire = source_pg_net->get_wire_list()->get_wire_list()[route.get_wire_idx()];
        idb::IdbSpecialWireSegment* source_segment = source_wire->get_segment_list()[route.get_segment_idx()];
        if (!pg_wire_map.contains(route.get_wire_idx())) {
          idb::IdbSpecialWire* wire = output_pg_net->get_wire_list()->add_wire();
          wire->set_wire_state(source_wire->get_wire_state());
          wire->set_shield_name(source_wire->get_shiled_name());
          pg_wire_map[route.get_wire_idx()] = wire;
        }
        idb::IdbSpecialWireSegment* segment = pg_wire_map[route.get_wire_idx()]->add_segment();
        segment->set_layer(dmInst->get_idb_layout()->get_layers()->find_layer(route.get_layer_name()));
        segment->set_layer_as_new();
        segment->set_shape_type(source_segment->get_shape_type());
        if (route.get_via_idx() >= 0) {
          idb::IdbVia* via = source_segment->get_via()->clone();
          segment->set_is_via(true);
          segment->set_via(via);
          segment->set_route_width(0);
          segment->add_point(via->get_coordinate()->get_x(), via->get_coordinate()->get_y());
        } else {
          // Explicit zero end extensions serialize the exact clipped rectangle.
          if (rect.get_ur_x() - rect.get_ll_x() >= rect.get_ur_y() - rect.get_ll_y()) {
            int32_t width = rect.get_ur_y() - rect.get_ll_y();
            segment->set_route_width(width);
            segment->add_flush_point(rect.get_ll_x(), rect.get_ll_y() + width / 2, 0);
            segment->add_flush_point(rect.get_ur_x(), rect.get_ll_y() + width / 2, 0);
          } else {
            int32_t width = rect.get_ur_x() - rect.get_ll_x();
            segment->set_route_width(width);
            segment->add_flush_point(rect.get_ll_x() + width / 2, rect.get_ll_y(), 0);
            segment->add_flush_point(rect.get_ll_x() + width / 2, rect.get_ur_y(), 0);
          }
        }
      } else {
        idb::IdbRegularWire* source_wire = source_net->get_wire_list()->get_wire_list()[route.get_wire_idx()];
        idb::IdbRegularWireSegment* source_segment = source_wire->get_segment_list()[route.get_segment_idx()];
        if (!wire_map.contains(route.get_wire_idx())) {
          idb::IdbRegularWire* wire = output_net->get_wire_list()->add_wire();
          wire->set_wire_state(source_wire->get_wire_statement());
          wire->set_shield_name(source_wire->get_shiled_name());
          wire_map[route.get_wire_idx()] = wire;
        }
        idb::IdbRegularWireSegment* segment = wire_map[route.get_wire_idx()]->add_segment();
        segment->set_layer(dmInst->get_idb_layout()->get_layers()->find_layer(route.get_layer_name()));
        segment->set_layer_as_new();
        if (route.get_via_idx() >= 0) {
          idb::IdbVia* via = source_segment->get_via_list()[route.get_via_idx()];
          segment->set_is_via(true);
          segment->copy_via(via);
          segment->add_point(via->get_coordinate()->get_x(), via->get_coordinate()->get_y());
        } else {
          segment->set_is_rect(true);
          segment->add_point(rect.get_ll_x(), rect.get_ll_y());
          segment->set_delta_rect(0, 0, rect.get_ur_x() - rect.get_ll_x(), rect.get_ur_y() - rect.get_ll_y());
        }
      }
    }
  }
}

void DEFCutter::connectPin(idb::IdbDesign* design, idb::IdbPin* pin, idb::IdbNet* net, idb::IdbSpecialNet* special_net)
{
  if (special_net != nullptr) {
    design->connectPinToSpecialNet(pin, special_net);
  } else {
    design->connectPinToNet(pin, net);
  }
}

void DEFCutter::buildShapeList(DCModel& dc_model, idb::IdbDesign* design)
{
  idb::IdbDesign* source = dmInst->get_idb_design();
  std::vector<idb::IdbBlockage*> blockage_list = source->get_blockage_list()->get_blockage_list();
  for (DCObject& object : dc_model.get_blockage_list()) {
    if (!object.get_retained()) {
      continue;
    }
    idb::IdbBlockage* input = blockage_list[object.get_source_idx()];
    idb::IdbBlockage* output = nullptr;
    if (input->is_routing_blockage()) {
      idb::IdbRoutingBlockage* routing = dynamic_cast<idb::IdbRoutingBlockage*>(input);
      idb::IdbRoutingBlockage* copy = design->get_blockage_list()->add_blockage_routing(routing->get_layer_name());
      copy->set_layer(routing->get_layer());
      copy->set_slots(routing->is_slots());
      copy->set_fills(routing->is_fills());
      copy->set_except_pgnet(routing->is_except_pgnet());
      copy->set_min_spacing(routing->get_min_spacing());
      copy->set_effective_width(routing->get_effective_width());
      output = copy;
    } else {
      idb::IdbPlacementBlockage* placement = dynamic_cast<idb::IdbPlacementBlockage*>(input);
      idb::IdbPlacementBlockage* copy = design->get_blockage_list()->add_blockage_placement();
      copy->set_soft(placement->is_soft());
      copy->set_fills(placement->is_partial());
      copy->set_max_density(placement->get_max_density());
      output = copy;
    }
    output->set_pushdown(input->is_pushdown());
    idb::IdbInstance* instance = design->get_instance_list()->find_instance(input->get_instance_name());
    if (instance != nullptr) {
      output->set_instance(instance);
      output->set_instance_name(instance->get_name());
    }
    for (DCRect& rect : object.get_rect_list()) {
      output->add_rect(rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y());
    }
  }
  for (DCObject& object : dc_model.get_fill_list()) {
    if (!object.get_retained()) {
      continue;
    }
    idb::IdbFill* input = source->get_fill_list()->get_fill_list()[object.get_source_idx()];
    if (object.get_part_idx() < 0) {
      idb::IdbFillLayer* output = design->get_fill_list()->add_fill_layer(input->get_layer()->get_layer());
      for (DCRect& rect : object.get_rect_list()) {
        output->add_rect(rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y());
      }
    } else {
      idb::IdbFillVia* output = design->get_fill_list()->add_fill_via(input->get_via()->get_via()->clone());
      idb::IdbCoordinate<int32_t>* coord = input->get_via()->get_coordinate_list()[object.get_part_idx()];
      output->add_coordinate(coord->get_x(), coord->get_y());
    }
  }
  for (DCObject& object : dc_model.get_region_list()) {
    if (!object.get_retained()) {
      continue;
    }
    idb::IdbRegion* input = source->get_region_list()->get_region_list()[object.get_source_idx()];
    idb::IdbRegion* output = design->get_region_list()->add_region(input->get_name());
    output->set_type(input->get_type());
    for (DCRect& rect : object.get_rect_list()) {
      output->add_boundary(rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y());
    }
    for (idb::IdbInstance* input_instance : input->get_instance_list()) {
      idb::IdbInstance* instance = design->get_instance_list()->find_instance(input_instance->get_name());
      if (instance != nullptr) {
        output->add_instance(instance);
        instance->set_region(output);
      }
    }
  }
  for (idb::IdbGroup* input : source->get_group_list()->get_group_list()) {
    std::vector<idb::IdbInstance*> instance_list;
    for (idb::IdbInstance* input_instance : input->get_instance_list()->get_instance_list()) {
      idb::IdbInstance* instance = design->get_instance_list()->find_instance(input_instance->get_name());
      if (instance != nullptr) {
        instance_list.push_back(instance);
      }
    }
    if (instance_list.empty()) {
      continue;
    }
    idb::IdbGroup* output = design->get_group_list()->add_group(input->get_group_name());
    if (input->get_region() != nullptr) {
      output->set_region(design->get_region_list()->find_region(input->get_region()->get_name()));
    }
    for (idb::IdbInstance* instance : instance_list) {
      output->add_instance(instance);
    }
  }
  for (DCObject& object : dc_model.get_slot_list()) {
    if (!object.get_retained()) {
      continue;
    }
    idb::IdbSlot* input = source->get_slot_list()->get_slot_list()[object.get_source_idx()];
    idb::IdbSlot* output = design->get_slot_list()->add_slot();
    output->set_layer_name(input->get_layer_name());
    output->set_layer(input->get_layer());
    for (DCRect& rect : object.get_rect_list()) {
      output->add_rect(rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y());
    }
  }
}

void DEFCutter::updateFloorplan(idb::IdbLayout& layout)
{
  idb::IdbLayout* target = dmInst->get_idb_layout();
  idb::IdbRect* die = layout.get_die()->get_bounding_box();
  target->initDie(die->get_low_x(), die->get_low_y(), die->get_high_x(), die->get_high_y());
  target->get_rows()->reset();
  for (idb::IdbRow* row : layout.get_rows()->get_row_list()) {
    target->get_rows()->add_row_list(row);
  }
  layout.get_rows()->get_row_list().clear();
  layout.get_rows()->set_row_number(0);
  target->get_gcell_grid_list()->clear();
  target->get_gcell_grid_list()->get_gcell_grid_list().swap(layout.get_gcell_grid_list()->get_gcell_grid_list());
  for (idb::IdbLayer* layer : target->get_layers()->get_routing_layers()) {
    dynamic_cast<idb::IdbLayerRouting*>(layer)->get_track_grid_list().clear();
  }
  target->get_track_grid_list()->reset();
  for (idb::IdbTrackGrid* track : layout.get_track_grid_list()->get_track_grid_list()) {
    target->get_track_grid_list()->add_track_grid(track);
    for (idb::IdbLayer* layer : track->get_layer_list()) {
      dynamic_cast<idb::IdbLayerRouting*>(layer)->add_track_grid(track);
    }
  }
  layout.get_track_grid_list()->get_track_grid_list().clear();
}

#if 1  // exhibit

void DEFCutter::writeReport(DCModel& dc_model)
{
  std::string path = MJDM.getConfig().dc_temp_directory_path + "cut_report.rpt";
  std::ofstream* stream = MJUTIL.getOutputFileStream(path);
  *stream << "Design : " << dc_model.get_design_name() << "\nCoordinates : absolute, DBU"
          << "\nDie : " << dc_model.get_die().get_ll_x() << " " << dc_model.get_die().get_ll_y() << " " << dc_model.get_die().get_ur_x() << " "
          << dc_model.get_die().get_ur_y() << "\nOnly complete placed instances, ports and vias are retained.\n"
          << "Each ordinary cut net reuses or adds one logical IO for all removed terminals.\n"
          << "Special nets do not generate or extend IOs, including same-name ordinary net records.\n"
          << "IOs use boundary routing cuts or legal edge tracks; unrouted connections require routing to the new IOs.\n"
          << "IO layers follow the mean routing-layer position of all net terminals; CUT layers are excluded.\n"
          << "IO directions follow inside/outside driver roles; PG and unresolved directions use INOUT.\n";
  int32_t kept_instance_num = 0;
  for (DCObject& object : dc_model.get_instance_list()) {
    if (object.get_retained()) {
      kept_instance_num++;
    } else {
      *stream << "RemovedInstance : " << object.get_name() << "\n";
    }
  }
  int32_t kept_net_num = 0;
  int32_t cut_net_num = 0;
  for (DCNet& net : dc_model.get_net_list()) {
    if (!net.get_retained()) {
      continue;
    }
    kept_net_num++;
    if (net.get_cut_pin_idx() >= 0) {
      cut_net_num++;
      *stream << "CutNet : " << net.get_name() << " removed_pins=" << net.get_removed_pin_num() << " io_pins=1\n";
    }
  }
  int32_t added_pin_num = 0;
  for (DCIOPin& pin : dc_model.get_cut_pin_list()) {
    added_pin_num += pin.get_source_idx() < 0;
    *stream << "IOPin : " << pin.get_name() << " net=" << dc_model.get_net_list()[pin.get_net_idx()].get_name()
            << " direction=" << GetDCPinTypeName()(pin.get_type()) << " ports=" << pin.get_shape_list().size() << " reused=" << (pin.get_source_idx() >= 0)
            << " layer=" << pin.get_layer_name() << "\n";
    for (DCLayerRect& shape : pin.get_shape_list()) {
      DCRect& rect = shape.get_rect();
      *stream << "  " << shape.get_layer_name() << " " << rect.get_ll_x() << " " << rect.get_ll_y() << " " << rect.get_ur_x() << " " << rect.get_ur_y() << "\n";
    }
  }
  *stream << "RetainedInstances : " << kept_instance_num << "/" << dc_model.get_instance_list().size() << "\nRetainedNets : " << kept_net_num << "/"
          << dc_model.get_net_list().size() << "\nCutNets : " << cut_net_num << "\nAddedIOPins : " << added_pin_num
          << "\nExtendedIOPins : " << dc_model.get_cut_pin_list().size() - added_pin_num << "\n";
  MJUTIL.closeFileStream(stream);
  MJLOG.info(Loc::current(), "Retained instances=", kept_instance_num, ", nets=", kept_net_num, ", cut nets=", cut_net_num);
}

#endif

DEFCutter* DEFCutter::_dc_instance = nullptr;

}  // namespace imj
