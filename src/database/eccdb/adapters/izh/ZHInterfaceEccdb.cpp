// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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

#include "ZHInterface.hpp"

#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/index/predicates.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/iterator/function_output_iterator.hpp>

#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/site/model/SiteComponents.h"
#include "service/geometry/GeometryEditService.h"
#include "service/geometry/GeometryQueryService.h"
#include "service/instance/InstanceService.h"
#include "tech/TechStore.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace izh {
namespace {

struct Box
{
  int64_t ll_x = 0;
  int64_t ll_y = 0;
  int64_t ur_x = 0;
  int64_t ur_y = 0;
};

using PlacementIndexPoint = boost::geometry::model::point<int64_t, 2, boost::geometry::cs::cartesian>;
using PlacementIndexBox = boost::geometry::model::box<PlacementIndexPoint>;
using PlacementIndexValue = std::pair<PlacementIndexBox, uint32_t>;
using PlacementIndex = boost::geometry::index::rtree<PlacementIndexValue, boost::geometry::index::quadratic<16>>;

PlacementIndexBox indexBox(const Box& bounds) noexcept
{
  return PlacementIndexBox{PlacementIndexPoint{bounds.ll_x, bounds.ll_y}, PlacementIndexPoint{bounds.ur_x, bounds.ur_y}};
}

struct FillerMaster
{
  eccdb::LibraryCellMasterId id;
  std::string name;
  int32_t site_count = 0;
};

struct FillerPlacement
{
  eccdb::LibraryCellMasterId master;
  std::string name;
  eccdb::Point origin;
  eccdb::DesignOrientation orientation = eccdb::DesignOrientation::kN;
};

constexpr std::size_t kMaxNativeMetalFillsPerLayer = 64u;

std::string configString(const std::map<std::string, std::any>& config, std::string_view key)
{
  const auto found = config.find(std::string{key});
  if (found == config.end()) {
    return {};
  }
  if (const auto* value = std::any_cast<std::string>(&found->second); value != nullptr) {
    return *value;
  }
  throw std::invalid_argument("iZH native option '" + std::string{key} + "' must be a string");
}

int32_t configPositiveInt(const std::map<std::string, std::any>& config, std::string_view key, int32_t default_value)
{
  const auto found = config.find(std::string{key});
  if (found == config.end()) {
    return default_value;
  }
  if (const auto* value = std::any_cast<int32_t>(&found->second); value != nullptr) {
    if (*value <= 0) {
      throw std::invalid_argument("iZH native option '" + std::string{key} + "' must be positive");
    }
    return *value;
  }
  throw std::invalid_argument("iZH native option '" + std::string{key} + "' must be an integer");
}

std::vector<std::string> splitNames(std::string names)
{
  for (char& character : names) {
    if (std::string_view{" \t\r\n,;{}[]"}.find(character) != std::string_view::npos) {
      character = ' ';
    }
  }

  std::vector<std::string> result;
  std::size_t begin = 0;
  while (begin < names.size()) {
    begin = names.find_first_not_of(' ', begin);
    if (begin == std::string::npos) {
      break;
    }
    const auto end = names.find(' ', begin);
    result.push_back(names.substr(begin, end - begin));
    begin = end;
  }
  return result;
}

bool isCoreFiller(const eccdb::LibraryCellMaster& master) noexcept
{
  return master.core_filler || master.type == eccdb::LibraryCellMasterType::kCoreSpacer;
}

bool isPlaced(eccdb::DesignPlacementStatus status) noexcept
{
  return status != eccdb::DesignPlacementStatus::kUnplaced;
}

bool blocksPostPlacementFiller(const eccdb::DesignBlockage& blockage) noexcept
{
  if (blockage.kind != eccdb::DesignBlockageKind::kPlacement) {
    return false;
  }

  // DEF defines SOFT and PARTIAL placement blockages for initial placement.
  // Filler insertion is a later placement operation, so only a hard placement
  // blockage removes row sites here. PUSHDOWN and COMPONENT only affect where
  // the absolute blockage geometry originates; both retain hard semantics.
  constexpr uint32_t kInitialPlacementOnly = eccdb::DesignBlockageFlag::kSoft | eccdb::DesignBlockageFlag::kHasPartial;
  return (blockage.flags & kInitialPlacementOnly) == 0u;
}

Box instanceBounds(const eccdb::DesignInstance& instance, const eccdb::LibraryCellMaster& master)
{
  const bool rotated = eccdb::isQuarterTurnOrientation(instance.orientation);
  const int64_t width = rotated ? static_cast<int64_t>(master.height) : static_cast<int64_t>(master.width);
  const int64_t height = rotated ? static_cast<int64_t>(master.width) : static_cast<int64_t>(master.height);
  return {.ll_x = instance.origin.x, .ll_y = instance.origin.y, .ur_x = static_cast<int64_t>(instance.origin.x) + width,
          .ur_y = static_cast<int64_t>(instance.origin.y) + height};
}

Box rectangleBounds(eccdb::Rect rectangle) noexcept
{
  rectangle = rectangle.normalized();
  return {.ll_x = rectangle.ll_x, .ll_y = rectangle.ll_y, .ur_x = rectangle.ur_x, .ur_y = rectangle.ur_y};
}

void markBlockedSites(std::vector<bool>& available,
                      int64_t row_x,
                      int64_t row_y,
                      int64_t site_width,
                      int64_t site_height,
                      const Box& blockage)
{
  const int64_t row_ur_x = row_x + site_width * static_cast<int64_t>(available.size());
  const int64_t row_ur_y = row_y + site_height;
  if (blockage.ur_x <= row_x || blockage.ll_x >= row_ur_x || blockage.ur_y <= row_y || blockage.ll_y >= row_ur_y) {
    return;
  }

  const int64_t clipped_ll_x = std::max(blockage.ll_x, row_x);
  const int64_t clipped_ur_x = std::min(blockage.ur_x, row_ur_x);
  const auto begin = static_cast<std::size_t>((clipped_ll_x - row_x) / site_width);
  const auto end = static_cast<std::size_t>((clipped_ur_x - row_x + site_width - 1) / site_width);
  for (std::size_t index = begin; index < std::min(end, available.size()); ++index) {
    available[index] = false;
  }
}

std::vector<FillerMaster> collectFillerMasters(const eccdb::LibraryStore& library,
                                                int32_t site_width,
                                                const std::vector<std::string>& requested)
{
  std::vector<FillerMaster> result;
  const auto add_master = [&](eccdb::LibraryCellMasterId id) {
    const auto& master = library.cellMasterStorage().cellMaster(id);
    if (!isCoreFiller(master)) {
      throw std::invalid_argument("iZH native filler master is not a core filler: " + master.name);
    }
    if (master.width == 0u || master.width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
        || static_cast<int32_t>(master.width) % site_width != 0) {
      throw std::invalid_argument("iZH native filler master width is not aligned to the row site: " + master.name);
    }
    result.push_back({.id = id, .name = master.name, .site_count = static_cast<int32_t>(master.width) / site_width});
  };

  if (requested.empty()) {
    for (const auto id : library.cellMasterStorage().cellMasters()) {
      if (isCoreFiller(library.cellMasterStorage().cellMaster(id))) {
        add_master(id);
      }
    }
  } else {
    for (const auto& name : requested) {
      const auto id = library.cellMasterStorage().findCellMaster(name);
      if (!id) {
        throw std::invalid_argument("iZH native filler master does not exist: " + name);
      }
      add_master(id);
    }
  }
  std::sort(result.begin(), result.end(), [](const FillerMaster& lhs, const FillerMaster& rhs) {
    if (lhs.site_count != rhs.site_count) {
      return lhs.site_count > rhs.site_count;
    }
    return lhs.name < rhs.name;
  });
  if (result.empty()) {
    throw std::invalid_argument("iZH native filler insertion requires at least one core filler master");
  }
  return result;
}

std::string nextFillerName(const eccdb::DesignNetlistStorage& netlist, const std::string& master_name, uint32_t& sequence)
{
  for (;;) {
    const auto candidate = master_name + "_" + std::to_string(sequence++);
    if (!netlist.findInstance(candidate)) {
      return candidate;
    }
  }
}

int32_t effectiveWidth(const eccdb::TechRoutingLayer& layer)
{
  return layer.min_width > 0 ? layer.min_width : layer.width;
}

int32_t ceilDivPositive(int64_t dividend, int32_t divisor)
{
  if (divisor <= 0 || dividend < 0) {
    throw std::invalid_argument("iZH native metal fill received invalid routing pitch");
  }
  const int64_t result = (dividend + divisor - 1) / divisor;
  if (result > std::numeric_limits<int32_t>::max()) {
    throw std::overflow_error("iZH native metal fill track count exceeds int32");
  }
  return static_cast<int32_t>(result);
}

std::vector<int32_t> fallbackTracks(const eccdb::Rect& die, bool horizontal, int32_t start, int32_t pitch, int32_t width)
{
  const int32_t lower = horizontal ? die.ll_y : die.ll_x;
  const int32_t upper = horizontal ? die.ur_y : die.ur_x;
  const int32_t lower_half = width / 2;
  const int32_t upper_half = width - lower_half;
  if (pitch <= 0 || upper - lower <= width) {
    return {};
  }
  const int64_t first = start >= lower + lower_half
                            ? start
                            : static_cast<int64_t>(start) + static_cast<int64_t>(ceilDivPositive(static_cast<int64_t>(lower + lower_half) - start, pitch)) * pitch;
  std::vector<int32_t> tracks;
  for (int64_t coordinate = first; coordinate + upper_half <= upper && tracks.size() < kMaxNativeMetalFillsPerLayer * 8u; coordinate += pitch) {
    if (coordinate >= std::numeric_limits<int32_t>::min() && coordinate <= std::numeric_limits<int32_t>::max()) {
      tracks.push_back(static_cast<int32_t>(coordinate));
    }
  }
  return tracks;
}

bool hasGeometry(const eccdb::GeometryQueryService& query, eccdb::Rect rectangle, eccdb::TechRoutingLayerId layer)
{
  eccdb::GeometryQueryFilter filter;
  filter.layer = eccdb::TechLayerId{layer.entity()}.packed();
  return query.hasShapes(rectangle, filter);
}

struct NativeAntennaRules
{
  double gate_area = 0.0;
  std::map<std::string, double> max_area_ratio;
  std::string source_file;
};

double parsePositiveDouble(std::string_view text, std::string_view description)
{
  std::size_t consumed = 0;
  double value = 0.0;
  try {
    value = std::stod(std::string{text}, &consumed);
  } catch (const std::exception&) {
    throw std::invalid_argument("iZH native antenna rule has an invalid " + std::string{description} + ": " + std::string{text});
  }
  if (consumed != text.size() || !std::isfinite(value) || value <= 0.0) {
    throw std::invalid_argument("iZH native antenna rule has an invalid " + std::string{description} + ": " + std::string{text});
  }
  return value;
}

NativeAntennaRules loadAntennaRules(const std::map<std::string, std::any>& config)
{
  const auto found = config.find("-antenna_rule_file");
  if (found == config.end()) {
    throw std::invalid_argument("iZH native antenna checking requires -antenna_rule_file (GATE_AREA/LAYER provider input)");
  }
  const auto* file = std::any_cast<std::string>(&found->second);
  if (file == nullptr || file->empty()) {
    throw std::invalid_argument("iZH native antenna option '-antenna_rule_file' must be a non-empty string");
  }

  NativeAntennaRules rules;
  rules.source_file = *file;
  std::ifstream input(*file);
  if (!input) {
    throw std::invalid_argument("iZH native antenna rule file cannot be opened: " + *file);
  }
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (const auto comment = line.find('#'); comment != std::string::npos) {
      line.erase(comment);
    }
    std::istringstream tokens(line);
    std::string kind;
    if (!(tokens >> kind)) {
      continue;
    }
    if (kind == "GATE_AREA") {
      std::string value;
      std::string trailing;
      if (!(tokens >> value) || (tokens >> trailing)) {
        throw std::invalid_argument("iZH native antenna rule line " + std::to_string(line_number)
                                    + " must be 'GATE_AREA <positive-area>'");
      }
      if (rules.gate_area != 0.0) {
        throw std::invalid_argument("iZH native antenna rule repeats GATE_AREA at line " + std::to_string(line_number));
      }
      rules.gate_area = parsePositiveDouble(value, "GATE_AREA");
      continue;
    }
    if (kind == "LAYER") {
      std::string name;
      std::string ratio;
      std::string trailing;
      if (!(tokens >> name >> ratio) || (tokens >> trailing) || name.empty()) {
        throw std::invalid_argument("iZH native antenna rule line " + std::to_string(line_number)
                                    + " must be 'LAYER <routing-layer> <positive-ratio>'");
      }
      const auto [unused, inserted] = rules.max_area_ratio.emplace(name, parsePositiveDouble(ratio, "layer ratio"));
      if (!inserted) {
        throw std::invalid_argument("iZH native antenna rule repeats layer '" + name + "'");
      }
      continue;
    }
    throw std::invalid_argument("iZH native antenna rule line " + std::to_string(line_number)
                                + " has unknown directive '" + kind + "'");
  }
  if (rules.gate_area <= 0.0 || rules.max_area_ratio.empty()) {
    throw std::invalid_argument("iZH native antenna rule file must define GATE_AREA and at least one LAYER rule");
  }
  for (const auto& [key, value] : config) {
    if (key != "-antenna_rule_file" && key != "-config") {
      throw std::invalid_argument("iZH native antenna option is not supported: " + key);
    }
  }
  return rules;
}

double routeShapeArea(const eccdb::DesignRoutingStorage& routing,
                      eccdb::GeometryShapeId shape_id,
                      const eccdb::DesignRouteShape& shape,
                      const eccdb::DesignRouteShapeAttributes* attributes,
                      const eccdb::TechRoutingLayer& layer)
{
  return std::visit(
      [&](const auto& geometry) -> double {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
          const auto width = (shape.flags & eccdb::DesignWirePathFlag::kHasWidth) != 0u && attributes != nullptr
                                 ? attributes->width
                                 : effectiveWidth(layer);
          if (width <= 0) {
            throw std::invalid_argument("iZH native antenna route has no positive width on a routed layer");
          }
          const auto dx = static_cast<double>(geometry.end.position.x) - geometry.begin.position.x;
          const auto dy = static_cast<double>(geometry.end.position.y) - geometry.begin.position.y;
          return std::hypot(dx, dy) * static_cast<double>(width);
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRoutePatch>) {
          const auto width = static_cast<int64_t>(geometry.rectangle.ur_x) - geometry.rectangle.ll_x;
          const auto height = static_cast<int64_t>(geometry.rectangle.ur_y) - geometry.rectangle.ll_y;
          return width > 0 && height > 0 ? static_cast<double>(width) * static_cast<double>(height) : 0.0;
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
          const auto bounds = routing.shapeBoundingBox(shape_id);
          const auto width = static_cast<int64_t>(bounds.ur_x) - bounds.ll_x;
          const auto height = static_cast<int64_t>(bounds.ur_y) - bounds.ll_y;
          return width > 0 && height > 0 ? static_cast<double>(width) * static_cast<double>(height) : 0.0;
        } else {
          return 0.0;
        }
      },
      shape.geometry);
}

std::vector<eccdb::DesignFillInput> buildMetalFillInputs(eccdb::DatabaseContext& context,
                                                          const std::vector<eccdb::TechRoutingLayerId>& layers)
{
  const auto& design = context.design();
  if (!design.globalStorage().hasDieArea()) {
    throw std::invalid_argument("iZH native metal fill requires a die area");
  }
  const auto die = design.globalStorage().dieBounds();
  if (die.ur_x <= die.ll_x || die.ur_y <= die.ll_y) {
    throw std::invalid_argument("iZH native metal fill requires a non-empty die area");
  }

  const auto* technology = context.technology();
  if (technology == nullptr) {
    throw std::logic_error("iZH native metal fill requires a TechStore");
  }
  const auto& floorplan = design.floorplanStorage();
  eccdb::GeometryQueryService query{context};
  std::vector<eccdb::DesignFillInput> result;

  for (const auto layer_id : layers) {
    const auto& layer = technology->routingLayerStorage().routingLayer(layer_id);
    const int32_t width = effectiveWidth(layer);
    if (width <= 0) {
      throw std::invalid_argument("iZH native metal fill requires a positive routing width");
    }
    const bool horizontal = layer.direction == eccdb::TechRoutingDirection::kHorizontal;
    if (!horizontal && layer.direction != eccdb::TechRoutingDirection::kVertical) {
      throw std::invalid_argument("iZH native metal fill only supports horizontal or vertical routing layers");
    }

    const auto axis = horizontal ? eccdb::DesignAxis::kY : eccdb::DesignAxis::kX;
    auto tracks = floorplan.trackCoordinates(layer_id, axis);
    if (tracks.empty()) {
      const int32_t start = horizontal ? layer.offset_y : layer.offset_x;
      const int32_t pitch = horizontal ? layer.pitch_y : layer.pitch_x;
      tracks = fallbackTracks(die, horizontal, start, pitch, width);
    }
    if (tracks.empty()) {
      throw std::invalid_argument("iZH native metal fill requires tracks or a positive layer pitch");
    }

    const int32_t pitch = horizontal ? layer.pitch_y : layer.pitch_x;
    const int32_t spacing = std::max(width, layer.fill_active_spacing);
    const std::size_t track_step = pitch > 0 ? static_cast<std::size_t>(std::max(1, ceilDivPositive(static_cast<int64_t>(width) + spacing, pitch))) : 1u;
    const int32_t lower_half = width / 2;
    const int32_t upper_half = width - lower_half;
    const int64_t raw_min_length = layer.area > 0 ? (layer.area + width - 1) / width : width;
    const int32_t min_length = static_cast<int32_t>(std::min<int64_t>(std::max<int64_t>(raw_min_length, width), std::numeric_limits<int32_t>::max()));

    eccdb::DesignFillInput input{.layer = eccdb::TechLayerId{layer_id.entity()}};
    for (std::size_t index = 0; index < tracks.size() && input.rectangles.size() < kMaxNativeMetalFillsPerLayer; index += track_step) {
      const int32_t track = tracks[index];
      eccdb::Rect rectangle;
      if (horizontal) {
        rectangle = {.ll_x = die.ll_x + spacing, .ll_y = track - lower_half, .ur_x = die.ur_x - spacing, .ur_y = track + upper_half};
      } else {
        rectangle = {.ll_x = track - lower_half, .ll_y = die.ll_y + spacing, .ur_x = track + upper_half, .ur_y = die.ur_y - spacing};
      }
      const int64_t length = horizontal ? static_cast<int64_t>(rectangle.ur_x) - rectangle.ll_x : static_cast<int64_t>(rectangle.ur_y) - rectangle.ll_y;
      if (rectangle.ll_x < die.ll_x || rectangle.ll_y < die.ll_y || rectangle.ur_x > die.ur_x || rectangle.ur_y > die.ur_y || length < min_length) {
        continue;
      }
      const eccdb::Rect clearance{.ll_x = rectangle.ll_x - spacing,
                                  .ll_y = rectangle.ll_y - spacing,
                                  .ur_x = rectangle.ur_x + spacing,
                                  .ur_y = rectangle.ur_y + spacing};
      if (!hasGeometry(query, clearance, layer_id)) {
        input.rectangles.push_back(rectangle);
      }
    }
    if (!input.rectangles.empty()) {
      result.push_back(std::move(input));
    }
  }
  return result;
}

}  // namespace

void ZHInterface::insertFillerFromEccdb(std::map<std::string, std::any>& config_map)
{
  if (_database_context == nullptr || _database_context->library() == nullptr) {
    throw std::logic_error("iZH native filler insertion requires a complete DatabaseContext");
  }
  auto& context = *_database_context;
  auto& design = context.design();
  const auto& library = *context.library();
  const auto min_filler_sites = configPositiveInt(config_map, "-min_filler_width", 1);
  const auto requested = splitNames(configString(config_map, "-filler"));
  const auto& floorplan = design.floorplanStorage();
  const auto rows = floorplan.rows();
  if (rows.empty()) {
    throw std::invalid_argument("iZH native filler insertion requires placement rows");
  }

  std::vector<FillerPlacement> placements;
  uint32_t sequence = 0;
  const auto& netlist = design.netlistStorage();
  std::vector<Box> placement_obstacles;
  netlist.forEachInstance([&](eccdb::DesignInstanceId,
                              const eccdb::DesignInstance& instance,
                              std::span<const eccdb::DesignInstanceTermId>) {
    if (!isPlaced(instance.placement_status)) {
      return;
    }
    const auto& master = library.cellMasterStorage().cellMaster(instance.master);
    placement_obstacles.push_back(instanceBounds(instance, master));
  });
  const auto& constraints = design.constraintStorage();
  for (const auto blockage_id : constraints.blockages()) {
    const auto& blockage = constraints.blockage(blockage_id);
    if (!blocksPostPlacementFiller(blockage)) {
      continue;
    }
    for (const auto rectangle_id : blockage.rectangles) {
      placement_obstacles.push_back(rectangleBounds(constraints.blockageRectangle(rectangle_id)));
    }
  }
  std::vector<PlacementIndexValue> placement_index_values;
  placement_index_values.reserve(placement_obstacles.size());
  for (uint32_t index = 0; index < placement_obstacles.size(); ++index) {
    placement_index_values.emplace_back(indexBox(placement_obstacles[index]), index);
  }
  const PlacementIndex placement_index{placement_index_values.begin(), placement_index_values.end()};

  std::unordered_map<int32_t, std::vector<FillerMaster>> masters_by_site_width;
  for (const auto row_id : rows) {
    const auto& row = floorplan.row(row_id);
    const auto& site = library.siteStorage().site(row.site);
    if (site.width <= 0 || site.height <= 0 || row.repeat_count_x == 0u) {
      throw std::invalid_argument("iZH native filler insertion found an invalid row site");
    }
    auto masters = masters_by_site_width.find(site.width);
    if (masters == masters_by_site_width.end()) {
      masters = masters_by_site_width.emplace(site.width, collectFillerMasters(library, site.width, requested)).first;
    }
    const auto repeat_y = std::max<uint32_t>(row.repeat_count_y, 1u);
    for (uint32_t y_index = 0; y_index < repeat_y; ++y_index) {
      const int64_t row_y = static_cast<int64_t>(row.origin.y) + static_cast<int64_t>(y_index) * row.step_y;
      std::vector<bool> available(row.repeat_count_x, true);
      const Box row_bounds{.ll_x = row.origin.x,
                           .ll_y = row_y,
                           .ur_x = static_cast<int64_t>(row.origin.x) + static_cast<int64_t>(site.width) * row.repeat_count_x,
                           .ur_y = row_y + site.height};
      // The R-tree may return a box merely touching this row. The canonical
      // half-open boundary rule remains in markBlockedSites, so selection is
      // an acceleration only and does not alter filler placement semantics.
      const auto mark_candidate = [&](const PlacementIndexValue& candidate) {
        markBlockedSites(available, row.origin.x, row_y, site.width, site.height, placement_obstacles[candidate.second]);
      };
      placement_index.query(boost::geometry::index::intersects(indexBox(row_bounds)), boost::make_function_output_iterator(mark_candidate));

      std::size_t begin = 0;
      while (begin < available.size()) {
        while (begin < available.size() && !available[begin]) {
          ++begin;
        }
        std::size_t end = begin;
        while (end < available.size() && available[end]) {
          ++end;
        }
        std::size_t cursor = begin;
        while (end > cursor && static_cast<int64_t>(end - cursor) >= min_filler_sites) {
          const int32_t free_sites = static_cast<int32_t>(end - cursor);
          const FillerMaster* chosen = nullptr;
          for (const auto& master : masters->second) {
            const int32_t remaining = free_sites - master.site_count;
            if (remaining >= 0 && (remaining == 0 || remaining >= min_filler_sites)) {
              chosen = &master;
              break;
            }
          }
          if (chosen == nullptr) {
            break;
          }
          placements.push_back({.master = chosen->id,
                                .name = nextFillerName(netlist, chosen->name, sequence),
                                .origin = {.x = static_cast<int32_t>(static_cast<int64_t>(row.origin.x) + cursor * site.width),
                                           .y = static_cast<int32_t>(row_y)},
                                .orientation = row.orientation});
          cursor += static_cast<std::size_t>(chosen->site_count);
        }
        begin = end;
      }
    }
  }

  std::vector<eccdb::DesignInstanceId> created;
  created.reserve(placements.size());
  eccdb::InstanceService instances{context};
  try {
    for (const auto& placement : placements) {
      created.push_back(instances.create(eccdb::DesignInstance{.name = placement.name,
                                                                 .master = placement.master,
                                                                 .origin = placement.origin,
                                                                 .orientation = placement.orientation,
                                                                 .placement_status = eccdb::DesignPlacementStatus::kPlaced,
                                                                 .source = eccdb::DesignInstanceSource::kDist}));
    }
  } catch (...) {
    for (auto it = created.rbegin(); it != created.rend(); ++it) {
      static_cast<void>(instances.destroy(*it));
    }
    throw;
  }
}

void ZHInterface::insertMetalFromEccdb(std::map<std::string, std::any>& config_map)
{
  if (_database_context == nullptr || _database_context->technology() == nullptr) {
    throw std::logic_error("iZH native metal fill requires a complete DatabaseContext");
  }
  auto& context = *_database_context;
  const auto& technology = *context.technology();
  const auto min_name = configString(config_map, "-min_fill_layer");
  const auto max_name = configString(config_map, "-max_fill_layer");

  std::vector<eccdb::TechRoutingLayerId> routing_layers;
  for (const auto generic : technology.layerSequence()) {
    const auto routing = eccdb::TechRoutingLayerId{generic.entity()};
    if (technology.routingLayerStorage().contains(routing)) {
      routing_layers.push_back(routing);
    }
  }
  if (routing_layers.empty()) {
    throw std::invalid_argument("iZH native metal fill requires routing layers");
  }

  const auto find_layer = [&](const std::string& name) {
    if (name.empty()) {
      return -1;
    }
    const auto generic = technology.findLayer(name);
    if (!generic) {
      throw std::invalid_argument("iZH native metal fill layer is not a routing layer: " + name);
    }
    const auto routing = eccdb::TechRoutingLayerId{generic.entity()};
    if (!technology.routingLayerStorage().contains(routing)) {
      throw std::invalid_argument("iZH native metal fill layer is not a routing layer: " + name);
    }
    const auto found = std::find(routing_layers.begin(), routing_layers.end(), routing);
    return static_cast<int32_t>(std::distance(routing_layers.begin(), found));
  };
  const int32_t first = min_name.empty() ? 0 : find_layer(min_name);
  const int32_t last = max_name.empty() ? static_cast<int32_t>(routing_layers.size()) - 1 : find_layer(max_name);
  if (first < 0 || last < first) {
    throw std::invalid_argument("iZH native metal fill layer range is invalid");
  }
  const std::vector<eccdb::TechRoutingLayerId> selected(routing_layers.begin() + first, routing_layers.begin() + last + 1);
  const auto inputs = buildMetalFillInputs(context, selected);

  std::vector<eccdb::DesignFillId> created;
  created.reserve(inputs.size());
  eccdb::GeometryEditService edits{context};
  try {
    for (auto input : inputs) {
      created.push_back(edits.createFill(std::move(input)));
    }
  } catch (...) {
    for (auto it = created.rbegin(); it != created.rend(); ++it) {
      static_cast<void>(edits.destroyFill(*it));
    }
    throw;
  }
}

void ZHInterface::checkAntennaFromEccdb(std::map<std::string, std::any>& config_map)
{
  if (_database_context == nullptr) {
    throw std::logic_error("iZH native antenna checking requires a DatabaseContext");
  }
  const auto rules = loadAntennaRules(config_map);
  const auto& design = _database_context->design();
  const auto& netlist = design.netlistStorage();
  const auto& routing = design.routingStorage();
  const auto* technology = _database_context->technology();
  if (technology == nullptr) {
    throw std::logic_error("iZH native antenna checking requires a TechStore");
  }

  NativeAntennaSummary summary;
  summary.rule_file = rules.source_file;
  constexpr std::size_t kMaxReportedViolations = 64u;
  netlist.forEachRegularNet([&](eccdb::DesignNetId net_id, const eccdb::DesignNet& net) {
    ++summary.net_count;
    const auto terminals = netlist.instanceTerms(net_id);
    if (terminals.empty()) {
      return;
    }
    ++summary.connected_net_count;
    const auto wires = routing.wires(net_id);
    summary.wire_count += wires.size();
    std::map<std::string, double> area_by_layer;
    for (const auto wire_id : wires) {
      routing.forEachShape(wire_id, [&](eccdb::GeometryShapeId shape_id,
                                       const eccdb::DesignRouteShape& shape,
                                       const eccdb::DesignRouteShapeAttributes* attributes) {
        const auto generic_layer = eccdb::TechLayerId{shape.layer.entity()};
        if (!technology->routingLayerStorage().contains(eccdb::TechRoutingLayerId{generic_layer.entity()})) {
          throw std::invalid_argument("iZH native antenna route references a non-routing layer");
        }
        const auto& layer = technology->routingLayerStorage().routingLayer(eccdb::TechRoutingLayerId{generic_layer.entity()});
        const auto area = routeShapeArea(routing, shape_id, shape, attributes, layer);
        if (area > 0.0) {
          area_by_layer[technology->layerInfo(generic_layer).name] += area;
        }
      });
    }

    const auto gate_area = rules.gate_area * static_cast<double>(terminals.size());
    for (const auto& [layer_name, area] : area_by_layer) {
      const auto rule = rules.max_area_ratio.find(layer_name);
      if (rule == rules.max_area_ratio.end()) {
        throw std::invalid_argument("iZH native antenna rule file has no rule for routed layer: " + layer_name);
      }
      const auto ratio = area / gate_area;
      if (ratio <= rule->second) {
        continue;
      }
      ++summary.violation_count;
      if (summary.violations.size() < kMaxReportedViolations) {
        std::ostringstream message;
        message << net.name << ":" << layer_name << " ratio=" << std::setprecision(8) << ratio << " limit=" << rule->second;
        summary.violations.push_back(message.str());
      }
    }
  });
  summary.checked = true;
  _last_antenna_summary = std::move(summary);
}

}  // namespace izh
