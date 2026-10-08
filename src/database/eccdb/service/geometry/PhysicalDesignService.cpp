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
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "geometry/PhysicalDesignService.h"
#include "context/DatabaseContext.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "design/DesignStore.h"
#include "design/geometry/DesignGeometryComponents.h"
#include "geometry/DomainGeometryComponents.h"
#include "geometry/GeometryRegistry.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

struct ShapeAttributes
{
  uint32_t flags = 0;
  uint32_t mask = 0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
};

[[nodiscard]] int32_t checkedCoordinate(int64_t value)
{
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    throw std::overflow_error("physical geometry coordinate exceeds int32 range");
  }
  return static_cast<int32_t>(value);
}

[[nodiscard]] Point checkedPoint(int64_t x, int64_t y)
{
  return Point{.x = checkedCoordinate(x), .y = checkedCoordinate(y)};
}

[[nodiscard]] Point transformAroundOrigin(Point point, Point origin, DesignOrientation orientation)
{
  const int64_t x = point.x;
  const int64_t y = point.y;
  int64_t transformed_x = 0;
  int64_t transformed_y = 0;
  switch (orientation) {
    case DesignOrientation::kN:
      transformed_x = x;
      transformed_y = y;
      break;
    case DesignOrientation::kS:
      transformed_x = -x;
      transformed_y = -y;
      break;
    case DesignOrientation::kE:
      transformed_x = y;
      transformed_y = -x;
      break;
    case DesignOrientation::kW:
      transformed_x = -y;
      transformed_y = x;
      break;
    case DesignOrientation::kFN:
      transformed_x = -x;
      transformed_y = y;
      break;
    case DesignOrientation::kFS:
      transformed_x = x;
      transformed_y = -y;
      break;
    case DesignOrientation::kFE:
      transformed_x = -y;
      transformed_y = -x;
      break;
    case DesignOrientation::kFW:
      transformed_x = y;
      transformed_y = x;
      break;
    default:
      throw std::invalid_argument("physical geometry has an invalid orientation");
  }
  return checkedPoint(transformed_x + origin.x, transformed_y + origin.y);
}

[[nodiscard]] Point placeMasterPoint(Point point, Point origin, DesignOrientation orientation, uint32_t width, uint32_t height)
{
  const int64_t x = point.x;
  const int64_t y = point.y;
  const int64_t master_width = width;
  const int64_t master_height = height;
  int64_t transformed_x = 0;
  int64_t transformed_y = 0;
  switch (orientation) {
    case DesignOrientation::kN:
      transformed_x = x;
      transformed_y = y;
      break;
    case DesignOrientation::kW:
      transformed_x = -y + master_height;
      transformed_y = x;
      break;
    case DesignOrientation::kS:
      transformed_x = -x + master_width;
      transformed_y = -y + master_height;
      break;
    case DesignOrientation::kE:
      transformed_x = y;
      transformed_y = -x + master_width;
      break;
    case DesignOrientation::kFN:
      transformed_x = -x + master_width;
      transformed_y = y;
      break;
    case DesignOrientation::kFS:
      transformed_x = x;
      transformed_y = -y + master_height;
      break;
    case DesignOrientation::kFW:
      transformed_x = y;
      transformed_y = x;
      break;
    case DesignOrientation::kFE:
      transformed_x = -y + master_height;
      transformed_y = -x + master_width;
      break;
    default:
      throw std::invalid_argument("physical geometry has an invalid orientation");
  }
  return checkedPoint(transformed_x + origin.x, transformed_y + origin.y);
}

template <typename Transform>
[[nodiscard]] Rect transformRectangle(Rect rectangle, Transform&& transform)
{
  const std::array corners{std::invoke(transform, Point{.x = rectangle.ll_x, .y = rectangle.ll_y}),
                           std::invoke(transform, Point{.x = rectangle.ll_x, .y = rectangle.ur_y}),
                           std::invoke(transform, Point{.x = rectangle.ur_x, .y = rectangle.ll_y}),
                           std::invoke(transform, Point{.x = rectangle.ur_x, .y = rectangle.ur_y})};
  Rect result{.ll_x = corners.front().x, .ll_y = corners.front().y, .ur_x = corners.front().x, .ur_y = corners.front().y};
  for (const auto corner : corners) {
    result.ll_x = std::min(result.ll_x, corner.x);
    result.ll_y = std::min(result.ll_y, corner.y);
    result.ur_x = std::max(result.ur_x, corner.x);
    result.ur_y = std::max(result.ur_y, corner.y);
  }
  return result;
}

template <typename Transform>
void visitGeometry(const DomainGeometryStorage& storage, std::span<const GeometryShapeId> ids, TechLayerId layer,
                   PhysicalGeometrySource source, ShapeAttributes attributes, Transform&& transform,
                   const PhysicalGeometryVisitor& visitor)
{
  for (const auto id : ids) {
    const auto kind = storage.geometryRegistry().shape(id).kind;
    if (kind == GeometryShapeKind::kRectangle && visitor.rectangle) {
      const auto& rectangle = storage.rectangle(id);
      visitor.rectangle(PhysicalLayerRect{.layer = layer,
                                          .rectangle = transformRectangle(rectangle.rectangle, transform),
                                          .source = source,
                                          .flags = attributes.flags,
                                          .mask = attributes.mask,
                                          .spacing = attributes.spacing,
                                          .design_rule_width = attributes.design_rule_width});
    } else if (kind == GeometryShapeKind::kPolygon && visitor.polygon) {
      const auto& polygon = storage.polygon(id);
      std::vector<Point> points;
      points.reserve(polygon.points.size());
      for (const auto point : polygon.points) {
        points.push_back(std::invoke(transform, point));
      }
      visitor.polygon(PhysicalLayerPolygon{.layer = layer,
                                            .points = points,
                                            .source = source,
                                            .flags = attributes.flags,
                                            .mask = attributes.mask,
                                            .spacing = attributes.spacing,
                                            .design_rule_width = attributes.design_rule_width});
    }
  }
}

template <typename Transform>
void visitDesignGeometry(const GeometryRegistry& registry, std::span<const GeometryShapeId> rectangles,
                         std::span<const GeometryShapeId> polygons, PhysicalGeometrySource source, Transform&& transform,
                         const PhysicalGeometryVisitor& visitor)
{
  if (visitor.rectangle) {
    for (const auto id : rectangles) {
      const auto& rectangle = registry.get<DesignViaRectangle>(id);
      visitor.rectangle(PhysicalLayerRect{.layer = rectangle.layer,
                                          .rectangle = transformRectangle(rectangle.rectangle, transform),
                                          .source = source,
                                          .mask = rectangle.mask});
    }
  }
  if (visitor.polygon) {
    for (const auto id : polygons) {
      const auto& polygon = registry.get<DesignViaPolygon>(id);
      std::vector<Point> points;
      points.reserve(polygon.points.size());
      for (const auto point : polygon.points) {
        points.push_back(std::invoke(transform, point));
      }
      visitor.polygon(PhysicalLayerPolygon{.layer = polygon.layer, .points = points, .source = source, .mask = polygon.mask});
    }
  }
}

template <typename Transform>
void visitGeneratedDesignViaGeometry(const DesignGeneratedVia& via, Transform&& transform, const PhysicalGeometryVisitor& visitor)
{
  if (!visitor.rectangle) {
    return;
  }

  // Match OpenDB's DEF via boxes: center the cut array, then offset each metal enclosure independently.
  const uint64_t width = static_cast<uint64_t>(via.column_count) * via.cut_size_x
                         + static_cast<uint64_t>(via.column_count - 1u) * via.cut_spacing_x;
  const uint64_t height = static_cast<uint64_t>(via.row_count) * via.cut_size_y
                          + static_cast<uint64_t>(via.row_count - 1u) * via.cut_spacing_y;
  if (width > std::numeric_limits<uint32_t>::max() || height > std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error("generated via geometry exceeds int32 coordinate range");
  }

  const int64_t min_x = -static_cast<int64_t>(width / 2u);
  const int64_t min_y = -static_cast<int64_t>(height / 2u);
  const int64_t max_x = min_x + static_cast<int64_t>(width);
  const int64_t max_y = min_y + static_cast<int64_t>(height);
  const int64_t origin_x = (via.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u ? via.origin.x : 0;
  const int64_t origin_y = (via.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u ? via.origin.y : 0;

  const auto emit = [&](TechLayerId layer, int64_t ll_x, int64_t ll_y, int64_t ur_x, int64_t ur_y) {
    const Rect rectangle{checkedCoordinate(ll_x), checkedCoordinate(ll_y), checkedCoordinate(ur_x), checkedCoordinate(ur_y)};
    visitor.rectangle(PhysicalLayerRect{.layer = layer,
                                        .rectangle = transformRectangle(rectangle, transform),
                                        .source = PhysicalGeometrySource::kDesignVia});
  };

  const int64_t bottom_offset_x = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.bottom_offset.x : 0;
  const int64_t bottom_offset_y = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.bottom_offset.y : 0;
  emit(TechLayerId{via.bottom_layer.entity()},
       min_x - via.bottom_enclosure_x + origin_x + bottom_offset_x,
       min_y - via.bottom_enclosure_y + origin_y + bottom_offset_y,
       max_x + via.bottom_enclosure_x + origin_x + bottom_offset_x,
       max_y + via.bottom_enclosure_y + origin_y + bottom_offset_y);

  const int64_t step_x = static_cast<int64_t>(via.cut_size_x) + via.cut_spacing_x;
  const int64_t step_y = static_cast<int64_t>(via.cut_size_y) + via.cut_spacing_y;
  for (uint32_t row = 0; row < via.row_count; ++row) {
    for (uint32_t column = 0; column < via.column_count; ++column) {
      const int64_t x = min_x + origin_x + static_cast<int64_t>(column) * step_x;
      const int64_t y = min_y + origin_y + static_cast<int64_t>(row) * step_y;
      emit(TechLayerId{via.cut_layer.entity()}, x, y, x + via.cut_size_x, y + via.cut_size_y);
    }
  }

  const int64_t top_offset_x = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.top_offset.x : 0;
  const int64_t top_offset_y = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.top_offset.y : 0;
  emit(TechLayerId{via.top_layer.entity()},
       min_x - via.top_enclosure_x + origin_x + top_offset_x,
       min_y - via.top_enclosure_y + origin_y + top_offset_y,
       max_x + via.top_enclosure_x + origin_x + top_offset_x,
       max_y + via.top_enclosure_y + origin_y + top_offset_y);
}

[[nodiscard]] bool isPlacedStatus(DesignPlacementStatus status) noexcept
{
  return status == DesignPlacementStatus::kPlaced || status == DesignPlacementStatus::kFixed || status == DesignPlacementStatus::kCover;
}

[[nodiscard]] TechLayerId layerId(TechConductorLayerRef layer) noexcept
{
  return layer ? layer.layer() : TechLayerId{};
}

void requirePlaced(const DesignInstance& instance)
{
  if (!isPlacedStatus(instance.placement_status)) {
    throw std::invalid_argument("physical geometry requires a placed design instance");
  }
}

template <typename Transform>
void visitMasterPinGeometry(const LibraryStore& library, LibraryMasterPinId pin_id, Transform&& transform,
                            DesignOrientation via_orientation, const PhysicalGeometryVisitor& visitor)
{
  const auto& pin = library.masterPortStorage().masterPin(pin_id);
  for (const auto& clause : pin.layer_clauses) {
    visitGeometry(library.geometryStorage(), clause.geometry, clause.layer, PhysicalGeometrySource::kMasterPort, {}, transform,
                  visitor);
  }
  if (!visitor.via) return;
  for (const auto shape : pin.vias) {
    const auto& via = library.geometryRegistry().get<LibraryViaPlacement>(shape);
    visitor.via(PhysicalViaPlacement{.tech_via = via.via,
                                     .design_via = {},
                                     .origin = std::invoke(transform, via.origin),
                                     .orientation = via_orientation,
                                     .source = PhysicalGeometrySource::kMasterPort});
  }
}

}  // namespace

PhysicalDesignService::PhysicalDesignService(const DatabaseContext& context)
    : PhysicalDesignService(context.technology() ? *context.technology() : throw std::invalid_argument("physical queries require technology storage"),
                            context.library() ? *context.library() : throw std::invalid_argument("physical queries require library storage"),
                            context.design())
{
}

PhysicalDesignService::PhysicalDesignService(const TechStore& tech, const LibraryStore& library, const DesignStore& design)
    : _tech(tech), _library(library), _design(design)
{
  if (&design.techRegistry() != &tech.techRegistry() || &design.libraryRegistry() != &library.libraryRegistry()) {
    throw std::invalid_argument("physical design service stores do not share the same technology and library registries");
  }
}

bool PhysicalDesignService::hasDesignInfo() const noexcept
{
  return _design.globalStorage().hasInfo();
}

int32_t PhysicalDesignService::databaseUnitsPerMicron() const
{
  return _design.globalStorage().info().database_units_per_micron;
}

bool PhysicalDesignService::hasDieArea() const noexcept
{
  return _design.globalStorage().hasDieArea();
}

std::span<const Point> PhysicalDesignService::dieBoundary() const
{
  return _design.globalStorage().dieBoundary();
}

Rect PhysicalDesignService::dieBounds() const
{
  return _design.globalStorage().dieBounds();
}

GeometryLayerKind PhysicalDesignService::layerKind(TechLayerId layer) const
{
  const auto& registry = _tech.techRegistry().registry();
  if (!_tech.contains(layer)) {
    throw std::out_of_range("physical layer query requires a valid technology layer");
  }
  if (registry.all_of<TechRoutingLayer>(layer.entity())) {
    return GeometryLayerKind::kRouting;
  }
  if (registry.all_of<TechCutLayer>(layer.entity())) {
    return GeometryLayerKind::kCut;
  }
  return GeometryLayerKind::kOther;
}

const TechLayerInfo& PhysicalDesignService::layerInfo(TechLayerId layer) const
{
  return _tech.layerInfo(layer);
}

const TechRoutingLayer& PhysicalDesignService::routingLayer(TechRoutingLayerId layer) const
{
  return _tech.routingLayerStorage().routingLayer(layer);
}

const TechCutLayer& PhysicalDesignService::cutLayer(TechCutLayerId layer) const
{
  return _tech.cutLayerStorage().cutLayer(layer);
}

std::optional<uint32_t> PhysicalDesignService::routingLevel(TechRoutingLayerId layer) const noexcept
{
  return _tech.routingLevel(layer);
}

void PhysicalDesignService::forEachRoutingLayer(const RoutingLayerVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  const auto& registry = _tech.techRegistry().registry();
  uint32_t routing_level = 0;
  const auto& sequence = _tech.layerSequence();
  for (uint32_t index = 0; index < sequence.size(); ++index) {
    const auto layer = sequence[index];
    if (!registry.all_of<TechRoutingLayer>(layer.entity())) {
      continue;
    }
    ++routing_level;
    const auto id = TechRoutingLayerId{layer.entity()};
    visitor(id, index, routing_level, _tech.layerInfo(layer), _tech.routingLayerStorage().routingLayer(id));
  }
}

void PhysicalDesignService::forEachCutLayer(const CutLayerVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  const auto& registry = _tech.techRegistry().registry();
  const auto& sequence = _tech.layerSequence();
  for (uint32_t index = 0; index < sequence.size(); ++index) {
    const auto layer = sequence[index];
    if (!registry.all_of<TechCutLayer>(layer.entity())) {
      continue;
    }
    const auto id = TechCutLayerId{layer.entity()};
    visitor(id, index, _tech.layerInfo(layer), _tech.cutLayerStorage().cutLayer(id));
  }
}

void PhysicalDesignService::forEachTrackGrid(const TrackGridVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  const auto view = _design.designRegistry().registry().view<const DesignTrackGrid>();
  view.each([&visitor](const auto entity, const DesignTrackGrid& grid) {
    visitor(DesignTrackGridId{entity}, grid);
  });
}

void PhysicalDesignService::forEachTrackGrid(TechRoutingLayerId layer, const TrackGridVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  const auto view = _design.designRegistry().registry().view<const DesignTrackGrid>();
  view.each([&visitor, layer](const auto entity, const DesignTrackGrid& grid) {
    if (std::find(grid.layers.begin(), grid.layers.end(), layer) != grid.layers.end()) {
      visitor(DesignTrackGridId{entity}, grid);
    }
  });
}

void PhysicalDesignService::forEachGCellGrid(const GCellGridVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  const auto view = _design.designRegistry().registry().view<const DesignGCellGrid>();
  view.each([&visitor](const auto entity, const DesignGCellGrid& grid) {
    visitor(DesignGCellGridId{entity}, grid);
  });
}

std::vector<int32_t> PhysicalDesignService::trackCoordinates(DesignTrackGridId grid) const
{
  return _design.floorplanStorage().trackCoordinates(grid);
}

std::vector<int32_t> PhysicalDesignService::trackCoordinates(TechRoutingLayerId layer, DesignAxis axis) const
{
  return _design.floorplanStorage().trackCoordinates(layer, axis);
}

void PhysicalDesignService::forEachRegularNet(const NetVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  _design.netlistStorage().forEachRegularNet(visitor);
}

void PhysicalDesignService::forEachSpecialNet(const NetVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  _design.netlistStorage().forEachSpecialNet(visitor);
}

DesignNetId PhysicalDesignService::findRegularNet(std::string_view name) const
{
  return _design.netlistStorage().findRegularNet(name);
}

DesignInstanceId PhysicalDesignService::findInstance(std::string_view name) const
{
  return _design.netlistStorage().findInstance(name);
}

DesignInstancePinId PhysicalDesignService::findInstancePin(DesignInstanceId instance, std::string_view master_term_name) const
{
  return _design.netlistStorage().findInstancePin(instance, master_term_name);
}

DesignInstanceTermId PhysicalDesignService::findInstanceTerm(DesignInstanceId instance, std::string_view master_term_name) const
{
  return _design.netlistStorage().findInstanceTerm(instance, master_term_name);
}

const DesignInstanceTerm* PhysicalDesignService::findInstanceTermData(DesignInstanceId instance,
                                                                       std::string_view master_term_name) const
{
  return _design.netlistStorage().findInstanceTermData(instance, master_term_name);
}

DesignIoPinId PhysicalDesignService::findIoPin(std::string_view name) const
{
  return _design.netlistStorage().findIoPin(name);
}

DesignBlockTermId PhysicalDesignService::findBlockTerm(std::string_view name) const
{
  return _design.netlistStorage().findBlockTerm(name);
}

LibraryCellMasterId PhysicalDesignService::findMaster(std::string_view name) const
{
  return _library.cellMasterStorage().findCellMaster(name);
}

LibraryMasterTermId PhysicalDesignService::findMasterTerm(LibraryCellMasterId master, std::string_view name) const
{
  return _library.masterTermStorage().findMasterTerm(master, name);
}

std::span<const LibraryMasterPortId> PhysicalDesignService::masterPorts(LibraryMasterTermId term) const
{
  return _library.masterTermStorage().masterTerm(term).ports;
}

std::span<const LibraryMasterPinId> PhysicalDesignService::masterPins(LibraryMasterTermId term) const
{
  return masterPorts(term);
}

std::span<const DesignBlockPinId> PhysicalDesignService::blockPins(DesignBlockTermId term) const
{
  return _design.netlistStorage().blockPins(term);
}

void PhysicalDesignService::forEachBlockPin(DesignBlockTermId term, const BlockPinVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  _design.netlistStorage().forEachBlockPin(term, visitor);
}

void PhysicalDesignService::forEachNetPin(DesignNetId net, const PhysicalNetPinVisitor& visitor) const
{
  const auto& design_registry = _design.designRegistry().registry();
  const auto& netlist = _design.netlistStorage();
  if (visitor.instance_pin) {
    if (const auto* connections = design_registry.try_get<const DesignNetInstancePins>(net.entity()); connections != nullptr) {
      for (const auto pin_id : connections->values) {
        const auto& pin = netlist.instanceTerm(pin_id);
        visitor.instance_pin(pin_id, pin, *pin.instance_ptr, *pin.master_term_ptr);
      }
    }
  }
  if (visitor.io_pin) {
    if (const auto* connections = design_registry.try_get<const DesignNetIoPins>(net.entity()); connections != nullptr) {
      const auto* terms = design_registry.storage<DesignIoPin>();
      for (const auto pin_id : connections->values) {
        visitor.io_pin(pin_id, terms->get(pin_id.entity()));
      }
    }
  }
}

PhysicalNdrReference PhysicalDesignService::nonDefaultRule(DesignNetId net) const
{
  const auto& value = _design.netlistStorage().net(net);
  return PhysicalNdrReference{.tech_rule = value.non_default_rule, .design_rule = value.design_non_default_rule};
}

bool PhysicalDesignService::netHasJumpers(DesignNetId net) const
{
  return (_design.netlistStorage().net(net).flags & DesignNetFlag::kHasJumpers) != 0u;
}

bool PhysicalDesignService::isNetConnectedByAbutment(DesignNetId net) const
{
  const auto& netlist = _design.netlistStorage();
  const auto instance_pins = netlist.instancePins(net);
  if (instance_pins.size() > 2u || !netlist.ioPins(net).empty()) {
    return false;
  }

  bool first_term = true;
  std::vector<Rect> first_pin_boxes;
  for (const auto pin_id : instance_pins) {
    const auto& pin = netlist.instancePin(pin_id);
    const auto& instance = netlist.instance(pin.instance);
    const auto& term = _library.masterTermStorage().masterTerm(pin.master_term);
    const auto& master = _library.cellMasterStorage().cellMaster(term.master);
    if (master.type != LibraryCellMasterType::kBlock && master.type != LibraryCellMasterType::kBlockBlackbox
        && master.type != LibraryCellMasterType::kBlockSoft) {
      return false;
    }

    bool overlaps_first_term = false;
    if (isPlacedStatus(instance.placement_status)) {
      forEachPlacedInstancePinGeometry(
          pin_id, PhysicalGeometryVisitor{.rectangle = [&](const PhysicalLayerRect& shape) {
            if (overlaps_first_term) {
              return;
            }
            if (layerKind(shape.layer) != GeometryLayerKind::kRouting) {
              return;
            }
            if (first_term) {
              first_pin_boxes.push_back(shape.rectangle);
              return;
            }
            for (const auto first_pin_box : first_pin_boxes) {
              if (shape.rectangle.intersects(first_pin_box)) {
                overlaps_first_term = true;
                return;
              }
            }
          },
                                         .polygon = [&](const PhysicalLayerPolygon& shape) {
                                           if (!overlaps_first_term && layerKind(shape.layer) == GeometryLayerKind::kRouting) {
                                             throw std::logic_error(
                                                 "routing master-port polygons are unsupported by net abutment detection");
                                           }
                                         }});
    }
    if (overlaps_first_term) {
      return true;
    }
    first_term = false;
  }
  return false;
}

std::optional<std::string_view> PhysicalDesignService::nonDefaultRuleName(DesignNetId net) const
{
  const auto reference = nonDefaultRule(net);
  if (reference.tech_rule && reference.design_rule) {
    throw std::logic_error("design net references both a technology and design non-default rule");
  }
  if (reference.tech_rule) {
    return _tech.nonDefaultRuleStorage().nonDefaultRule(reference.tech_rule).name;
  }
  if (reference.design_rule) {
    return _design.routingStorage().nonDefaultRule(reference.design_rule).name;
  }
  return std::nullopt;
}

void PhysicalDesignService::forEachNetWire(DesignNetId net, const NetWireVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  _design.routingStorage().forEachWire(net, visitor);
}

void PhysicalDesignService::forEachNetWireShape(DesignNetId net, const NetWireShapeVisitor& visitor) const
{
  if (!visitor) return;
  const auto& storage = _design.routingStorage();
  storage.forEachWire(net, [&](DesignWireId wire_id, const DesignWire& wire) {
    storage.forEachShape(wire_id, [&](DesignRouteShapeId id, const DesignRouteShape& shape,
                                      const DesignRouteShapeAttributes* attributes) {
      visitor(wire_id, wire, id, shape, attributes);
    });
  });
}

void PhysicalDesignService::forEachNetGeometry(DesignNetId net, const PhysicalNetGeometryVisitor& visitor) const
{
  const auto* geometry = _design.routingStorage().netGeometry(net);
  if (geometry == nullptr) {
    return;
  }
  const auto& storage = _design.routingStorage();
  const auto& registry = storage.geometryRegistry();
  for (const auto id : geometry->shapes) {
    const auto kind = registry.shape(id).kind;
    if (kind == GeometryShapeKind::kRectangle && visitor.rectangle) {
      const auto& value = registry.get<const DesignGeometryRectangle>(id);
      visitor.rectangle(value);
    } else if (kind == GeometryShapeKind::kPolygon && visitor.polygon) {
      const auto& value = registry.get<const DesignGeometryPolygon>(id);
      visitor.polygon(value);
    } else if (kind == GeometryShapeKind::kNetVia && visitor.via) {
      const auto& value = registry.get<const DesignGeometryNetVia>(id);
      visitor.via(value);
    }
  }
}

std::string_view PhysicalDesignService::viaName(TechViaMasterId tech_via, DesignViaId design_via) const
{
  if (static_cast<bool>(tech_via) == static_cast<bool>(design_via)) {
    throw std::invalid_argument("routing VIA requires exactly one technology or design VIA definition");
  }
  if (tech_via) {
    return _tech.viaMasterStorage().viaMaster(tech_via).name;
  }
  return _design.routingStorage().via(design_via).name;
}

PhysicalRoutingViaLayers PhysicalDesignService::routingViaLayers(TechViaMasterId tech_via, DesignViaId design_via) const
{
  if (static_cast<bool>(tech_via) == static_cast<bool>(design_via)) {
    throw std::invalid_argument("routing VIA requires exactly one technology or design VIA definition");
  }

  const auto order = [this](TechRoutingLayerId first, TechRoutingLayerId second) {
    const auto& registry = _tech.techRegistry().registry();
    if (!first || !second || first == second || !registry.valid(first.entity()) || !registry.valid(second.entity())
        || !registry.all_of<TechRoutingLayer>(first.entity()) || !registry.all_of<TechRoutingLayer>(second.entity())) {
      throw std::invalid_argument("routing VIA does not connect two distinct routing layers");
    }
    const auto first_layer = TechLayerId{first.entity()};
    const auto second_layer = TechLayerId{second.entity()};
    if (_tech.isBelow(first_layer, second_layer)) {
      return PhysicalRoutingViaLayers{.bottom = first, .top = second};
    }
    if (_tech.isBelow(second_layer, first_layer)) {
      return PhysicalRoutingViaLayers{.bottom = second, .top = first};
    }
    throw std::invalid_argument("routing VIA layers do not have a technology-stack order");
  };

  if (tech_via) {
    const auto& geometry = _tech.viaMasterStorage().geometry(tech_via);
    if (geometry.bottom_layer.kind != TechConductorLayerKind::kRouting
        || geometry.top_layer.kind != TechConductorLayerKind::kRouting) {
      throw std::invalid_argument("technology VIA does not have routing layers on both sides");
    }
    return order(TechRoutingLayerId{geometry.bottom_layer.entity}, TechRoutingLayerId{geometry.top_layer.entity});
  }

  const auto& via = _design.routingStorage().via(design_via);
  if ((via.flags & DesignViaFlag::kGenerated) != 0u) {
    return order(via.generated.bottom_layer, via.generated.top_layer);
  }

  const auto& registry = _tech.techRegistry().registry();
  std::vector<TechRoutingLayerId> layers;
  const auto append = [&](TechLayerId layer) {
    if (!layer || !registry.valid(layer.entity()) || !registry.all_of<TechRoutingLayer>(layer.entity())) {
      return;
    }
    const auto routing_layer = TechRoutingLayerId{layer.entity()};
    if (std::find(layers.begin(), layers.end(), routing_layer) == layers.end()) {
      layers.push_back(routing_layer);
    }
  };
  const auto& geometry = _design.geometryRegistry();
  for (const auto id : via.rectangles) {
    const auto& rectangle = geometry.get<DesignViaRectangle>(id);
    append(rectangle.layer);
  }
  for (const auto id : via.polygons) {
    const auto& polygon = geometry.get<DesignViaPolygon>(id);
    append(polygon.layer);
  }
  if (layers.size() != 2u) {
    throw std::invalid_argument("fixed design VIA does not have geometry on exactly two routing layers");
  }
  return order(layers[0], layers[1]);
}

bool PhysicalDesignService::isPlaced(DesignInstanceId instance) const
{
  return isPlacedStatus(_design.netlistStorage().instance(instance).placement_status);
}

void PhysicalDesignService::forEachMasterPinGeometry(LibraryMasterPinId pin, const PhysicalGeometryVisitor& visitor) const
{
  const auto identity = [](Point point) { return point; };
  visitMasterPinGeometry(_library, pin, identity, DesignOrientation::kN, visitor);
}

void PhysicalDesignService::forEachPlacedInstanceMasterPinGeometry(DesignInstanceTermId iterm_id, LibraryMasterPinId pin_id,
                                                                    const PhysicalGeometryVisitor& visitor) const
{
  const auto& netlist = _design.netlistStorage();
  const auto& iterm = netlist.instancePin(iterm_id);
  const auto& instance = netlist.instance(iterm.instance);
  requirePlaced(instance);
  const auto& master = _library.cellMasterStorage().cellMaster(instance.master);
  const auto& term = _library.masterTermStorage().masterTerm(iterm.master_term);
  const auto& pin = _library.masterPortStorage().masterPin(pin_id);
  if (term.master != instance.master || pin.term != iterm.master_term) {
    throw std::logic_error("placed instance master pin does not belong to its instance term");
  }

  const auto transform = [&instance, &master](Point point) {
    return placeMasterPoint(point, instance.origin, instance.orientation, master.width, master.height);
  };
  visitMasterPinGeometry(_library, pin_id, transform, instance.orientation, visitor);
}

void PhysicalDesignService::forEachPlacedInstancePinGeometry(DesignInstancePinId pin_id,
                                                              const PhysicalGeometryVisitor& visitor) const
{
  const auto& netlist = _design.netlistStorage();
  const auto& pin = netlist.instancePin(pin_id);
  const auto& instance = netlist.instance(pin.instance);
  requirePlaced(instance);
  const auto& master = _library.cellMasterStorage().cellMaster(instance.master);
  const auto& term = _library.masterTermStorage().masterTerm(pin.master_term);
  if (term.master != instance.master) {
    throw std::logic_error("design instance pin does not belong to its instance master");
  }

  const auto transform = [&instance, &master](Point point) {
    return placeMasterPoint(point, instance.origin, instance.orientation, master.width, master.height);
  };
  for (const auto port_id : term.ports) {
    visitMasterPinGeometry(_library, port_id, transform, instance.orientation, visitor);
  }
}

void PhysicalDesignService::forEachBlockPinGeometry(DesignBlockPinId pin_id, const PhysicalGeometryVisitor& visitor) const
{
  const auto& pin = _design.netlistStorage().blockPin(pin_id);
  const auto& geometry = _design.geometryRegistry();
  const bool has_placement = (pin.flags & DesignIoPinPortFlag::kHasPlacement) != 0u;
  const Point origin = has_placement ? pin.origin : Point{};
  const DesignOrientation orientation = has_placement ? pin.orientation : DesignOrientation::kN;
  const auto transform = [origin, orientation](Point point) { return transformAroundOrigin(point, origin, orientation); };
  for (const auto id : pin.rectangles) {
    const auto& rectangle = geometry.get<DesignPinRectangle>(id);
    if (visitor.rectangle) {
      visitor.rectangle(PhysicalLayerRect{.layer = rectangle.layer,
                                          .rectangle = transformRectangle(rectangle.rectangle, transform),
                                          .source = PhysicalGeometrySource::kIoPin,
                                          .flags = rectangle.flags,
                                          .mask = rectangle.mask,
                                          .spacing = rectangle.spacing,
                                          .design_rule_width = rectangle.design_rule_width});
    }
  }
  if (visitor.polygon) {
    for (const auto id : pin.polygons) {
      const auto& polygon = geometry.get<DesignPinPolygon>(id);
      std::vector<Point> points;
      points.reserve(polygon.points.size());
      for (const auto point : polygon.points) {
        points.push_back(transform(point));
      }
      visitor.polygon(PhysicalLayerPolygon{.layer = polygon.layer,
                                            .points = points,
                                            .source = PhysicalGeometrySource::kIoPin,
                                            .flags = polygon.flags,
                                            .mask = polygon.mask,
                                            .spacing = polygon.spacing,
                                            .design_rule_width = polygon.design_rule_width});
    }
  }
  if (visitor.via) {
    for (const auto id : pin.vias) {
      const auto& via = geometry.get<DesignPinVia>(id);
      visitor.via(PhysicalViaPlacement{.tech_via = via.tech_via,
                                       .design_via = via.design_via,
                                       .origin = transform(via.origin),
                                       .orientation = orientation,
                                       .source = PhysicalGeometrySource::kIoPin,
                                       .flags = via.flags,
                                       .top_mask = via.top_mask,
                                       .cut_mask = via.cut_mask,
                                       .bottom_mask = via.bottom_mask});
    }
  }
}

void PhysicalDesignService::forEachIoPinGeometry(DesignIoPinId pin_id, const PhysicalGeometryVisitor& visitor) const
{
  _design.netlistStorage().forEachBlockPin(pin_id, [this, &visitor](DesignBlockPinId block_pin, const DesignBlockPin&) {
    forEachBlockPinGeometry(block_pin, visitor);
  });
}

void PhysicalDesignService::forEachPlacedMasterObstructionGeometry(DesignInstanceId instance_id,
                                                                    const PhysicalGeometryVisitor& visitor) const
{
  const auto& instance = _design.netlistStorage().instance(instance_id);
  requirePlaced(instance);
  const auto& master = _library.cellMasterStorage().cellMaster(instance.master);
  if (!_library.cellMasterStorage().hasObs(instance.master)) {
    return;
  }

  const auto transform = [&instance, &master](Point point) {
    return placeMasterPoint(point, instance.origin, instance.orientation, master.width, master.height);
  };
  const auto& obstruction = _library.cellMasterStorage().obs(instance.master);
  for (const auto& clause : obstruction.layer_clauses) {
    visitGeometry(_library.geometryStorage(), clause.geometry, clause.layer, PhysicalGeometrySource::kMasterObstruction,
                  ShapeAttributes{.flags = clause.flags,
                                  .spacing = clause.spacing,
                                  .design_rule_width = clause.design_rule_width},
                  transform, visitor);
  }
  if (visitor.via) {
    for (const auto shape : obstruction.vias) {
      const auto& via = _library.geometryRegistry().get<LibraryViaPlacement>(shape);
      visitor.via(PhysicalViaPlacement{.tech_via = via.via,
                                       .design_via = {},
                                       .origin = transform(via.origin),
                                       .orientation = instance.orientation,
                                       .source = PhysicalGeometrySource::kMasterObstruction});
    }
  }
}

void PhysicalDesignService::forEachRoutingBlockage(const RoutingBlockageVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  const auto view = _design.designRegistry().registry().view<const DesignBlockage>();
  view.each([&visitor](const auto entity, const DesignBlockage& blockage) {
    if (blockage.kind == DesignBlockageKind::kRouting) {
      visitor(DesignBlockageId{entity}, blockage);
    }
  });
}

void PhysicalDesignService::forEachRoutingBlockageGeometry(DesignBlockageId blockage_id,
                                                            const PhysicalGeometryVisitor& visitor) const
{
  const auto& blockage = _design.constraintStorage().blockage(blockage_id);
  if (blockage.kind != DesignBlockageKind::kRouting) {
    throw std::invalid_argument("physical routing blockage geometry requires a routing blockage");
  }
  const auto identity = [](Point point) { return point; };
  const auto layer = TechLayerId{blockage.layer.entity()};
  const auto& geometry = _design.geometryRegistry();
  if (visitor.rectangle) {
    for (const auto id : blockage.rectangles) {
      const auto& rectangle = geometry.get<GeometryRectangle>(id);
      visitor.rectangle(PhysicalLayerRect{.layer = layer,
                                          .rectangle = transformRectangle(rectangle.rectangle, identity),
                                          .source = PhysicalGeometrySource::kRoutingBlockage,
                                          .flags = blockage.flags,
                                          .mask = blockage.mask,
                                          .spacing = blockage.spacing,
                                          .design_rule_width = blockage.design_rule_width});
    }
  }
  if (visitor.polygon) {
    for (const auto id : blockage.polygons) {
      const auto& polygon = geometry.get<GeometryPolygon>(id);
      visitor.polygon(PhysicalLayerPolygon{.layer = layer,
                                            .points = polygon.points,
                                            .source = PhysicalGeometrySource::kRoutingBlockage,
                                            .flags = blockage.flags,
                                            .mask = blockage.mask,
                                            .spacing = blockage.spacing,
                                            .design_rule_width = blockage.design_rule_width});
    }
  }
}

void PhysicalDesignService::forEachTechVia(const TechViaVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  const auto view = _tech.techRegistry().registry().view<const TechViaMaster, const TechViaGeometry>();
  view.each([&visitor](const auto entity, const TechViaMaster& via, const TechViaGeometry& geometry) {
    visitor(TechViaMasterId{entity}, via, geometry);
  });
}

void PhysicalDesignService::forEachTechViaRuleGenerate(const TechViaRuleGenerateVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  const auto view = _tech.techRegistry().registry().view<const TechViaRuleGenerate, const TechViaRuleGenerateBottomLayer,
                                                         const TechViaRuleGenerateCutLayer, const TechViaRuleGenerateTopLayer>();
  view.each([&visitor](const auto entity, const TechViaRuleGenerate& rule, const TechViaRuleGenerateBottomLayer& bottom,
                       const TechViaRuleGenerateCutLayer& cut, const TechViaRuleGenerateTopLayer& top) {
    visitor(TechViaRuleGenerateId{entity}, rule, bottom, cut, top);
  });
}

void PhysicalDesignService::forEachDesignVia(const DesignViaVisitor& visitor) const
{
  if (!visitor) {
    return;
  }
  const auto view = _design.designRegistry().registry().view<const DesignVia>();
  view.each([&visitor](const auto entity, const DesignVia& via) {
    visitor(DesignViaId{entity}, via);
  });
}

void PhysicalDesignService::forEachTechViaGeometry(TechViaMasterId via, const PhysicalGeometryVisitor& visitor) const
{
  forEachTechViaGeometry(via, {}, DesignOrientation::kN, visitor);
}

void PhysicalDesignService::forEachTechViaGeometry(TechViaMasterId via, Point origin, DesignOrientation orientation,
                                                    const PhysicalGeometryVisitor& visitor) const
{
  const auto& storage = _tech.viaMasterStorage();
  const auto& geometry = storage.geometry(via);
  const auto transform = [origin, orientation](Point point) { return transformAroundOrigin(point, origin, orientation); };
  const auto visit_layer = [&](TechLayerId layer, std::span<const GeometryShapeId> ids) {
    if (layer) {
      visitGeometry(_tech.geometryStorage(), ids, layer, PhysicalGeometrySource::kTechVia, {}, transform, visitor);
    }
  };
  visit_layer(layerId(geometry.bottom_layer), geometry.bottom_geometry);
  visit_layer(TechLayerId{geometry.cut_layer.entity()}, geometry.cut_geometry);
  visit_layer(layerId(geometry.top_layer), geometry.top_geometry);
}

void PhysicalDesignService::forEachDesignViaGeometry(DesignViaId via, const PhysicalGeometryVisitor& visitor) const
{
  forEachDesignViaGeometry(via, {}, DesignOrientation::kN, visitor);
}

void PhysicalDesignService::forEachDesignViaGeometry(DesignViaId via, Point origin, DesignOrientation orientation,
                                                      const PhysicalGeometryVisitor& visitor) const
{
  const auto& value = _design.routingStorage().via(via);
  const auto transform = [origin, orientation](Point point) { return transformAroundOrigin(point, origin, orientation); };
  if ((value.flags & DesignViaFlag::kGenerated) != 0u) {
    visitGeneratedDesignViaGeometry(value.generated, transform, visitor);
    return;
  }
  visitDesignGeometry(_design.geometryRegistry(), value.rectangles, value.polygons, PhysicalGeometrySource::kDesignVia,
                      transform, visitor);
}

}  // namespace eccdb
