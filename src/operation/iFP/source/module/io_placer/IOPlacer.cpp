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
  if (config.io_pin_layer_name_list.empty() || config.io_pin_width_micron <= 0.0 || config.io_pin_depth_micron <= 0.0) {
    return;
  }

  int32_t micron_dbu = FPDM.getDatabase().get_micron_dbu();
  int32_t width = FPUTIL.transMicronToDBU(config.io_pin_width_micron, micron_dbu);
  int32_t depth = FPUTIL.transMicronToDBU(config.io_pin_depth_micron, micron_dbu);
  autoPlacePins(config.io_pin_layer_name_list, width, depth);
}

void IOPlacer::autoPlacePins(std::vector<std::string>& layer_name_list, int32_t width, int32_t depth)
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

  std::vector<IOPin>& io_pin_list = database.get_io_pin_list();
  int32_t io_pin_num = static_cast<int32_t>(io_pin_list.size());
  int32_t edge_pin_num = io_pin_num % 4 == 0 ? io_pin_num / 4 : io_pin_num / 4 + 1;
  int32_t io_pin_idx = 0;
  int32_t horizontal_pitch = getTrackPitch(horizontal_layer_name);
  int32_t vertical_pitch = getTrackPitch(vertical_layer_name);
  int32_t manufacture_grid = database.get_manufacture_grid();

  placeIOPinsOnEdge(IOEdgeType::kLeft, io_pin_list, io_pin_idx, edge_pin_num, horizontal_layer_name, vertical_layer_name, width, depth,
                    horizontal_pitch, vertical_pitch, manufacture_grid);
  placeIOPinsOnEdge(IOEdgeType::kRight, io_pin_list, io_pin_idx, edge_pin_num, horizontal_layer_name, vertical_layer_name, width, depth,
                    horizontal_pitch, vertical_pitch, manufacture_grid);
  placeIOPinsOnEdge(IOEdgeType::kBottom, io_pin_list, io_pin_idx, edge_pin_num, horizontal_layer_name, vertical_layer_name, width, depth,
                    horizontal_pitch, vertical_pitch, manufacture_grid);
  placeIOPinsOnEdge(IOEdgeType::kTop, io_pin_list, io_pin_idx, edge_pin_num, horizontal_layer_name, vertical_layer_name, width, depth,
                    horizontal_pitch, vertical_pitch, manufacture_grid);
}

int32_t IOPlacer::getTrackPitch(std::string layer_name)
{
  Database& database = FPDM.getDatabase();
  std::map<std::string, int32_t>::iterator routing_layer_iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
  if (routing_layer_iter == database.get_routing_layer_name_to_idx_map().end()) {
    return 0;
  }

  RoutingLayer& routing_layer = database.get_routing_layer_list()[routing_layer_iter->second];
  return std::max(std::max(routing_layer.get_prefer_track_pitch(), routing_layer.get_nonprefer_track_pitch()),
                  std::max(routing_layer.get_pitch_x(), routing_layer.get_pitch_y()));
}

void IOPlacer::placeIOPinsOnEdge(IOEdgeType edge_type, std::vector<IOPin>& io_pin_list, int32_t& io_pin_idx, int32_t edge_pin_num,
                                 std::string horizontal_layer_name, std::string vertical_layer_name, int32_t width, int32_t depth,
                                 int32_t horizontal_pitch, int32_t vertical_pitch, int32_t manufacture_grid)
{
  Die& die = FPDM.getDatabase().get_die();
  Core& core = FPDM.getDatabase().get_core();
  int32_t io_pin_num = static_cast<int32_t>(io_pin_list.size());
  int32_t side_pin_num = std::min(edge_pin_num, io_pin_num - io_pin_idx);

  for (int32_t side_pin_idx = 0; side_pin_idx < side_pin_num; side_pin_idx++) {
    int32_t x = -1;
    int32_t y = -1;
    int32_t rect_width = -1;
    int32_t rect_height = -1;
    std::string layer_name;
    switch (edge_type) {
      case IOEdgeType::kLeft:
        x = die.get_ll_x() + depth / 2;
        y = getAlongCoord(core.get_ll_y(), core.get_ur_y(), die.get_ll_y(), die.get_ur_y(), width, horizontal_pitch, side_pin_num,
                          side_pin_idx, manufacture_grid);
        rect_width = depth;
        rect_height = width;
        layer_name = horizontal_layer_name;
        break;
      case IOEdgeType::kRight:
        x = die.get_ur_x() - depth / 2;
        y = getAlongCoord(core.get_ll_y(), core.get_ur_y(), die.get_ll_y(), die.get_ur_y(), width, horizontal_pitch, side_pin_num,
                          side_pin_idx, manufacture_grid);
        rect_width = depth;
        rect_height = width;
        layer_name = horizontal_layer_name;
        break;
      case IOEdgeType::kBottom:
        x = getAlongCoord(core.get_ll_x(), core.get_ur_x(), die.get_ll_x(), die.get_ur_x(), width, vertical_pitch, side_pin_num,
                          side_pin_idx, manufacture_grid);
        y = die.get_ll_y() + depth / 2;
        rect_width = width;
        rect_height = depth;
        layer_name = vertical_layer_name;
        break;
      case IOEdgeType::kTop:
        x = getAlongCoord(core.get_ll_x(), core.get_ur_x(), die.get_ll_x(), die.get_ur_x(), width, vertical_pitch, side_pin_num,
                          side_pin_idx, manufacture_grid);
        y = die.get_ur_y() - depth / 2;
        rect_width = width;
        rect_height = depth;
        layer_name = vertical_layer_name;
        break;
      default:
        return;
    }
    addIOPinPort(io_pin_list[io_pin_idx++], x, y, rect_width, rect_height, manufacture_grid, layer_name);
  }
}

int32_t IOPlacer::getAlongCoord(int32_t range_low, int32_t range_high, int32_t die_low, int32_t die_high, int32_t pin_span,
                                int32_t access_pitch, int32_t side_pin_num, int32_t pin_idx, int32_t manufacture_grid)
{
  int32_t legal_low = std::max(range_low, die_low + access_pitch);
  int32_t legal_high = std::min(range_high, die_high - access_pitch);
  int32_t start = FPUTIL.alignUp(legal_low + pin_span / 2, manufacture_grid);
  int32_t end = FPUTIL.alignDown(legal_high - pin_span / 2, manufacture_grid);

  if (start > end) {
    start = FPUTIL.alignUp(range_low + pin_span / 2, manufacture_grid);
    end = FPUTIL.alignDown(range_high - pin_span / 2, manufacture_grid);
  }
  if (start > end) {
    return FPUTIL.alignNearest((range_low + range_high) / 2, manufacture_grid);
  }
  if (side_pin_num <= 1) {
    return FPUTIL.alignNearest((start + end) / 2, manufacture_grid);
  }

  int64_t span = static_cast<int64_t>(end - start);
  int32_t coord = start + static_cast<int32_t>((span * pin_idx + (side_pin_num - 1) / 2) / static_cast<int64_t>(side_pin_num - 1));
  coord = FPUTIL.alignNearest(coord, manufacture_grid);
  return std::max(start, std::min(end, coord));
}

void IOPlacer::addIOPinPort(IOPin& io_pin, int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, int32_t manufacture_grid,
                            std::string layer_name)
{
  io_pin.set_placed(true);
  io_pin.set_fixed(false);

  IOPort io_port;
  syncPinLocation(io_pin, io_port, x, y);

  int32_t shape_ll_x = FPUTIL.alignDown(x - rect_width / 2, manufacture_grid);
  int32_t shape_ll_y = FPUTIL.alignDown(y - rect_height / 2, manufacture_grid);
  int32_t shape_ur_x = shape_ll_x + rect_width;
  int32_t shape_ur_y = shape_ll_y + rect_height;
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
