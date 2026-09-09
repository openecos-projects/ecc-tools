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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>
#include <vector>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ifp {

namespace {

struct PinSlot
{
  int64_t cost = 0;
  int32_t edge_order = 0;
  IOEdgeType edge_type = IOEdgeType::kNone;
  int32_t coord = 0;
};

struct InputPinPlacement
{
  int32_t pin_idx = -1;
  int32_t line_num = 0;
  IOEdgeType edge_type = IOEdgeType::kNone;
  int32_t along_coord = 0;
  int32_t width = 0;
  int32_t depth = 0;
  std::string layer_name;
};

IOEdgeType getIOEdgeType(std::string edge_name)
{
  std::transform(edge_name.begin(), edge_name.end(), edge_name.begin(),
                 [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  if (edge_name == "LEFT") {
    return IOEdgeType::kLeft;
  }
  if (edge_name == "RIGHT") {
    return IOEdgeType::kRight;
  }
  if (edge_name == "BOTTOM") {
    return IOEdgeType::kBottom;
  }
  if (edge_name == "TOP") {
    return IOEdgeType::kTop;
  }
  return IOEdgeType::kNone;
}

}  // namespace

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

  resetIOPinPlacement();
  placeIOPin();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void IOPlacer::placeAuto()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  resetIOPinPlacement();
  Config& config = FPDM.getConfig();
  if (!config.io_pin_layer_name_list.empty()) {
    autoPlacePins(config.io_pin_layer_name_list);
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void IOPlacer::placeIOPin()
{
  Config& config = FPDM.getConfig();
  if (config.io_pin_placement_mode == PlacementMode::kFile) {
    if (config.input_io_pin_path.empty()) {
      FPLOG.error(Loc::current(), "IO placer mode is 'file', but io_placer.file_path is empty!");
    }
    placeIOPinsFromFile(config.input_io_pin_path);
    return;
  }
  if (config.io_pin_layer_name_list.empty()) {
    return;
  }

  autoPlacePins(config.io_pin_layer_name_list);
}

void IOPlacer::resetIOPinPlacement()
{
  for (IOPin& io_pin : FPDM.getDatabase().get_io_pin_list()) {
    io_pin.set_coord(-1, -1);
    io_pin.set_placed(false);
    io_pin.set_fixed(false);
    io_pin.set_direct_location(false);
    io_pin.get_port_list().clear();
    io_pin.get_new_port_list().clear();
    io_pin.set_updated(false);
  }
}

void IOPlacer::placeIOPinsFromFile(const std::string& file_path)
{
  Database& database = FPDM.getDatabase();
  Config& config = FPDM.getConfig();
  if (config.io_pin_layer_name_list.empty()) {
    FPLOG.error(Loc::current(), "IO pin layer list must not be empty when using input file '", file_path, "'!");
  }

  std::string horizontal_layer_name;
  std::string vertical_layer_name;
  for (const std::string& layer_name : config.io_pin_layer_name_list) {
    auto layer_iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
    if (layer_iter == database.get_routing_layer_name_to_idx_map().end()) {
      continue;
    }
    RoutingLayer& layer = database.get_routing_layer_list()[layer_iter->second];
    if (layer.get_prefer_direction() == Direction::kHorizontal && horizontal_layer_name.empty()) {
      horizontal_layer_name = layer_name;
    } else if (layer.get_prefer_direction() == Direction::kVertical && vertical_layer_name.empty()) {
      vertical_layer_name = layer_name;
    }
  }
  if (horizontal_layer_name.empty() || vertical_layer_name.empty()) {
    FPLOG.error(Loc::current(), "Input IO pin placement requires horizontal and vertical routing layers!");
  }

  int32_t horizontal_depth = 4 * getTrackPitch(vertical_layer_name);
  int32_t vertical_depth = 4 * getTrackPitch(horizontal_layer_name);
  if (horizontal_depth <= 0 || vertical_depth <= 0) {
    FPLOG.error(Loc::current(), "Failed to determine valid IO pin depths!");
  }

  std::ifstream placement_file(file_path);
  if (!placement_file.is_open()) {
    FPLOG.error(Loc::current(), "Failed to open IO pin placement file '", file_path, "'!");
  }

  std::vector<IOPin>& io_pin_list = database.get_io_pin_list();
  const std::map<std::string, int32_t>& pin_name_to_idx = database.get_io_pin_name_to_idx_map();
  std::set<std::string> placed_pin_names;
  std::vector<InputPinPlacement> placement_list;
  int32_t line_num = 0;
  std::string line;
  while (std::getline(placement_file, line)) {
    ++line_num;
    size_t first_character = line.find_first_not_of(" \t\r");
    if (first_character == std::string::npos || line[first_character] == '#') {
      continue;
    }

    std::istringstream line_stream(line);
    std::string pin_name;
    std::string edge_name;
    std::string layer_name;
    std::string trailing_token;
    double offset_micron = 0.0;
    if (!(line_stream >> pin_name >> edge_name >> offset_micron >> layer_name)
        || (line_stream >> trailing_token && trailing_token[0] != '#')) {
      FPLOG.error(Loc::current(), "Malformed IO pin placement at line ", line_num, " in '", file_path,
                  "'. Expected: pin_name edge offset_micron layer.");
    }
    if (!std::isfinite(offset_micron)) {
      FPLOG.error(Loc::current(), "Non-finite IO pin offset at line ", line_num, " in '", file_path, "'!");
    }

    auto pin_iter = pin_name_to_idx.find(pin_name);
    if (pin_iter == pin_name_to_idx.end()) {
      FPLOG.error(Loc::current(), "Unknown IO pin '", pin_name, "' at line ", line_num, " in '", file_path, "'!");
    }
    if (!placed_pin_names.insert(pin_name).second) {
      FPLOG.error(Loc::current(), "Duplicate IO pin '", pin_name, "' at line ", line_num, " in '", file_path, "'!");
    }
    if (std::find(config.io_pin_layer_name_list.begin(), config.io_pin_layer_name_list.end(), layer_name)
        == config.io_pin_layer_name_list.end()) {
      FPLOG.error(Loc::current(), "IO pin '", pin_name, "' uses layer '", layer_name, "' outside io_layer_list!");
    }

    auto layer_iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
    if (layer_iter == database.get_routing_layer_name_to_idx_map().end()) {
      FPLOG.error(Loc::current(), "Unknown routing layer '", layer_name, "' for IO pin '", pin_name, "'!");
    }
    RoutingLayer& layer = database.get_routing_layer_list()[layer_iter->second];
    IOEdgeType edge_type = getIOEdgeType(edge_name);
    bool vertical_edge = edge_type == IOEdgeType::kLeft || edge_type == IOEdgeType::kRight;
    if (edge_type == IOEdgeType::kNone
        || (vertical_edge && layer.get_prefer_direction() != Direction::kHorizontal)
        || (!vertical_edge && layer.get_prefer_direction() != Direction::kVertical)) {
      FPLOG.error(Loc::current(), "Invalid edge/layer combination for IO pin '", pin_name, "' at line ", line_num, "!");
    }

    double offset_dbu = offset_micron * database.get_micron_dbu();
    if (offset_dbu < std::numeric_limits<int32_t>::min() || offset_dbu > std::numeric_limits<int32_t>::max()) {
      FPLOG.error(Loc::current(), "IO pin offset is outside the supported coordinate range at line ", line_num, " in '", file_path,
                  "'!");
    }

    int32_t width = layer.get_min_width();
    int32_t track_pitch = getTrackPitch(layer_name);
    int32_t track_offset = getTrackOffset(layer_name);
    int32_t along_coord = FPUTIL.transMicronToDBU(offset_micron, database.get_micron_dbu());
    if (width <= 0 || track_pitch <= 0 || (static_cast<int64_t>(along_coord) - track_offset) % track_pitch != 0) {
      FPLOG.error(Loc::current(), "IO pin '", pin_name, "' is not on a valid track at line ", line_num, "!");
    }

    Core& core = database.get_core();
    int32_t range_low = vertical_edge ? core.get_ll_y() : core.get_ll_x();
    int32_t range_high = vertical_edge ? core.get_ur_y() : core.get_ur_x();
    if (2LL * along_coord - width < 2LL * range_low || 2LL * along_coord + width > 2LL * range_high) {
      FPLOG.error(Loc::current(), "IO pin '", pin_name, "' is outside the legal core-edge range at line ", line_num, "!");
    }

    placement_list.push_back(
        {pin_iter->second, line_num, edge_type, along_coord, width, vertical_edge ? horizontal_depth : vertical_depth, layer_name});
  }

  if (placement_list.size() != io_pin_list.size()) {
    FPLOG.error(Loc::current(), "IO pin placement file '", file_path, "' contains ", placement_list.size(), " unique pin(s), but the design has ",
                io_pin_list.size(), ". Please specify every IO pin exactly once!");
  }

  for (size_t first_idx = 0; first_idx < placement_list.size(); ++first_idx) {
    const InputPinPlacement& first = placement_list[first_idx];
    for (size_t second_idx = first_idx + 1; second_idx < placement_list.size(); ++second_idx) {
      const InputPinPlacement& second = placement_list[second_idx];
      if (first.edge_type != second.edge_type || first.layer_name != second.layer_name) {
        continue;
      }
      int64_t first_low = 2LL * first.along_coord - first.width;
      int64_t first_high = 2LL * first.along_coord + first.width;
      int64_t second_low = 2LL * second.along_coord - second.width;
      int64_t second_high = 2LL * second.along_coord + second.width;
      if (std::max(first_low, second_low) < std::min(first_high, second_high)) {
        FPLOG.error(Loc::current(), "Overlapping IO pin shapes at lines ", first.line_num, " and ", second.line_num, " in '", file_path, "'!");
      }
    }
  }

  Die& die = database.get_die();
  for (const InputPinPlacement& placement : placement_list) {
    bool vertical_edge = placement.edge_type == IOEdgeType::kLeft || placement.edge_type == IOEdgeType::kRight;
    int32_t x = vertical_edge ? (placement.edge_type == IOEdgeType::kLeft ? die.get_ll_x() + placement.depth / 2
                                                                         : die.get_ur_x() - placement.depth / 2)
                              : placement.along_coord;
    int32_t y = vertical_edge ? placement.along_coord
                              : (placement.edge_type == IOEdgeType::kBottom ? die.get_ll_y() + placement.depth / 2
                                                                            : die.get_ur_y() - placement.depth / 2);
    IOPin& io_pin = io_pin_list[placement.pin_idx];
    addIOPinPort(io_pin, placement.edge_type, x, y, placement.width, placement.depth, placement.layer_name);
    io_pin.set_fixed(true);
  }
  FPLOG.info(Loc::current(), "Placed ", placement_list.size(), " fixed IO pins from '", file_path, "'.");
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

  int32_t horizontal_width = getLayerMinWidth(horizontal_layer_name);
  int32_t vertical_width = getLayerMinWidth(vertical_layer_name);
  int32_t horizontal_pitch = getTrackPitch(horizontal_layer_name);
  int32_t vertical_pitch = getTrackPitch(vertical_layer_name);
  if (horizontal_width <= 0 || vertical_width <= 0 || horizontal_pitch <= 0 || vertical_pitch <= 0) {
    return;
  }

  int32_t vertical_depth = 4 * horizontal_pitch;
  int32_t horizontal_depth = 4 * vertical_pitch;

  std::vector<IOPin>& io_pin_list = database.get_io_pin_list();
  int32_t io_pin_num = static_cast<int32_t>(io_pin_list.size());
  if (io_pin_num == 0) {
    return;
  }

  int32_t horizontal_offset = getTrackOffset(horizontal_layer_name);
  int32_t vertical_offset = getTrackOffset(vertical_layer_name);
  Die& die = database.get_die();
  Core& core = database.get_core();

  auto build_slots = [&](int32_t spacing_multiplier) {
    std::vector<PinSlot> slot_list;
    auto add_edge_slots = [&](IOEdgeType edge_type, int32_t edge_order, int32_t range_low, int32_t range_high, int32_t die_low,
                              int32_t die_high, int32_t pin_span, int32_t access_pitch, int32_t track_offset, int32_t track_pitch,
                              int32_t perpendicular_span) {
      int32_t legal_low = std::max(range_low, die_low + access_pitch);
      int32_t legal_high = std::min(range_high, die_high - access_pitch);
      int32_t start = track_offset + FPUTIL.alignUp(legal_low + pin_span / 2 - track_offset, track_pitch);
      int32_t end = track_offset + FPUTIL.alignDown(legal_high - pin_span / 2 - track_offset, track_pitch);
      if (start > end) {
        return;
      }

      int64_t center2 = static_cast<int64_t>(die_low) + die_high;
      int32_t anchor = track_offset + FPUTIL.alignNearest(static_cast<int32_t>(center2 / 2) - track_offset, track_pitch);
      anchor = std::max(start, std::min(end, anchor));
      int32_t spacing = spacing_multiplier * track_pitch;
      while (anchor - spacing >= start) {
        anchor -= spacing;
      }

      for (int64_t coord = anchor; coord <= end; coord += spacing) {
        slot_list.push_back(
            {static_cast<int64_t>(perpendicular_span) + std::abs(2 * coord - center2), edge_order, edge_type, static_cast<int32_t>(coord)});
      }
    };

    add_edge_slots(IOEdgeType::kLeft, 0, core.get_ll_y(), core.get_ur_y(), die.get_ll_y(), die.get_ur_y(), horizontal_width,
                   vertical_pitch, horizontal_offset, horizontal_pitch, die.get_width());
    add_edge_slots(IOEdgeType::kRight, 1, core.get_ll_y(), core.get_ur_y(), die.get_ll_y(), die.get_ur_y(), horizontal_width,
                   vertical_pitch, horizontal_offset, horizontal_pitch, die.get_width());
    add_edge_slots(IOEdgeType::kBottom, 2, core.get_ll_x(), core.get_ur_x(), die.get_ll_x(), die.get_ur_x(), vertical_width,
                   horizontal_pitch, vertical_offset, vertical_pitch, die.get_height());
    add_edge_slots(IOEdgeType::kTop, 3, core.get_ll_x(), core.get_ur_x(), die.get_ll_x(), die.get_ur_x(), vertical_width,
                   horizontal_pitch, vertical_offset, vertical_pitch, die.get_height());
    std::sort(slot_list.begin(), slot_list.end(), [](const PinSlot& lhs, const PinSlot& rhs) {
      return std::tie(lhs.cost, lhs.edge_order, lhs.coord) < std::tie(rhs.cost, rhs.edge_order, rhs.coord);
    });
    return slot_list;
  };

  int32_t spacing_multiplier = 2;
  std::vector<PinSlot> slot_list = build_slots(spacing_multiplier);
  if (slot_list.size() < io_pin_list.size()) {
    spacing_multiplier = 1;
    slot_list = build_slots(spacing_multiplier);
  }
  if (slot_list.size() < io_pin_list.size()) {
    FPLOG.error(Loc::current(), "IO pins exceed the total legal edge capacity at the minimum track pitch spacing.");
    return;
  }

  slot_list.resize(io_pin_list.size());
  std::sort(slot_list.begin(), slot_list.end(), [](const PinSlot& lhs, const PinSlot& rhs) {
    return std::tie(lhs.edge_order, lhs.coord) < std::tie(rhs.edge_order, rhs.coord);
  });

  for (size_t pin_idx = 0; pin_idx < io_pin_list.size(); ++pin_idx) {
    const PinSlot& slot = slot_list[pin_idx];
    bool vertical_edge = slot.edge_type == IOEdgeType::kLeft || slot.edge_type == IOEdgeType::kRight;
    int32_t x = vertical_edge ? (slot.edge_type == IOEdgeType::kLeft ? die.get_ll_x() + horizontal_depth / 2
                                                                    : die.get_ur_x() - horizontal_depth / 2)
                              : slot.coord;
    int32_t y = vertical_edge ? slot.coord
                              : (slot.edge_type == IOEdgeType::kBottom ? die.get_ll_y() + vertical_depth / 2
                                                                       : die.get_ur_y() - vertical_depth / 2);
    addIOPinPort(io_pin_list[pin_idx], slot.edge_type, x, y, vertical_edge ? horizontal_width : vertical_width,
                 vertical_edge ? horizontal_depth : vertical_depth, vertical_edge ? horizontal_layer_name : vertical_layer_name);
  }
}

int32_t IOPlacer::getLayerMinWidth(std::string layer_name)
{
  Database& database = FPDM.getDatabase();
  auto iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
  if (iter == database.get_routing_layer_name_to_idx_map().end()) {
    return 0;
  }
  return database.get_routing_layer_list()[iter->second].get_min_width();
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

void IOPlacer::placeIOPinsOnEdge(IOEdgeType edge_type, std::vector<IOPin>& io_pin_list, int32_t& io_pin_idx, int32_t edge_pin_num, std::string layer_name,
                                 int32_t width, int32_t depth, int32_t access_pitch, int32_t track_offset, int32_t track_pitch)
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
        y = getAlongCoord(core.get_ll_y(), core.get_ur_y(), die.get_ll_y(), die.get_ur_y(), width, access_pitch, side_pin_num, side_pin_idx, track_offset,
                          track_pitch);
        break;
      case IOEdgeType::kRight:
        x = die.get_ur_x() - depth / 2;
        y = getAlongCoord(core.get_ll_y(), core.get_ur_y(), die.get_ll_y(), die.get_ur_y(), width, access_pitch, side_pin_num, side_pin_idx, track_offset,
                          track_pitch);
        break;
      case IOEdgeType::kBottom:
        x = getAlongCoord(core.get_ll_x(), core.get_ur_x(), die.get_ll_x(), die.get_ur_x(), width, access_pitch, side_pin_num, side_pin_idx, track_offset,
                          track_pitch);
        y = die.get_ll_y() + depth / 2;
        break;
      case IOEdgeType::kTop:
        x = getAlongCoord(core.get_ll_x(), core.get_ur_x(), die.get_ll_x(), die.get_ur_x(), width, access_pitch, side_pin_num, side_pin_idx, track_offset,
                          track_pitch);
        y = die.get_ur_y() - depth / 2;
        break;
      default:
        return;
    }
    addIOPinPort(io_pin_list[io_pin_idx++], edge_type, x, y, width, depth, layer_name);
  }
}

int32_t IOPlacer::getAlongCoord(int32_t range_low, int32_t range_high, int32_t die_low, int32_t die_high, int32_t pin_span, int32_t access_pitch,
                                int32_t side_pin_num, int32_t pin_idx, int32_t track_offset, int32_t track_pitch)
{
  int32_t legal_low = std::max(range_low, die_low + access_pitch);
  int32_t legal_high = std::min(range_high, die_high - access_pitch);
  auto align_up = [track_offset, track_pitch](int32_t value) { return track_offset + FPUTIL.alignUp(value - track_offset, track_pitch); };
  auto align_down = [track_offset, track_pitch](int32_t value) { return track_offset + FPUTIL.alignDown(value - track_offset, track_pitch); };
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
