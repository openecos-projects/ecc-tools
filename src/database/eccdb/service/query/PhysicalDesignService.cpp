// SPDX-License-Identifier: Apache-2.0

#include "service/query/PhysicalDesignService.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "design/DesignStore.h"
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
void visitPoolGeometry(const GeometryPool& pool, GeometryHandle handle, TechLayerId layer, PhysicalGeometrySource source,
                       ShapeAttributes attributes, Transform&& transform, const PhysicalGeometryVisitor& visitor)
{
  if (visitor.rectangle) {
    for (const auto rectangle : pool.rectangles(handle)) {
      visitor.rectangle(PhysicalLayerRect{.layer = layer,
                                          .rectangle = transformRectangle(rectangle, transform),
                                          .source = source,
                                          .flags = attributes.flags,
                                          .mask = attributes.mask,
                                          .spacing = attributes.spacing,
                                          .design_rule_width = attributes.design_rule_width});
    }
  }
  if (visitor.polygon) {
    for (uint32_t polygon_index = 0; polygon_index < pool.polygonCount(handle); ++polygon_index) {
      const auto source_points = pool.polygonPoints(handle, polygon_index);
      std::vector<Point> points;
      points.reserve(source_points.size());
      for (const auto point : source_points) {
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
void visitDesignGeometry(const std::vector<DesignViaRectangle>& rectangles, const std::vector<DesignViaPolygon>& polygons,
                         PhysicalGeometrySource source, Transform&& transform, const PhysicalGeometryVisitor& visitor)
{
  if (visitor.rectangle) {
    for (const auto& rectangle : rectangles) {
      visitor.rectangle(PhysicalLayerRect{.layer = rectangle.layer,
                                          .rectangle = transformRectangle(rectangle.rectangle, transform),
                                          .source = source,
                                          .mask = rectangle.mask});
    }
  }
  if (visitor.polygon) {
    for (const auto& polygon : polygons) {
      std::vector<Point> points;
      points.reserve(polygon.points.size());
      for (const auto point : polygon.points) {
        points.push_back(std::invoke(transform, point));
      }
      visitor.polygon(PhysicalLayerPolygon{.layer = polygon.layer, .points = points, .source = source, .mask = polygon.mask});
    }
  }
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

}  // namespace

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

const DesignDieArea& PhysicalDesignService::dieArea() const
{
  return _design.globalStorage().dieArea();
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

DesignIoPinId PhysicalDesignService::findIoPin(std::string_view name) const
{
  return _design.netlistStorage().findIoPin(name);
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

void PhysicalDesignService::forEachNetPin(DesignNetId net, const PhysicalNetPinVisitor& visitor) const
{
  const auto& design_registry = _design.designRegistry().registry();
  if (visitor.instance_pin) {
    if (const auto* connections = design_registry.try_get<const DesignNetInstancePins>(net.entity()); connections != nullptr) {
      for (const auto pin_id : connections->values) {
        const auto& pin = design_registry.get<const DesignInstancePin>(pin_id.entity());
        visitor.instance_pin(pin_id, pin, *pin.instance_ptr, *pin.master_term_ptr);
      }
    }
  }
  if (visitor.io_pin) {
    if (const auto* connections = design_registry.try_get<const DesignNetIoPins>(net.entity()); connections != nullptr) {
      for (const auto pin_id : connections->values) {
        visitor.io_pin(pin_id, design_registry.get<const DesignIoPin>(pin_id.entity()));
      }
    }
  }
}

PhysicalNdrReference PhysicalDesignService::nonDefaultRule(DesignNetId net) const
{
  const auto& value = _design.netlistStorage().net(net);
  return PhysicalNdrReference{.tech_rule = value.non_default_rule, .design_rule = value.design_non_default_rule};
}

bool PhysicalDesignService::isPlaced(DesignInstanceId instance) const
{
  return isPlacedStatus(_design.netlistStorage().instance(instance).placement_status);
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
    const auto& port = _library.masterPortStorage().masterPort(port_id);
    for (const auto& clause : port.layer_clauses) {
      visitPoolGeometry(_library.geometryPool(), clause.geometry, clause.layer, PhysicalGeometrySource::kMasterPort, {}, transform, visitor);
    }
    if (visitor.via) {
      for (const auto& via : port.vias) {
        visitor.via(PhysicalViaPlacement{.tech_via = via.via,
                                         .design_via = {},
                                         .origin = transform(via.origin),
                                         .orientation = instance.orientation,
                                         .source = PhysicalGeometrySource::kMasterPort});
      }
    }
  }
}

void PhysicalDesignService::forEachIoPinGeometry(DesignIoPinId pin_id, const PhysicalGeometryVisitor& visitor) const
{
  const auto& pin = _design.netlistStorage().ioPin(pin_id);
  for (const auto& port : pin.ports) {
    const bool has_placement = (port.flags & DesignIoPinPortFlag::kHasPlacement) != 0u;
    const Point origin = has_placement ? port.origin : Point{};
    const DesignOrientation orientation = has_placement ? port.orientation : DesignOrientation::kN;
    const auto transform = [origin, orientation](Point point) { return transformAroundOrigin(point, origin, orientation); };
    for (const auto& rectangle : port.rectangles) {
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
      for (const auto& polygon : port.polygons) {
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
      for (const auto& via : port.vias) {
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
    visitPoolGeometry(_library.geometryPool(), clause.geometry, clause.layer, PhysicalGeometrySource::kMasterObstruction,
                      ShapeAttributes{.flags = clause.flags,
                                      .spacing = clause.spacing,
                                      .design_rule_width = clause.design_rule_width},
                      transform, visitor);
  }
  if (visitor.via) {
    for (const auto& via : obstruction.vias) {
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
  if (visitor.rectangle) {
    for (const auto rectangle : blockage.rectangles) {
      visitor.rectangle(PhysicalLayerRect{.layer = layer,
                                          .rectangle = transformRectangle(rectangle, identity),
                                          .source = PhysicalGeometrySource::kRoutingBlockage,
                                          .flags = blockage.flags,
                                          .mask = blockage.mask,
                                          .spacing = blockage.spacing,
                                          .design_rule_width = blockage.design_rule_width});
    }
  }
  if (visitor.polygon) {
    for (const auto& polygon : blockage.polygons) {
      visitor.polygon(PhysicalLayerPolygon{.layer = layer,
                                            .points = polygon,
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
  const auto visit_layer = [&](TechLayerId layer, GeometryHandle handle) {
    if (layer) {
      visitPoolGeometry(_tech.geometryPool(), handle, layer, PhysicalGeometrySource::kTechVia, {}, transform, visitor);
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
  visitDesignGeometry(value.rectangles, value.polygons, PhysicalGeometrySource::kDesignVia, transform, visitor);
}

}  // namespace eccdb
