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
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <vector>

#include "DataManager.hpp"
#include "DieBuilder.hpp"
#include "IOCellPlacer.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ifp {

namespace {

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

  Config& config = FPDM.getConfig();
  if (config.io_placer_mode == IOPlacerMode::kIOCell) {
    placeIOCell();
  } else {
    buildNetIOPinIndex();
    resetIOPinPlacement();
    placeIOPin();
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void IOPlacer::placeAuto()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Config& config = FPDM.getConfig();
  if (config.io_placer_mode == IOPlacerMode::kIOCell) {
    placeIOCell();
    FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
    return;
  }
  buildNetIOPinIndex();
  resetIOPinPlacement();
  if (!config.io_pin_layer_name_list.empty()) {
    autoPlacePins(config.io_pin_layer_name_list);
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

bool IOPlacer::planDieForIO()
{
  Config& config = FPDM.getConfig();
  if (config.io_cell_mode != PlacementMode::kAuto) {
    FPLOG.info(Loc::current(), "IO capacity auto-resize applies to io_cell.mode=auto only; skipping.");
    return true;
  }
  Database& database = FPDM.getDatabase();
  if (database.get_micron_dbu() <= 0) {
    FPLOG.error(Loc::current(), "Cannot check IO capacity with an invalid micron DBU.");
    return false;
  }
  const double dbu = database.get_micron_dbu();

  if (_io_cell_placer == nullptr) {
    _io_cell_placer = std::make_unique<IOCellPlacer>(config);
  }
  IOCellPlacer& placer = *_io_cell_placer;
  IOCapacityReport report = placer.checkCapacity();

  std::ofstream report_file;
  if (!config.ip_temp_directory_path.empty()) {
    report_file.open(config.ip_temp_directory_path + "io_capacity.rpt");
  }
  auto write_report = [&](const IOCapacityReport& current) {
    if (!report_file.is_open()) {
      return;
    }
    const double die_width = (database.get_die().get_ur_x() - database.get_die().get_ll_x()) / dbu;
    const double die_height = (database.get_die().get_ur_y() - database.get_die().get_ll_y()) / dbu;
    report_file << "IO capacity check\n";
    report_file << "die " << die_width << " x " << die_height << " um\n";
    for (const auto& side : current.sides) {
      report_file << side.edge_name << " (DBU): line=" << side.line_length << " power=" << side.power_length
                  << " signal=" << side.signal_length << " free=" << side.free_length << " deficit=" << side.deficit;
      if (!side.error.empty()) {
        report_file << " error=" << side.error;
      }
      report_file << "\n";
    }
    report_file << "capacity: " << (current.fits ? "PASS" : "FAIL") << "\n";
    report_file.flush();
  };

  // One planning pass normally suffices; the extra attempts only absorb
  // site-alignment rounding after a die_util scale.
  constexpr int32_t kMaxAttempts = 4;
  for (int32_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
    const double die_width = (database.get_die().get_ur_x() - database.get_die().get_ll_x()) / dbu;
    const double die_height = (database.get_die().get_ur_y() - database.get_die().get_ll_y()) / dbu;
    write_report(report);
    if (!report.error.empty()) {
      FPLOG.error(Loc::current(), report.error);
      return false;
    }
    FPLOG.info(Loc::current(), "IO capacity check (attempt ", attempt, "): ", report.fits ? "PASS" : "FAIL", " (die ",
               die_width, " x ", die_height, " um).");
    if (report.fits) {
      return true;
    }
    if (!config.io_cell_auto_resize) {
      FPLOG.error(Loc::current(), "IO cell capacity is insufficient and auto resize is disabled.");
      return false;
    }
    for (const auto& side : report.sides) {
      if (side.deficit > 0) {
        FPLOG.warn(Loc::current(), "  ", side.edge_name, ": line=", side.line_length, " required=", side.required_line,
                   " deficit=", side.deficit);
      }
    }
    if (attempt + 1 == kMaxAttempts) {
      break;
    }

    const double required_die_width = report.required_die_width_dbu > 0 ? report.required_die_width_dbu / dbu : die_width;
    const double required_die_height = report.required_die_height_dbu > 0 ? report.required_die_height_dbu / dbu : die_height;
    bool resized = false;

    if (config.die_mode == DieMode::kDieSize) {
      // Each axis grows only as much as its own sides require.
      const double new_width = std::max(die_width, required_die_width);
      const double new_height = std::max(die_height, required_die_height);
      if (new_width > die_width + 1e-9 || new_height > die_height + 1e-9) {
        config.die_width_micron = new_width;
        config.die_height_micron = new_height;
        resized = true;
        FPLOG.info(Loc::current(), "Resized die_size for IO capacity: ", new_width, " x ", new_height, " um.");
      }
    } else if (config.die_mode == DieMode::kDieUtil) {
      const Core& core = database.get_core();
      const double core_width = (core.get_ur_x() - core.get_ll_x()) / dbu;
      const double core_height = (core.get_ur_y() - core.get_ll_y()) / dbu;
      if (core_width <= 0.0 || core_height <= 0.0) {
        FPLOG.error(Loc::current(), "Cannot grow die_util with an invalid core size.");
        return false;
      }
      // Die = core + margins, so subtract the margins before scaling the core.
      const double margin_width = std::max(0.0, die_width - core_width);
      const double margin_height = std::max(0.0, die_height - core_height);
      const double required_core_width = std::max(0.0, required_die_width - margin_width);
      const double required_core_height = std::max(0.0, required_die_height - margin_height);
      double scale = 1.0;
      if (required_core_width > core_width) {
        scale = std::max(scale, required_core_width / core_width);
      }
      if (required_core_height > core_height) {
        scale = std::max(scale, required_core_height / core_height);
      }
      if (scale > 1.0 + 1e-9) {
        // Uniform scaling keeps the configured aspect ratio; utilization drops by scale^2.
        const double new_utilization = config.die_utilization / (scale * scale);
        if (new_utilization <= 0.0) {
          FPLOG.error(Loc::current(), "IO capacity requires an invalid die utilization ", new_utilization, ".");
          return false;
        }
        config.die_utilization = new_utilization;
        resized = true;
        FPLOG.info(Loc::current(), "Grew die_util uniformly by ", scale, "; utilization ", new_utilization, ".");
      }
    } else {
      FPLOG.error(Loc::current(), "IO cell capacity is insufficient and die_builder.mode is not set.");
      return false;
    }

    if (!resized) {
      break;
    }
    DieBuilder::initInst();
    FPDB.build();
    DieBuilder::destroyInst();
    report = placer.checkCapacity();
  }

  FPLOG.error(Loc::current(), "IO cell capacity is still insufficient after ", kMaxAttempts, " attempt(s).");
  return false;
}

void IOPlacer::placeIOCell()
{
  if (_io_cell_placer == nullptr) {
    _io_cell_placer = std::make_unique<IOCellPlacer>(FPDM.getConfig());
  }
  if (!_io_cell_placer->place()) {
    FPLOG.error(Loc::current(), "IO Cell placement failed.");
  }
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
    int32_t lower_half_width = width / 2;
    int32_t upper_half_width = width - lower_half_width;
    if (static_cast<int64_t>(along_coord) - lower_half_width < range_low
        || static_cast<int64_t>(along_coord) + upper_half_width > range_high) {
      FPLOG.error(Loc::current(), "IO pin '", pin_name, "' is outside the legal core-edge range at line ", line_num, "!");
    }

    int32_t depth = calculatePinDepth(edge_type, width, layer_name, vertical_edge ? vertical_layer_name : horizontal_layer_name);
    placement_list.push_back({pin_iter->second, line_num, edge_type, along_coord, width, depth, layer_name});
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
      int64_t first_low = static_cast<int64_t>(first.along_coord) - first.width / 2;
      int64_t first_high = first_low + first.width;
      int64_t second_low = static_cast<int64_t>(second.along_coord) - second.width / 2;
      int64_t second_high = second_low + second.width;
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

  struct AutoPinLayer
  {
    std::string name;
    std::string access_name;
    int32_t width = 0;
    int32_t pitch = 0;
    int32_t offset = 0;
  };

  std::vector<AutoPinLayer> horizontal_layers;
  std::vector<AutoPinLayer> vertical_layers;
  std::set<std::string> seen_layer_name_set;
  for (std::string& layer_name : layer_name_list) {
    std::map<std::string, int32_t>::iterator routing_layer_iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
    if (routing_layer_iter == database.get_routing_layer_name_to_idx_map().end()) {
      FPLOG.warn(Loc::current(), "IO layer '", layer_name, "' is not a routing layer; ignored.");
      continue;
    }
    if (!seen_layer_name_set.insert(layer_name).second) {
      continue;
    }

    RoutingLayer& routing_layer = database.get_routing_layer_list()[routing_layer_iter->second];
    const int32_t width = routing_layer.get_min_width();
    const int32_t pitch = getTrackPitch(layer_name);
    if (width <= 0 || pitch <= 0) {
      FPLOG.warn(Loc::current(), "IO layer '", layer_name, "' has invalid width ", width, " or track pitch ", pitch, "; ignored.");
      continue;
    }

    AutoPinLayer auto_pin_layer;
    auto_pin_layer.name = routing_layer.get_name();
    auto_pin_layer.width = width;
    auto_pin_layer.pitch = pitch;
    auto_pin_layer.offset = getTrackOffset(layer_name);
    if (routing_layer.get_prefer_direction() == Direction::kHorizontal) {
      horizontal_layers.push_back(auto_pin_layer);
    } else if (routing_layer.get_prefer_direction() == Direction::kVertical) {
      vertical_layers.push_back(auto_pin_layer);
    } else {
      FPLOG.warn(Loc::current(), "IO layer '", layer_name, "' has no preferred routing direction; ignored.");
    }
  }

  if (horizontal_layers.empty() || vertical_layers.empty()) {
    FPLOG.warn(Loc::current(), "Auto IO pin placement requires at least one horizontal and one vertical routing layer.");
    return;
  }
  // The access layer only determines the pin depth; use the first perpendicular
  // layer so every configured pin layer keeps a legal, track-aligned geometry.
  for (AutoPinLayer& layer : horizontal_layers) {
    layer.access_name = vertical_layers.front().name;
  }
  for (AutoPinLayer& layer : vertical_layers) {
    layer.access_name = horizontal_layers.front().name;
  }

  std::vector<IOPin>& io_pin_list = database.get_io_pin_list();
  int32_t io_pin_num = static_cast<int32_t>(io_pin_list.size());
  if (io_pin_num == 0) {
    return;
  }

  Die& die = database.get_die();
  Core& core = database.get_core();

  struct EdgeRange
  {
    int32_t first;
    int32_t last;
  };

  auto get_edge_range = [&](int32_t range_low, int32_t range_high, int32_t die_low, int32_t die_high, int32_t pin_span,
                            int32_t access_pitch, int32_t track_offset, int32_t track_pitch) {
    int32_t legal_low = std::max(range_low, die_low + access_pitch);
    int32_t legal_high = std::min(range_high, die_high - access_pitch);
    int32_t lower_half_span = pin_span / 2;
    int32_t upper_half_span = pin_span - lower_half_span;
    int32_t first = track_offset + FPUTIL.alignUp(legal_low + lower_half_span - track_offset, track_pitch);
    int32_t last = track_offset + FPUTIL.alignDown(legal_high - upper_half_span - track_offset, track_pitch);
    return EdgeRange{first, last};
  };

  auto build_edge_slots = [](const EdgeRange& range, int32_t track_pitch) {
    std::vector<int32_t> slots;
    for (int64_t coord = range.first; coord <= range.last; coord += track_pitch) {
      slots.push_back(static_cast<int32_t>(coord));
    }
    return slots;
  };

  auto get_candidate_layers = [&](bool vertical_edge) -> const std::vector<AutoPinLayer>& {
    if (vertical_edge) {
      return horizontal_layers;
    }
    return vertical_layers;
  };

  const std::array<IOEdgeType, 4> edge_type_list{IOEdgeType::kLeft, IOEdgeType::kRight, IOEdgeType::kBottom, IOEdgeType::kTop};
  std::array<std::vector<std::vector<int32_t>>, 4> edge_slots;
  std::array<int32_t, 4> slot_counts{};
  for (int32_t edge_idx = 0; edge_idx < static_cast<int32_t>(edge_type_list.size()); ++edge_idx) {
    const IOEdgeType edge_type = edge_type_list[edge_idx];
    const bool vertical_edge = edge_type == IOEdgeType::kLeft || edge_type == IOEdgeType::kRight;
    const std::vector<AutoPinLayer>& candidate_layers = get_candidate_layers(vertical_edge);
    const int32_t access_pitch = vertical_edge ? vertical_layers.front().pitch : horizontal_layers.front().pitch;
    for (const AutoPinLayer& layer : candidate_layers) {
      EdgeRange range;
      if (vertical_edge) {
        range = get_edge_range(core.get_ll_y(), core.get_ur_y(), die.get_ll_y(), die.get_ur_y(), layer.width, access_pitch, layer.offset,
                               layer.pitch);
      } else {
        range = get_edge_range(core.get_ll_x(), core.get_ur_x(), die.get_ll_x(), die.get_ur_x(), layer.width, access_pitch, layer.offset,
                               layer.pitch);
      }
      edge_slots[edge_idx].push_back(build_edge_slots(range, layer.pitch));
      slot_counts[edge_idx] += static_cast<int32_t>(edge_slots[edge_idx].back().size());
    }
  }

  int32_t total_slots = slot_counts[0] + slot_counts[1] + slot_counts[2] + slot_counts[3];
  if (total_slots <= 0) {
    return;
  }
  if (total_slots < io_pin_num) {
    FPLOG.error(Loc::current(), "IO pins exceed the total legal track capacity (", io_pin_num, " pins, ", total_slots, " slots).");
    return;
  }

  int32_t counts[4] = {0, 0, 0, 0};
  int32_t remaining = io_pin_num;
  for (int32_t idx = 0; idx < 3; ++idx) {
    counts[idx] = static_cast<int32_t>((static_cast<int64_t>(io_pin_num) * slot_counts[idx]) / total_slots);
    remaining -= counts[idx];
  }
  counts[3] = remaining;
  // The proportional split can leave more pins on the last edge than its
  // tracks can host when the edge lengths differ. Move that excess to edges
  // with free capacity without changing the normal distribution.
  if (counts[3] > slot_counts[3]) {
    int32_t excess = counts[3] - slot_counts[3];
    counts[3] = slot_counts[3];
    int32_t edge_cursor = 0;
    while (excess > 0) {
      bool moved = false;
      for (int32_t offset = 0; offset < 4 && excess > 0; ++offset) {
        const int32_t edge_idx = (edge_cursor + offset) % 4;
        if (counts[edge_idx] < slot_counts[edge_idx]) {
          ++counts[edge_idx];
          --excess;
          edge_cursor = (edge_idx + 1) % 4;
          moved = true;
          break;
        }
      }
      if (!moved) {
        FPLOG.error(Loc::current(), "IO pins exceed the legal track capacity on the edges.");
        return;
      }
    }
  }

  constexpr int32_t max_gap_tracks = 10;
  int32_t pin_idx = 0;
  auto place_edge = [&](IOEdgeType edge_type, const std::vector<int32_t>& slots, int32_t count, int32_t width,
                        const std::string& pin_layer, const std::string& access_layer) {
    if (count <= 0) {
      return;
    }
    if (static_cast<int32_t>(slots.size()) < count) {
      FPLOG.error(Loc::current(), "IO pins exceed the legal track capacity on an edge.");
      return;
    }

    std::vector<int32_t> selected;
    selected.reserve(count);
    if (count == 1) {
      selected.push_back(slots[slots.size() / 2]);
    } else {
      int32_t slot_count = static_cast<int32_t>(slots.size());
      int32_t span = static_cast<int32_t>(std::min<int64_t>(slot_count - 1, static_cast<int64_t>(count - 1) * max_gap_tracks));
      int32_t start = (slot_count - 1 - span) / 2;
      int32_t base = span / (count - 1);
      int32_t remainder = span % (count - 1);
      int32_t index = start;
      for (int32_t idx = 0; idx < count; ++idx) {
        selected.push_back(slots[index]);
        if (idx + 1 < count) {
          index += base + (idx < remainder ? 1 : 0);
        }
      }
    }

    bool vertical_edge = edge_type == IOEdgeType::kLeft || edge_type == IOEdgeType::kRight;
    for (int32_t coord : selected) {
      int32_t depth = calculatePinDepth(edge_type, width, pin_layer, access_layer);
      int32_t x = vertical_edge ? (edge_type == IOEdgeType::kLeft ? die.get_ll_x() + depth / 2 : die.get_ur_x() - depth / 2) : coord;
      int32_t y = vertical_edge ? coord : (edge_type == IOEdgeType::kBottom ? die.get_ll_y() + depth / 2 : die.get_ur_y() - depth / 2);
      addIOPinPort(io_pin_list[pin_idx++], edge_type, x, y, width, depth, pin_layer);
    }
  };

  for (int32_t edge_idx = 0; edge_idx < static_cast<int32_t>(edge_type_list.size()); ++edge_idx) {
    const IOEdgeType edge_type = edge_type_list[edge_idx];
    const bool vertical_edge = edge_type == IOEdgeType::kLeft || edge_type == IOEdgeType::kRight;
    const std::vector<AutoPinLayer>& candidate_layers = get_candidate_layers(vertical_edge);

    std::vector<int32_t> layer_counts(candidate_layers.size(), 0);
    int32_t remaining_count = counts[edge_idx];
    int32_t layer_cursor = 0;
    while (remaining_count > 0) {
      bool progress = false;
      for (size_t offset = 0; offset < candidate_layers.size() && remaining_count > 0; ++offset) {
        const size_t layer_idx = (static_cast<size_t>(layer_cursor) + offset) % candidate_layers.size();
        if (layer_counts[layer_idx] < static_cast<int32_t>(edge_slots[edge_idx][layer_idx].size())) {
          ++layer_counts[layer_idx];
          --remaining_count;
          layer_cursor = static_cast<int32_t>((layer_idx + 1) % candidate_layers.size());
          progress = true;
          break;
        }
      }
      if (!progress) {
        FPLOG.error(Loc::current(), "IO pins exceed the legal track capacity on an edge.");
        return;
      }
    }

    for (size_t layer_idx = 0; layer_idx < candidate_layers.size(); ++layer_idx) {
      if (layer_counts[layer_idx] == 0) {
        continue;
      }
      const AutoPinLayer& layer = candidate_layers[layer_idx];
      place_edge(edge_type, edge_slots[edge_idx][layer_idx], layer_counts[layer_idx], layer.width, layer.name, layer.access_name);
    }
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

int32_t IOPlacer::getLayerMinArea(std::string layer_name)
{
  Database& database = FPDM.getDatabase();
  auto iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
  if (iter == database.get_routing_layer_name_to_idx_map().end()) {
    return 0;
  }
  return database.get_routing_layer_list()[iter->second].get_min_area();
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

int32_t IOPlacer::calculatePinDepth(IOEdgeType edge_type, int32_t width, const std::string& pin_layer_name,
                                    const std::string& access_layer_name)
{
  bool x_depth = edge_type == IOEdgeType::kLeft || edge_type == IOEdgeType::kRight;
  if (!x_depth && edge_type != IOEdgeType::kBottom && edge_type != IOEdgeType::kTop) {
    FPLOG.error(Loc::current(), "Cannot calculate IO pin depth for an invalid edge!");
  }
  int64_t pitch = getTrackPitch(access_layer_name);
  int64_t offset = getTrackOffset(access_layer_name);
  if (width <= 0 || pitch <= 0) {
    FPLOG.error(Loc::current(), "IO pin width and access track pitch must be positive for layer '", access_layer_name, "'!");
  }

  const Die& die = FPDM.getDatabase().get_die();
  int64_t low = x_depth ? die.get_ll_x() : die.get_ll_y();
  int64_t high = x_depth ? die.get_ur_x() : die.get_ur_y();
  bool increasing = edge_type == IOEdgeType::kLeft || edge_type == IOEdgeType::kBottom;
  int64_t boundary = increasing ? low : high;
  int64_t half_width = (static_cast<int64_t>(width) + 1) / 2;
  int64_t min_area = getLayerMinArea(pin_layer_name);
  int64_t min_area_depth = min_area > 0 ? (min_area + width - 1) / width : 0;
  // Reach the nearest legal access track, keep a half-width margin inside the pin,
  // and extend the pin only as far as the layer minimum-area rule requires.
  int64_t min_distance = std::max(half_width, min_area_depth - half_width);
  int64_t target = boundary + (increasing ? min_distance : -min_distance);
  int64_t remainder = (target - offset) % pitch;
  if (remainder < 0) {
    remainder += pitch;
  }
  int64_t access_coord = increasing ? target + (pitch - remainder) % pitch : target - remainder;
  int64_t distance = increasing ? access_coord - boundary : boundary - access_coord;
  int64_t depth = distance + half_width;

  if (depth > std::numeric_limits<int32_t>::max()) {
    FPLOG.error(Loc::current(), "IO pin depth is outside the supported DBU range for pin layer '", pin_layer_name, "'!");
  }
  if (high <= low || depth > high - low) {
    FPLOG.error(Loc::current(), "Cannot fit IO pin depth ", depth, " inside die on edge ", static_cast<int>(edge_type),
                " with pin layer '", pin_layer_name, "' and access layer '", access_layer_name, "'!");
  }
  return static_cast<int32_t>(depth);
}

void IOPlacer::addIOPinPort(IOPin& io_pin, IOEdgeType edge_type, int32_t x, int32_t y, int32_t width, int32_t depth, std::string layer_name)
{
  io_pin.set_placed(true);
  io_pin.set_fixed(false);

  IOPort io_port;
  syncPinLocation(io_pin, io_port, x, y);

  Die& die = FPDM.getDatabase().get_die();
  int32_t lower_half_width = width / 2;
  int32_t upper_half_width = width - lower_half_width;
  int32_t shape_ll_x = x - lower_half_width;
  int32_t shape_ll_y = y - lower_half_width;
  int32_t shape_ur_x = x + upper_half_width;
  int32_t shape_ur_y = y + upper_half_width;
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

void IOPlacer::buildNetIOPinIndex()
{
  _io_pin_to_net_pin_map.clear();
  for (Net& net : FPDM.getDatabase().get_net_list()) {
    for (NetPin& net_pin : net.get_net_pin_list()) {
      if (net_pin.get_io()) {
        _io_pin_to_net_pin_map[net_pin.get_pin_name()].push_back(&net_pin);
      }
    }
  }
}

void IOPlacer::updateNetIOPin(IOPin& io_pin)
{
  auto iter = _io_pin_to_net_pin_map.find(io_pin.get_name());
  if (iter == _io_pin_to_net_pin_map.end()) {
    return;
  }
  for (NetPin* net_pin : iter->second) {
    net_pin->set_coord(io_pin.get_x(), io_pin.get_y());
    net_pin->set_placed(io_pin.get_placed());
  }
}

// private

IOPlacer::~IOPlacer() = default;

IOPlacer* IOPlacer::_ip_instance = nullptr;

}  // namespace ifp
