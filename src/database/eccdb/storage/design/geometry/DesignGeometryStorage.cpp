// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "design/geometry/DesignGeometryStorage.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <entt/container/dense_map.hpp>
#include <entt/container/dense_set.hpp>

#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/pin_access/model/PinAccessComponents.h"

namespace eccdb {

namespace {

[[nodiscard]] int32_t checkedCoordinate(int64_t value)
{
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    throw std::overflow_error("geometry coordinate exceeds int32 range");
  }
  return static_cast<int32_t>(value);
}

[[nodiscard]] int32_t translated(int32_t value, int32_t delta)
{
  return checkedCoordinate(static_cast<int64_t>(value) + delta);
}

Point orient(Point point, DesignOrientation orientation)
{
  const int64_t x = point.x;
  const int64_t y = point.y;
  switch (orientation) {
    case DesignOrientation::kN: return {checkedCoordinate(x), checkedCoordinate(y)};
    case DesignOrientation::kW: return {checkedCoordinate(-y), checkedCoordinate(x)};
    case DesignOrientation::kS: return {checkedCoordinate(-x), checkedCoordinate(-y)};
    case DesignOrientation::kE: return {checkedCoordinate(y), checkedCoordinate(-x)};
    case DesignOrientation::kFN: return {checkedCoordinate(-x), checkedCoordinate(y)};
    case DesignOrientation::kFW: return {checkedCoordinate(y), checkedCoordinate(x)};
    case DesignOrientation::kFS: return {checkedCoordinate(x), checkedCoordinate(-y)};
    case DesignOrientation::kFE: return {checkedCoordinate(-y), checkedCoordinate(-x)};
  }
  throw std::invalid_argument("invalid geometry orientation");
}

DesignOrientation inverse(DesignOrientation orientation)
{
  if (orientation == DesignOrientation::kW) return DesignOrientation::kE;
  if (orientation == DesignOrientation::kE) return DesignOrientation::kW;
  return orientation;
}

Rect placed(Rect box, const DesignBlockPin& pin)
{
  auto first = orient({box.ll_x, box.ll_y}, pin.orientation);
  auto second = orient({box.ur_x, box.ur_y}, pin.orientation);
  return Rect{translated(first.x, pin.origin.x), translated(first.y, pin.origin.y),
              translated(second.x, pin.origin.x), translated(second.y, pin.origin.y)}.normalized();
}

}  // namespace

bool DesignGeometryStorage::intersects(Rect lhs, Rect rhs) noexcept
{
  return lhs.ll_x <= rhs.ur_x && rhs.ll_x <= lhs.ur_x && lhs.ll_y <= rhs.ur_y && rhs.ll_y <= lhs.ur_y;
}

Rect DesignGeometryStorage::polygonBounds(const std::vector<Point>& points)
{
  if (points.empty()) throw std::invalid_argument("geometry polygon has no points");
  Rect result{points.front().x, points.front().y, points.front().x, points.front().y};
  for (const auto point : points) {
    result.ll_x = std::min(result.ll_x, point.x);
    result.ll_y = std::min(result.ll_y, point.y);
    result.ur_x = std::max(result.ur_x, point.x);
    result.ur_y = std::max(result.ur_y, point.y);
  }
  return result;
}

Rect DesignGeometryStorage::viaBounds(const DesignGeometryNetVia& via) const
{
  if (via.origins.empty()) throw std::invalid_argument("geometry via has no origins");
  auto result = _routing.viaPlacementBounds(via.tech_via, via.design_via, via.origins.front(), via.orientation);
  for (const auto point : via.origins) {
    result = result.united(_routing.viaPlacementBounds(via.tech_via, via.design_via, point, via.orientation));
  }
  return result;
}

DesignGeometryShapeView DesignGeometryStorage::shape(GeometryShapeId id) const
{
  if (!registry().contains(id)) throw std::out_of_range("invalid geometry entity");
  const auto& metadata = registry().shape(id);
  switch (metadata.kind) {
    case GeometryShapeKind::kSegment:
    case GeometryShapeKind::kVia:
    case GeometryShapeKind::kPatch:
    case GeometryShapeKind::kPoint:
      if (const auto* value = registry().tryGet<const DesignRouteShape>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      break;
    case GeometryShapeKind::kRectangle:
      if (const auto* value = registry().tryGet<const DesignGeometryRectangle>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      if (const auto* value = registry().tryGet<const GeometryRectangle>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      if (const auto* value = registry().tryGet<const DesignPinRectangle>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      if (const auto* value = registry().tryGet<const DesignViaRectangle>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      break;
    case GeometryShapeKind::kPolygon:
      if (const auto* value = registry().tryGet<const DesignGeometryPolygon>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      if (const auto* value = registry().tryGet<const GeometryPolygon>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      if (const auto* value = registry().tryGet<const DesignPinPolygon>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      if (const auto* value = registry().tryGet<const DesignViaPolygon>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      break;
    case GeometryShapeKind::kNetVia:
      if (const auto* value = registry().tryGet<const DesignGeometryNetVia>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      break;
    case GeometryShapeKind::kPinVia:
      if (const auto* value = registry().tryGet<const DesignPinVia>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      break;
    case GeometryShapeKind::kBoundary:
      if (const auto* value = registry().tryGet<const GeometryBoundary>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      break;
    case GeometryShapeKind::kGuide:
      if (const auto* value = registry().tryGet<const DesignRouteGuide>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      break;
    case GeometryShapeKind::kTrackAssignment:
      if (const auto* value = registry().tryGet<const DesignTrackAssignmentShape>(id))
        return {.handle = id, .metadata = &metadata, .payload = value};
      break;
    case GeometryShapeKind::kInvalid:
    case GeometryShapeKind::kLibraryVia:
      break;
  }
  throw std::out_of_range("geometry entity has no typed payload");
}

TechLayerId DesignGeometryStorage::effectiveLayer(GeometryShapeId id) const
{
  const auto& metadata = registry().shape(id);
  if (metadata.layer) return TechLayerId{metadata.layer.entity()};
  const auto result = std::visit([](auto pointer) -> TechLayerId {
    if constexpr (requires { pointer->layer; }) return TechLayerId{pointer->layer.entity()};
    return {};
  }, shape(id).payload);
  if (result) return result;
  const auto entity = static_cast<DesignEntity>(metadata.owner.value);
  const auto& objects = _routing.designRegistry().registry();
  if (metadata.owner.domain == GeometryOwnerDomain::kDesignFill)
    return objects.get<const DesignFill>(entity).layer;
  if (metadata.owner.domain == GeometryOwnerDomain::kDesignBlockage)
    return TechLayerId{objects.get<const DesignBlockage>(entity).layer.entity()};
  return {};
}

Rect DesignGeometryStorage::bounds(GeometryShapeId id) const
{
  const auto box = localBounds(id);
  const auto relation = owner(id);
  if (relation.domain == GeometryOwnerDomain::kDesignBlockPin) {
    const auto& pin = _routing.designRegistry().registry().get<const DesignBlockPin>(static_cast<DesignEntity>(relation.value));
    return placed(box, pin);
  }
  return box;
}

Rect DesignGeometryStorage::boundsUncached(GeometryShapeId id) const
{
  const auto box = computeLocalBounds(id);
  const auto relation = owner(id);
  if (relation.domain == GeometryOwnerDomain::kDesignBlockPin) {
    const auto& pin = _routing.designRegistry().registry().get<const DesignBlockPin>(static_cast<DesignEntity>(relation.value));
    return placed(box, pin);
  }
  return box;
}

bool DesignGeometryStorage::forEachViaLayerBounds(GeometryShapeId id, const ViaLayerBoundsVisitor& visitor) const
{
  return _routing.forEachViaLayerBounds(id, visitor);
}

Rect DesignGeometryStorage::localBounds(GeometryShapeId id) const
{
  if (!registry().contains(id)) throw std::out_of_range("invalid geometry entity");
  auto& cached = registry().ensureBounds(id);
  const auto epoch = geometryEpoch();
  if (cached.usable() && cached.space == BoundsSpace::kLocal && cached.source_epoch == epoch) {
    return cached.rect;
  }
  const auto result = computeLocalBounds(id);
  cached.set(result, BoundsSpace::kLocal, epoch);
  return result;
}

Rect DesignGeometryStorage::computeLocalBounds(GeometryShapeId id) const
{
  if (!registry().contains(id)) throw std::out_of_range("invalid geometry entity");
  const auto view = shape(id);
  const auto result = std::visit([&](auto pointer) -> Rect {
    using Payload = std::remove_cvref_t<decltype(pointer)>;
    if constexpr (std::is_same_v<Payload, std::monostate>) {
      throw std::out_of_range("empty geometry shape view");
    } else if constexpr (std::is_same_v<Payload, const DesignRouteShape*>) {
      return _routing.shapeBoundingBox(id);
    } else if constexpr (requires { pointer->rectangle; }) {
      return pointer->rectangle;
    } else if constexpr (requires { pointer->points; }) {
      return polygonBounds(pointer->points);
    } else if constexpr (requires { pointer->box; }) {
      return pointer->box;
    } else if constexpr (std::is_same_v<Payload, const DesignPinVia*>) {
      return _routing.viaPlacementBounds(pointer->tech_via, pointer->design_via, pointer->origin, DesignOrientation::kN);
    } else {
      return viaBounds(*pointer);
    }
  }, view.payload);
  return result;
}

std::span<const GeometryShapeId> DesignGeometryStorage::wireShapeIds(DesignWireId wire) const
{
  return _routing.wire(wire).shapes;
}

std::span<const GeometryShapeId> DesignGeometryStorage::netShapeIds(DesignNetId net) const
{
  const auto* geometry = _routing.netGeometry(net);
  return geometry == nullptr ? std::span<const GeometryShapeId>{} : std::span<const GeometryShapeId>{geometry->shapes};
}

DesignNetGeometry& DesignGeometryStorage::netGeometry(DesignNetId net)
{
  return _routing.ensureNetGeometry(net);
}

GeometryShapeId DesignGeometryStorage::createNetPayload(DesignNetId net, DesignGeometryRectangle value)
{
  _routing.validateNetShape(value);
  auto& owner = netGeometry(net);
  GeometryShapeId id;
  try {
    id = registry().create(designNetOwner(net), std::move(value));
    registry().shape(id).layer = registry().get<const DesignGeometryRectangle>(id).layer;
    registry().shape(id).flags = registry().get<const DesignGeometryRectangle>(id).flags;
    owner.shapes.push_back(id);
  } catch (...) {
    static_cast<void>(registry().destroy(id));
    static_cast<void>(_routing.pruneEmptyNetGeometry(net));
    throw;
  }
  _routing.markGeometryChanged();
  return id;
}

GeometryShapeId DesignGeometryStorage::createNetPayload(DesignNetId net, DesignGeometryPolygon value)
{
  _routing.validateNetShape(value);
  auto& owner = netGeometry(net);
  GeometryShapeId id;
  try {
    id = registry().create(designNetOwner(net), std::move(value));
    registry().shape(id).layer = registry().get<const DesignGeometryPolygon>(id).layer;
    registry().shape(id).flags = registry().get<const DesignGeometryPolygon>(id).flags;
    owner.shapes.push_back(id);
  } catch (...) {
    static_cast<void>(registry().destroy(id));
    static_cast<void>(_routing.pruneEmptyNetGeometry(net));
    throw;
  }
  _routing.markGeometryChanged();
  return id;
}

GeometryShapeId DesignGeometryStorage::createNetPayload(DesignNetId net, DesignGeometryNetVia value)
{
  _routing.validateNetShape(value);
  auto& owner = netGeometry(net);
  GeometryShapeId id;
  try {
    id = registry().create(designNetOwner(net), std::move(value));
    registry().shape(id).flags = registry().get<const DesignGeometryNetVia>(id).flags;
    owner.shapes.push_back(id);
  } catch (...) {
    static_cast<void>(registry().destroy(id));
    static_cast<void>(_routing.pruneEmptyNetGeometry(net));
    throw;
  }
  _routing.markGeometryChanged();
  return id;
}

GeometryShapeId DesignGeometryStorage::appendNetShape(DesignNetId net, DesignNetRectangle value)
{
  return createNetPayload(net, std::move(value));
}

GeometryShapeId DesignGeometryStorage::appendNetShape(DesignNetId net, DesignNetPolygon value)
{
  return createNetPayload(net, std::move(value));
}

GeometryShapeId DesignGeometryStorage::appendNetShape(DesignNetId net, DesignNetVia value)
{
  return createNetPayload(net, std::move(value));
}

void DesignGeometryStorage::moveShape(GeometryShapeId id, int32_t dx, int32_t dy)
{
  if (!registry().contains(id)) throw std::out_of_range("invalid geometry entity");
  const auto metadata = registry().shape(id);
  if (metadata.owner.domain == GeometryOwnerDomain::kNone) throw std::invalid_argument("geometry has no owner");

  if (metadata.owner.domain == GeometryOwnerDomain::kDesignWire) {
    auto value = registry().get<const DesignRouteShape>(id);
    const auto* current_attributes = registry().tryGet<const DesignRouteShapeAttributes>(id);
    const auto attributes = current_attributes == nullptr
                                ? std::optional<DesignRouteShapeAttributes>{}
                                : std::optional<DesignRouteShapeAttributes>{*current_attributes};
    std::visit(
        [dx, dy](auto& geometry) {
          auto shift = [dx, dy](DesignWirePoint& point) {
            point.position.x = translated(point.position.x, dx);
            point.position.y = translated(point.position.y, dy);
          };
          using Geometry = std::remove_cvref_t<decltype(geometry)>;
          if constexpr (std::is_same_v<Geometry, DesignRouteSegment>) {
            shift(geometry.begin);
            shift(geometry.end);
          } else if constexpr (std::is_same_v<Geometry, DesignRouteVia>) {
            shift(geometry.origin);
          } else if constexpr (std::is_same_v<Geometry, DesignRoutePatch>) {
            shift(geometry.anchor);
            geometry.rectangle.ll_x = translated(geometry.rectangle.ll_x, dx);
            geometry.rectangle.ur_x = translated(geometry.rectangle.ur_x, dx);
            geometry.rectangle.ll_y = translated(geometry.rectangle.ll_y, dy);
            geometry.rectangle.ur_y = translated(geometry.rectangle.ur_y, dy);
          } else if constexpr (std::is_same_v<Geometry, DesignRoutePoint>) {
            shift(geometry.point);
          }
        },
        value.geometry);
    updateRouteShape(id, std::move(value), attributes);
    return;
  }

  if (metadata.owner.domain != GeometryOwnerDomain::kDesignNet) {
    if (metadata.owner.domain == GeometryOwnerDomain::kDesignBlockPin) {
      const auto& pin = _routing.designRegistry().registry().get<const DesignBlockPin>(static_cast<DesignEntity>(metadata.owner.value));
      const auto delta = orient({dx, dy}, inverse(pin.orientation));
      dx = delta.x;
      dy = delta.y;
    }
    const auto view = shape(id);
    std::visit([&](auto pointer) {
      using Pointer = std::remove_cvref_t<decltype(pointer)>;
      if constexpr (std::is_same_v<Pointer, std::monostate>
                    || std::is_same_v<Pointer, const DesignRouteShape*>
                    || std::is_same_v<Pointer, const DesignGeometryNetVia*>) {
        throw std::invalid_argument("geometry has no movable payload");
      } else {
        auto value = *pointer;
        const auto shiftPoint = [&](Point& point) {
          point.x = translated(point.x, dx);
          point.y = translated(point.y, dy);
        };
        const auto shiftBox = [&](Rect& box) {
          box.ll_x = translated(box.ll_x, dx);
          box.ur_x = translated(box.ur_x, dx);
          box.ll_y = translated(box.ll_y, dy);
          box.ur_y = translated(box.ur_y, dy);
        };
        if constexpr (requires { value.rectangle; }) shiftBox(value.rectangle);
        else if constexpr (requires { value.points; }) for (auto& point : value.points) shiftPoint(point);
        else if constexpr (requires { value.box; }) shiftBox(value.box);
        else if constexpr (requires { value.origin; }) shiftPoint(value.origin);
        replaceShape(id, std::move(value));
      }
    }, view.payload);
    return;
  }
  switch (metadata.kind) {
    case GeometryShapeKind::kRectangle: {
      const auto& old = registry().get<const DesignGeometryRectangle>(id);
      DesignNetRectangle value{.layer = old.layer,
                               .rectangle = old.rectangle,
                               .route_status = old.route_status,
                               .flags = old.flags,
                               .mask = old.mask,
                               .shield_net = old.shield_net,
                               .shape = old.shape};
      value.rectangle.ll_x = translated(value.rectangle.ll_x, dx);
      value.rectangle.ur_x = translated(value.rectangle.ur_x, dx);
      value.rectangle.ll_y = translated(value.rectangle.ll_y, dy);
      value.rectangle.ur_y = translated(value.rectangle.ur_y, dy);
      replaceNetShape(id, std::move(value));
      return;
    }
    case GeometryShapeKind::kPolygon: {
      const auto& old = registry().get<const DesignGeometryPolygon>(id);
      DesignNetPolygon value{.layer = old.layer,
                             .points = old.points,
                             .route_status = old.route_status,
                             .flags = old.flags,
                             .mask = old.mask,
                             .shield_net = old.shield_net,
                             .shape = old.shape};
      for (auto& point : value.points) {
        point.x = translated(point.x, dx);
        point.y = translated(point.y, dy);
      }
      replaceNetShape(id, std::move(value));
      return;
    }
    case GeometryShapeKind::kNetVia: {
      const auto& old = registry().get<const DesignGeometryNetVia>(id);
      DesignNetVia value{.tech_via = old.tech_via,
                         .design_via = old.design_via,
                         .origins = old.origins,
                         .orientation = old.orientation,
                         .route_status = old.route_status,
                         .flags = old.flags,
                         .top_mask = old.top_mask,
                         .cut_mask = old.cut_mask,
                         .bottom_mask = old.bottom_mask,
                         .shield_net = old.shield_net,
                         .shape = old.shape};
      for (auto& point : value.origins) {
        point.x = translated(point.x, dx);
        point.y = translated(point.y, dy);
      }
      replaceNetShape(id, std::move(value));
      return;
    }
    default: throw std::invalid_argument("geometry kind is not movable");
  }
}

void DesignGeometryStorage::reorderWireShapes(DesignWireId wire, std::span<const GeometryShapeId> order)
{
  auto& owner = _routing.wire(wire);
  if (order.size() != owner.shapes.size()) throw std::invalid_argument("wire shape order has a different size");
  entt::dense_map<GeometryEntity, bool> seen;
  seen.reserve(order.size());
  const auto expected_owner = designWireOwner(wire);
  for (const auto id : order) {
    if (!registry().contains(id) || registry().shape(id).owner.domain != expected_owner.domain
        || registry().shape(id).owner.value != expected_owner.value || !seen.emplace(id.entity(), true).second) {
      throw std::invalid_argument("wire shape order contains an invalid or duplicate entity");
    }
  }
  for (const auto id : owner.shapes) {
    if (!seen.contains(id.entity())) throw std::invalid_argument("wire shape order omits an owned entity");
  }
  owner.shapes.assign(order.begin(), order.end());
  _routing.markGeometryChanged();
}

void DesignGeometryStorage::replaceNetShape(GeometryShapeId id, DesignNetRectangle value)
{
  if (!registry().contains(id) || registry().shape(id).owner.domain != GeometryOwnerDomain::kDesignNet
      || registry().shape(id).kind != GeometryShapeKind::kRectangle) {
    throw std::invalid_argument("geometry is not a standalone net rectangle");
  }
  _routing.validateNetShape(value);
  auto& metadata = registry().shape(id);
  metadata.layer = value.layer;
  metadata.flags = value.flags;
  registry().replace(id, std::move(value));
  _routing.markGeometryChanged();
}

void DesignGeometryStorage::replaceNetShape(GeometryShapeId id, DesignNetPolygon value)
{
  if (!registry().contains(id) || registry().shape(id).owner.domain != GeometryOwnerDomain::kDesignNet
      || registry().shape(id).kind != GeometryShapeKind::kPolygon) {
    throw std::invalid_argument("geometry is not a standalone net polygon");
  }
  _routing.validateNetShape(value);
  auto& metadata = registry().shape(id);
  metadata.layer = value.layer;
  metadata.flags = value.flags;
  registry().replace(id, std::move(value));
  _routing.markGeometryChanged();
}

void DesignGeometryStorage::replaceNetShape(GeometryShapeId id, DesignNetVia value)
{
  if (!registry().contains(id) || registry().shape(id).owner.domain != GeometryOwnerDomain::kDesignNet
      || registry().shape(id).kind != GeometryShapeKind::kNetVia) {
    throw std::invalid_argument("geometry is not a standalone net via");
  }
  _routing.validateNetShape(value);
  auto& metadata = registry().shape(id);
  metadata.flags = value.flags;
  registry().replace(id, std::move(value));
  _routing.markGeometryChanged();
}

bool DesignGeometryStorage::destroy(GeometryShapeId id)
{
  if (!registry().contains(id)) return false;
  const auto owner = registry().shape(id).owner;
  if (owner.domain == GeometryOwnerDomain::kDesignWire) return _routing.destroyShape(id);
  if (owner.domain == GeometryOwnerDomain::kDesignNet) {
    const auto net = DesignNetId{static_cast<DesignEntity>(owner.value)};
    if (auto* relation = _routing.netGeometry(net); relation != nullptr) {
      std::erase(relation->shapes, id);
      if (relation->shapes.empty()) {
        static_cast<void>(_routing.pruneEmptyNetGeometry(net));
        return registry().destroy(id);
      }
    }
  } else {
    auto& objects = _routing.designRegistry().registry();
    const auto entity = static_cast<DesignEntity>(owner.value);
    const auto erase = [id](auto& ids) { std::erase(ids, id); };
    switch (owner.domain) {
      case GeometryOwnerDomain::kDesignBlockPin: {
        auto& pin = objects.get<DesignBlockPin>(entity);
        erase(pin.rectangles); erase(pin.polygons); erase(pin.vias);
        objects.remove<DesignBlockPinAccessGroups>(entity);
        break;
      }
      case GeometryOwnerDomain::kDesignVia: {
        auto& via = objects.get<DesignVia>(entity);
        const bool fixed = (via.flags & DesignViaFlag::kGenerated) == 0u;
        const bool final_shape = (via.rectangles.size() == 1u && via.polygons.empty())
                                 || (via.rectangles.empty() && via.polygons.size() == 1u);
        if (fixed && final_shape) {
          throw std::invalid_argument("cannot remove the final fixed VIA shape; delete the VIA through DesignRoutingStorage");
        }
        erase(via.rectangles); erase(via.polygons);
        break;
      }
      case GeometryOwnerDomain::kDesignInstance: {
        auto& instance = objects.get<DesignInstance>(entity);
        erase(instance.region_bounds);
        if (instance.region_bounds.empty()) instance.flags &= ~DesignInstanceFlag::kHasRegionBounds;
        break;
      }
      case GeometryOwnerDomain::kDesignRegion: {
        auto& region = objects.get<DesignRegion>(entity);
        if (region.rectangles.size() == 1u) {
          throw std::invalid_argument("cannot remove the final region shape; delete the region through DesignConstraintStorage");
        }
        erase(region.rectangles);
        break;
      }
      case GeometryOwnerDomain::kDesignBlockage: {
        auto& blockage = objects.get<DesignBlockage>(entity);
        const bool final_shape = (blockage.rectangles.size() == 1u && blockage.polygons.empty())
                                 || (blockage.rectangles.empty() && blockage.polygons.size() == 1u);
        if (final_shape) {
          throw std::invalid_argument("cannot remove the final blockage shape; delete the blockage through DesignConstraintStorage");
        }
        erase(blockage.rectangles); erase(blockage.polygons);
        break;
      }
      case GeometryOwnerDomain::kDesignFill: {
        auto& fill = objects.get<DesignFill>(entity);
        if (fill.rectangles.size() == 1u) {
          throw std::invalid_argument("cannot remove the final fill shape; delete the fill through DesignFillStorage");
        }
        erase(fill.rectangles);
        break;
      }
      case GeometryOwnerDomain::kDesignDieArea: objects.remove<DesignDieArea>(entity); break;
      case GeometryOwnerDomain::kDesignCoreArea: objects.remove<DesignCoreArea>(entity); break;
      case GeometryOwnerDomain::kDesignGuide: {
        auto& ids = objects.get<DesignNetRouteGuides>(entity).values;
        erase(ids);
        if (ids.empty()) objects.remove<DesignNetRouteGuides>(entity);
        break;
      }
      case GeometryOwnerDomain::kDesignTrackAssignment: {
        auto& ids = objects.get<DesignNetTrackAssignments>(entity).values;
        erase(ids);
        if (ids.empty()) objects.remove<DesignNetTrackAssignments>(entity);
        break;
      }
      default: throw std::invalid_argument("geometry is not owned by this design");
    }
  }
  const bool destroyed = registry().destroy(id);
  if (destroyed) _routing.markGeometryChanged();
  return destroyed;
}

void DesignGeometryStorage::notifyEdit(GeometryShapeId id)
{
  auto& metadata = registry().shape(id);
  const auto view = shape(id);
  std::visit([&](auto pointer) {
    if constexpr (requires { pointer->flags; }) metadata.flags = pointer->flags;
    if constexpr (requires { pointer->layer; }) {
      if constexpr (std::is_same_v<std::remove_cvref_t<decltype(pointer->layer)>, TechRoutingLayerId>)
        metadata.layer = pointer->layer;
    }
  }, view.payload);
  if (metadata.owner.domain == GeometryOwnerDomain::kDesignBlockPin) {
    _routing.designRegistry().registry().remove<DesignBlockPinAccessGroups>(static_cast<DesignEntity>(metadata.owner.value));
  }
  _routing.markGeometryChanged();
}

std::vector<GeometryShapeId> DesignGeometryStorage::shapes(GeometryOwner relation) const
{
  const auto& objects = _routing.designRegistry().registry();
  const auto entity = static_cast<DesignEntity>(relation.value);
  std::vector<GeometryShapeId> result;
  const auto append = [&](const auto& ids) { result.insert(result.end(), ids.begin(), ids.end()); };
  switch (relation.domain) {
    case GeometryOwnerDomain::kDesignWire: append(objects.get<const DesignWire>(entity).shapes); break;
    case GeometryOwnerDomain::kDesignNet: append(objects.get<const DesignNetGeometry>(entity).shapes); break;
    case GeometryOwnerDomain::kDesignBlockPin: {
      const auto& pin = objects.get<const DesignBlockPin>(entity);
      append(pin.rectangles); append(pin.polygons); append(pin.vias); break;
    }
    case GeometryOwnerDomain::kDesignVia: {
      const auto& via = objects.get<const DesignVia>(entity);
      append(via.rectangles); append(via.polygons); break;
    }
    case GeometryOwnerDomain::kDesignInstance: append(objects.get<const DesignInstance>(entity).region_bounds); break;
    case GeometryOwnerDomain::kDesignRegion: append(objects.get<const DesignRegion>(entity).rectangles); break;
    case GeometryOwnerDomain::kDesignBlockage: {
      const auto& blockage = objects.get<const DesignBlockage>(entity);
      append(blockage.rectangles); append(blockage.polygons); break;
    }
    case GeometryOwnerDomain::kDesignFill: append(objects.get<const DesignFill>(entity).rectangles); break;
    case GeometryOwnerDomain::kDesignDieArea: result.push_back(objects.get<const DesignDieArea>(entity).boundary); break;
    case GeometryOwnerDomain::kDesignCoreArea: result.push_back(objects.get<const DesignCoreArea>(entity).boundary); break;
    case GeometryOwnerDomain::kDesignGuide: append(objects.get<const DesignNetRouteGuides>(entity).values); break;
    case GeometryOwnerDomain::kDesignTrackAssignment: append(objects.get<const DesignNetTrackAssignments>(entity).values); break;
    default: throw std::invalid_argument("geometry owner is not in this design");
  }
  return result;
}

void DesignGeometryStorage::validateRestoredState() const
{
  const auto& objects = _routing.designRegistry().registry();
  const auto& geometry_registry = registry().registry();
  // Bounds are runtime-derived and omitted from binary snapshots. They stay
  // sparse after restore and are materialized by localBounds() only when an
  // exact query actually needs them.
  entt::dense_set<GeometryEntity> claimed;
  const auto payloadCount = [&geometry_registry](GeometryEntity entity) {
    return static_cast<unsigned>(geometry_registry.all_of<DesignRouteShape>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<DesignGeometryRectangle>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<DesignGeometryPolygon>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<DesignGeometryNetVia>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<GeometryRectangle>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<GeometryPolygon>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<GeometryBoundary>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<DesignPinRectangle>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<DesignPinPolygon>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<DesignPinVia>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<DesignViaRectangle>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<DesignViaPolygon>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<DesignRouteGuide>(entity))
           + static_cast<unsigned>(geometry_registry.all_of<DesignTrackAssignmentShape>(entity));
  };
  const auto claim = [&](GeometryOwner relation, const auto& ids, const auto& expected) {
    for (const auto id : ids) {
      if (!registry().contains(id) || !claimed.emplace(id.entity()).second)
        throw std::runtime_error("binary geometry has a missing or duplicate owner relation");
      const auto& metadata = registry().shape(id);
      if (metadata.owner.domain != relation.domain || metadata.owner.value != relation.value)
        throw std::runtime_error("binary geometry owner disagrees with its relation");
      if (payloadCount(id.entity()) != 1u || !expected(id, metadata)) {
        throw std::runtime_error("binary geometry payload disagrees with its owner relation");
      }
    }
  };
  const auto owners = [&]<typename Component>(GeometryOwnerDomain domain, auto validate) {
    const auto view = objects.view<const Component>();
    for (const auto entity : view) {
      const auto relation = GeometryOwner{domain, static_cast<uint64_t>(entt::to_integral(entity))};
      validate(view.template get<const Component>(entity), relation);
    }
  };
  const auto isRouteShape = [this](GeometryShapeId id, const GeometryShape& metadata) {
    const auto* payload = registry().tryGet<const DesignRouteShape>(id);
    return payload != nullptr && metadata.kind == routeShapeKind(*payload);
  };
  const auto isNetShape = [this](GeometryShapeId id, const GeometryShape& metadata) {
    switch (metadata.kind) {
      case GeometryShapeKind::kRectangle: return registry().has<DesignGeometryRectangle>(id);
      case GeometryShapeKind::kPolygon: return registry().has<DesignGeometryPolygon>(id);
      case GeometryShapeKind::kNetVia: return registry().has<DesignGeometryNetVia>(id);
      default: return false;
    }
  };
  const auto claimTyped = [&]<typename Payload>(GeometryOwner relation, const auto& ids, GeometryShapeKind kind) {
    claim(relation, ids, [this, kind](GeometryShapeId id, const GeometryShape& metadata) {
      return metadata.kind == kind && registry().has<Payload>(id);
    });
  };

  owners.template operator()<DesignWire>(GeometryOwnerDomain::kDesignWire, [&](const auto& value, GeometryOwner relation) {
    claim(relation, value.shapes, isRouteShape);
  });
  owners.template operator()<DesignNetGeometry>(GeometryOwnerDomain::kDesignNet, [&](const auto& value, GeometryOwner relation) {
    claim(relation, value.shapes, isNetShape);
  });
  owners.template operator()<DesignBlockPin>(GeometryOwnerDomain::kDesignBlockPin, [&](const auto& value, GeometryOwner relation) {
    claimTyped.template operator()<DesignPinRectangle>(relation, value.rectangles, GeometryShapeKind::kRectangle);
    claimTyped.template operator()<DesignPinPolygon>(relation, value.polygons, GeometryShapeKind::kPolygon);
    claimTyped.template operator()<DesignPinVia>(relation, value.vias, GeometryShapeKind::kPinVia);
  });
  owners.template operator()<DesignVia>(GeometryOwnerDomain::kDesignVia, [&](const auto& value, GeometryOwner relation) {
    claimTyped.template operator()<DesignViaRectangle>(relation, value.rectangles, GeometryShapeKind::kRectangle);
    claimTyped.template operator()<DesignViaPolygon>(relation, value.polygons, GeometryShapeKind::kPolygon);
  });
  owners.template operator()<DesignInstance>(GeometryOwnerDomain::kDesignInstance, [&](const auto& value, GeometryOwner relation) {
    claimTyped.template operator()<GeometryRectangle>(relation, value.region_bounds, GeometryShapeKind::kRectangle);
  });
  owners.template operator()<DesignRegion>(GeometryOwnerDomain::kDesignRegion, [&](const auto& value, GeometryOwner relation) {
    if (value.rectangles.empty()) throw std::runtime_error("binary design region has no geometry");
    claimTyped.template operator()<GeometryRectangle>(relation, value.rectangles, GeometryShapeKind::kRectangle);
  });
  owners.template operator()<DesignBlockage>(GeometryOwnerDomain::kDesignBlockage, [&](const auto& value, GeometryOwner relation) {
    if (value.rectangles.empty() && value.polygons.empty()) throw std::runtime_error("binary design blockage has no geometry");
    claimTyped.template operator()<GeometryRectangle>(relation, value.rectangles, GeometryShapeKind::kRectangle);
    claimTyped.template operator()<GeometryPolygon>(relation, value.polygons, GeometryShapeKind::kPolygon);
  });
  owners.template operator()<DesignFill>(GeometryOwnerDomain::kDesignFill, [&](const auto& value, GeometryOwner relation) {
    if (value.rectangles.empty()) throw std::runtime_error("binary design fill has no geometry");
    claimTyped.template operator()<GeometryRectangle>(relation, value.rectangles, GeometryShapeKind::kRectangle);
  });
  owners.template operator()<DesignDieArea>(GeometryOwnerDomain::kDesignDieArea, [&](const auto& value, GeometryOwner relation) {
    claimTyped.template operator()<GeometryBoundary>(relation, std::array{value.boundary}, GeometryShapeKind::kBoundary);
  });
  owners.template operator()<DesignCoreArea>(GeometryOwnerDomain::kDesignCoreArea, [&](const auto& value, GeometryOwner relation) {
    claimTyped.template operator()<GeometryBoundary>(relation, std::array{value.boundary}, GeometryShapeKind::kBoundary);
  });
  owners.template operator()<DesignNetRouteGuides>(GeometryOwnerDomain::kDesignGuide, [&](const auto& value, GeometryOwner relation) {
    claimTyped.template operator()<DesignRouteGuide>(relation, value.values, GeometryShapeKind::kGuide);
  });
  owners.template operator()<DesignNetTrackAssignments>(GeometryOwnerDomain::kDesignTrackAssignment,
                                                         [&](const auto& value, GeometryOwner relation) {
    claimTyped.template operator()<DesignTrackAssignmentShape>(relation, value.values, GeometryShapeKind::kTrackAssignment);
  });

  const auto metadata = geometry_registry.view<const GeometryShape>();
  for (const auto entity : metadata) {
    if (!claimed.contains(entity) || payloadCount(entity) != 1u) {
      throw std::runtime_error("binary geometry entity has no owner relation or an invalid payload count");
    }
  }

  const auto validatePayloadMetadata = [&]<typename Payload>() {
    const auto payloads = geometry_registry.view<const Payload>();
    for (const auto entity : payloads) {
      if (!geometry_registry.all_of<GeometryShape>(entity)) {
        throw std::runtime_error("binary geometry payload has no GeometryShape metadata");
      }
    }
  };
  validatePayloadMetadata.template operator()<DesignRouteShape>();
  validatePayloadMetadata.template operator()<DesignGeometryRectangle>();
  validatePayloadMetadata.template operator()<DesignGeometryPolygon>();
  validatePayloadMetadata.template operator()<DesignGeometryNetVia>();
  validatePayloadMetadata.template operator()<GeometryRectangle>();
  validatePayloadMetadata.template operator()<GeometryPolygon>();
  validatePayloadMetadata.template operator()<GeometryBoundary>();
  validatePayloadMetadata.template operator()<DesignPinRectangle>();
  validatePayloadMetadata.template operator()<DesignPinPolygon>();
  validatePayloadMetadata.template operator()<DesignPinVia>();
  validatePayloadMetadata.template operator()<DesignViaRectangle>();
  validatePayloadMetadata.template operator()<DesignViaPolygon>();
  validatePayloadMetadata.template operator()<DesignRouteGuide>();
  validatePayloadMetadata.template operator()<DesignTrackAssignmentShape>();
}

std::vector<GeometryShapeId> DesignGeometryStorage::shapes(DesignNetId net) const
{
  std::vector<GeometryShapeId> result;
  for (const auto wire : _routing.wireIds(net)) {
    const auto& ids = _routing.wire(wire).shapes;
    result.insert(result.end(), ids.begin(), ids.end());
  }
  if (const auto* geometry = _routing.netGeometry(net); geometry != nullptr)
    result.insert(result.end(), geometry->shapes.begin(), geometry->shapes.end());
  return result;
}

std::vector<GeometryShapeId> DesignGeometryStorage::query(DesignNetId net, Rect box) const
{
  std::vector<GeometryShapeId> result;
  forEach(net, [&](GeometryShapeId id, DesignGeometryShapeView) {
    if (intersects(boundsUncached(id), box)) result.push_back(id);
  });
  return result;
}

}  // namespace eccdb
