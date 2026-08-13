// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "IOPlacer.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ifp {

// public

void IOPlacer::initInst()
{
  if (_ip_instance == nullptr) {
    _ip_instance = new IOPlacer();
  }
}

IOPlacer& IOPlacer::getInst()
{
  if (_ip_instance == nullptr) {
    FPLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ip_instance;
}

void IOPlacer::destroyInst()
{
  if (_ip_instance != nullptr) {
    delete _ip_instance;
    _ip_instance = nullptr;
  }
}

// function

void IOPlacer::place()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  placeIOPin();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void IOPlacer::placeIOPin()
{
  Config& config = FPDM.getConfig();
  if (config.io_pin_layer_name_list.empty()) {
    return;
  }

  autoPlacePins(config.io_pin_layer_name_list);
}

void IOPlacer::autoPlacePins(std::vector<std::string>& layer_name_list)
{
  Database& database = FPDM.getDatabase();
  std::string horizontal_layer_name;
  std::string vertical_layer_name;
  for (std::string& layer_name : layer_name_list) {
    std::map<std::string, int32_t>::iterator routing_layer_iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
    if (routing_layer_iter == database.get_routing_layer_name_to_idx_map().end()) {
      continue;
    }

    RoutingLayer& routing_layer = database.get_routing_layer_list()[routing_layer_iter->second];
    if (routing_layer.get_prefer_direction() == Direction::kHorizontal && horizontal_layer_name.empty()) {
      horizontal_layer_name = routing_layer.get_name();
    }
    if (routing_layer.get_prefer_direction() == Direction::kVertical && vertical_layer_name.empty()) {
      vertical_layer_name = routing_layer.get_name();
    }
  }
  if (horizontal_layer_name.empty() || vertical_layer_name.empty()) {
    return;
  }

  int32_t horizontal_pitch = getTrackPitch(horizontal_layer_name);
  int32_t vertical_pitch = getTrackPitch(vertical_layer_name);
  if (horizontal_pitch <= 0 || vertical_pitch <= 0) {
    return;
  }

  int32_t vertical_depth = 4 * horizontal_pitch;
  int32_t horizontal_depth = 4 * vertical_pitch;

  std::vector<IOPin>& io_pin_list = database.get_io_pin_list();
  int32_t io_pin_num = static_cast<int32_t>(io_pin_list.size());
  int32_t edge_pin_num = io_pin_num % 4 == 0 ? io_pin_num / 4 : io_pin_num / 4 + 1;
  int32_t io_pin_idx = 0;
  int32_t horizontal_offset = getTrackOffset(horizontal_layer_name);
  int32_t vertical_offset = getTrackOffset(vertical_layer_name);

  placeIOPinsOnEdge(IOEdgeType::kLeft, io_pin_list, io_pin_idx, edge_pin_num, horizontal_layer_name, horizontal_pitch,
                    horizontal_depth, vertical_pitch, horizontal_offset, horizontal_pitch);
  placeIOPinsOnEdge(IOEdgeType::kRight, io_pin_list, io_pin_idx, edge_pin_num, horizontal_layer_name, horizontal_pitch,
                    horizontal_depth, vertical_pitch, horizontal_offset, horizontal_pitch);
  placeIOPinsOnEdge(IOEdgeType::kBottom, io_pin_list, io_pin_idx, edge_pin_num, vertical_layer_name, vertical_pitch,
                    vertical_depth, horizontal_pitch, vertical_offset, vertical_pitch);
  placeIOPinsOnEdge(IOEdgeType::kTop, io_pin_list, io_pin_idx, edge_pin_num, vertical_layer_name, vertical_pitch,
                    vertical_depth, horizontal_pitch, vertical_offset, vertical_pitch);
}

int32_t IOPlacer::getTrackPitch(std::string layer_name)
{
  Database& database = FPDM.getDatabase();
  std::map<std::string, int32_t>::iterator routing_layer_iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
  if (routing_layer_iter == database.get_routing_layer_name_to_idx_map().end()) {
    return 0;
  }

  RoutingLayer& routing_layer = database.get_routing_layer_list()[routing_layer_iter->second];
  int32_t pitch = routing_layer.get_prefer_direction() == Direction::kHorizontal ? routing_layer.get_pitch_y() : routing_layer.get_pitch_x();
  if (pitch <= 0) {
    pitch = routing_layer.get_prefer_track_pitch();
  }
  return pitch;
}

int32_t IOPlacer::getTrackOffset(std::string layer_name)
{
  Database& database = FPDM.getDatabase();
  auto iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
  if (iter == database.get_routing_layer_name_to_idx_map().end()) {
    return 0;
  }
  return std::max(database.get_routing_layer_list()[iter->second].get_prefer_track_offset(), 0);
}

void IOPlacer::placeIOPinsOnEdge(IOEdgeType edge_type, std::vector<IOPin>& io_pin_list, int32_t& io_pin_idx, int32_t edge_pin_num,
                                 std::string layer_name, int32_t width, int32_t depth, int32_t access_pitch, int32_t track_offset,
                                 int32_t track_pitch)
{
  Die& die = FPDM.getDatabase().get_die();
  Core& core = FPDM.getDatabase().get_core();
  int32_t io_pin_num = static_cast<int32_t>(io_pin_list.size());
  int32_t side_pin_num = std::min(edge_pin_num, io_pin_num - io_pin_idx);

  for (int32_t side_pin_idx = 0; side_pin_idx < side_pin_num; side_pin_idx++) {
    int32_t x = -1;
    int32_t y = -1;
    switch (edge_type) {
      case IOEdgeType::kLeft:
        x = die.get_ll_x() + depth / 2;
        y = getAlongCoord(core.get_ll_y(), core.get_ur_y(), die.get_ll_y(), die.get_ur_y(), width, access_pitch, side_pin_num,
                          side_pin_idx, track_offset, track_pitch);
        break;
      case IOEdgeType::kRight:
        x = die.get_ur_x() - depth / 2;
        y = getAlongCoord(core.get_ll_y(), core.get_ur_y(), die.get_ll_y(), die.get_ur_y(), width, access_pitch, side_pin_num,
                          side_pin_idx, track_offset, track_pitch);
        break;
      case IOEdgeType::kBottom:
        x = getAlongCoord(core.get_ll_x(), core.get_ur_x(), die.get_ll_x(), die.get_ur_x(), width, access_pitch, side_pin_num,
                          side_pin_idx, track_offset, track_pitch);
        y = die.get_ll_y() + depth / 2;
        break;
      case IOEdgeType::kTop:
        x = getAlongCoord(core.get_ll_x(), core.get_ur_x(), die.get_ll_x(), die.get_ur_x(), width, access_pitch, side_pin_num,
                          side_pin_idx, track_offset, track_pitch);
        y = die.get_ur_y() - depth / 2;
        break;
      default:
        return;
    }
    addIOPinPort(io_pin_list[io_pin_idx++], edge_type, x, y, width, depth, layer_name);
  }
}

int32_t IOPlacer::getAlongCoord(int32_t range_low, int32_t range_high, int32_t die_low, int32_t die_high, int32_t pin_span,
                                int32_t access_pitch, int32_t side_pin_num, int32_t pin_idx, int32_t track_offset, int32_t track_pitch)
{
  int32_t legal_low = std::max(range_low, die_low + access_pitch);
  int32_t legal_high = std::min(range_high, die_high - access_pitch);
  auto align_up = [track_offset, track_pitch](int32_t value) {
    return track_offset + FPUTIL.alignUp(value - track_offset, track_pitch);
  };
  auto align_down = [track_offset, track_pitch](int32_t value) {
    return track_offset + FPUTIL.alignDown(value - track_offset, track_pitch);
  };
  int32_t start = align_up(legal_low + pin_span / 2);
  int32_t end = align_down(legal_high - pin_span / 2);

  if (start > end) {
    start = align_up(range_low + pin_span / 2);
    end = align_down(range_high - pin_span / 2);
  }
  if (start > end) {
    return track_offset + FPUTIL.alignNearest((range_low + range_high) / 2 - track_offset, track_pitch);
  }
  if (side_pin_num <= 1) {
    return track_offset + FPUTIL.alignNearest((start + end) / 2 - track_offset, track_pitch);
  }

  int64_t span = static_cast<int64_t>(end - start);
  int32_t coord = start + static_cast<int32_t>((span * pin_idx + (side_pin_num - 1) / 2) / static_cast<int64_t>(side_pin_num - 1));
  coord = track_offset + FPUTIL.alignNearest(coord - track_offset, track_pitch);
  return std::max(start, std::min(end, coord));
}

void IOPlacer::addIOPinPort(IOPin& io_pin, IOEdgeType edge_type, int32_t x, int32_t y, int32_t width, int32_t depth, std::string layer_name)
{
  io_pin.set_placed(true);
  io_pin.set_fixed(false);

  IOPort io_port;
  syncPinLocation(io_pin, io_port, x, y);

  Die& die = FPDM.getDatabase().get_die();
  int32_t shape_ll_x = x - width / 2;
  int32_t shape_ll_y = y - width / 2;
  int32_t shape_ur_x = x + width / 2;
  int32_t shape_ur_y = y + width / 2;
  if (edge_type == IOEdgeType::kLeft) {
    shape_ll_x = die.get_ll_x();
    shape_ur_x = die.get_ll_x() + depth;
  } else if (edge_type == IOEdgeType::kRight) {
    shape_ll_x = die.get_ur_x() - depth;
    shape_ur_x = die.get_ur_x();
  } else if (edge_type == IOEdgeType::kBottom) {
    shape_ll_y = die.get_ll_y();
    shape_ur_y = die.get_ll_y() + depth;
  } else if (edge_type == IOEdgeType::kTop) {
    shape_ll_y = die.get_ur_y() - depth;
    shape_ur_y = die.get_ur_y();
  }
  io_port.set_layer_name(layer_name);
  io_port.set_rect(shape_ll_x - x, shape_ll_y - y, shape_ur_x - x, shape_ur_y - y);
  io_pin.get_new_port_list().push_back(io_port);
  io_pin.set_updated(true);
  updateNetIOPin(io_pin);
}

void IOPlacer::syncPinLocation(IOPin& io_pin, IOPort& io_port, int32_t x, int32_t y)
{
  if (io_pin.get_port_exist() || io_pin.get_special_net()) {
    io_port.set_placed(true);
    io_port.set_coord(x, y);
  } else {
    io_pin.set_direct_location(true);
  }
  io_pin.set_coord(x, y);
  io_pin.set_orient(PlacementOrientation::kN);
}

void IOPlacer::updateNetIOPin(IOPin& io_pin)
{
  for (Net& net : FPDM.getDatabase().get_net_list()) {
    for (NetPin& net_pin : net.get_net_pin_list()) {
      if (net_pin.get_io() && net_pin.get_pin_name() == io_pin.get_name()) {
        net_pin.set_coord(io_pin.get_x(), io_pin.get_y());
        net_pin.set_placed(io_pin.get_placed());
      }
    }
  }
}

// private

IOPlacer* IOPlacer::_ip_instance = nullptr;

}  // namespace ifp
