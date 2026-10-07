// SPDX-License-Identifier: Apache-2.0
#include "service/routing/NetRouteService.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "design/DesignStore.h"
#include "design/routing/storage/DesignRoutingStorage.h"
#include "design/via/component/ViaComponents.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

struct RouteViaLayers
{
  TechRoutingLayerId bottom;
  TechRoutingLayerId top;
};

void ensureRegularNet(const DesignStore& design, DesignNetId net)
{
  const auto& netlist = design.netlistStorage();
  if (!netlist.contains(net)) {
    throw std::out_of_range("route service requires a valid design net");
  }
  if (netlist.isSpecialNet(net)) {
    throw std::invalid_argument("route service only supports ordinary design nets");
  }
}

void ensureRoutingLayer(const TechStore& technology, TechRoutingLayerId layer)
{
  if (!technology.routingLayerStorage().contains(layer)) {
    throw std::invalid_argument("route shape references an invalid routing layer");
  }
}

RouteViaLayers makeViaLayers(const TechStore& technology, TechRoutingLayerId bottom, TechRoutingLayerId top)
{
  ensureRoutingLayer(technology, bottom);
  ensureRoutingLayer(technology, top);
  if (bottom == top) {
    throw std::invalid_argument("route VIA must connect two distinct routing layers");
  }
  return {.bottom = bottom, .top = top};
}

RouteViaLayers orderViaLayers(const TechStore& technology, TechRoutingLayerId first, TechRoutingLayerId second)
{
  auto layers = makeViaLayers(technology, first, second);
  if (technology.isBelow(TechLayerId{layers.top.entity()}, TechLayerId{layers.bottom.entity()})) {
    std::swap(layers.bottom, layers.top);
  }
  return layers;
}

RouteViaLayers routeViaLayers(const DesignStore&, const TechStore& technology, TechViaMasterId via)
{
  const auto& vias = technology.viaMasterStorage();
  if (!vias.contains(via)) {
    throw std::invalid_argument("route VIA references an invalid technology VIA");
  }

  // contains() above has already checked the VIA identity. Access its payload
  // directly instead of repeating the same check in geometry().
  const auto& geometry = technology.techRegistry().registry().get<const TechViaGeometry>(via.entity());
  if (geometry.bottom_layer.kind != TechConductorLayerKind::kRouting || geometry.top_layer.kind != TechConductorLayerKind::kRouting) {
    throw std::invalid_argument("route VIA must have routing layers on both sides");
  }
  return makeViaLayers(technology, TechRoutingLayerId{geometry.bottom_layer.entity}, TechRoutingLayerId{geometry.top_layer.entity});
}

RouteViaLayers routeViaLayers(const DesignStore& design, const TechStore& technology, DesignViaId via_id)
{
  const auto& vias = design.routingStorage();
  if (!vias.contains(via_id)) {
    throw std::invalid_argument("route VIA references an invalid design VIA");
  }

  const auto& via = design.designRegistry().registry().get<const DesignVia>(via_id.entity());
  if ((via.flags & DesignViaFlag::kGenerated) != 0u) {
    return makeViaLayers(technology, via.generated.bottom_layer, via.generated.top_layer);
  }

  std::vector<TechRoutingLayerId> routing_layers;
  const auto& geometry = design.designRegistry().geometryRegistry();
  const auto append_routing_layer = [&](TechLayerId layer) {
    const auto routing_layer = TechRoutingLayerId{layer.entity()};
    if (technology.routingLayerStorage().contains(routing_layer)
        && std::find(routing_layers.begin(), routing_layers.end(), routing_layer) == routing_layers.end()) {
      routing_layers.push_back(routing_layer);
    }
  };
  for (const auto shape : via.rectangles) {
    append_routing_layer(geometry.get<DesignViaRectangle>(shape).layer);
  }
  for (const auto shape : via.polygons) {
    append_routing_layer(geometry.get<DesignViaPolygon>(shape).layer);
  }
  if (routing_layers.size() != 2u) {
    throw std::invalid_argument("fixed design VIA must have geometry on exactly two routing layers");
  }
  return orderViaLayers(technology, routing_layers[0], routing_layers[1]);
}

RouteViaLayers routeViaLayers(const DesignStore& design, const TechStore& technology, const RouteViaDefinition& definition)
{
  return std::visit(
      [&](const auto via) -> RouteViaLayers {
        using ViaId = std::remove_cvref_t<decltype(via)>;
        if constexpr (std::is_same_v<ViaId, TechViaMasterId>) {
          return routeViaLayers(design, technology, via);
        } else {
          return routeViaLayers(design, technology, via);
        }
      },
      definition);
}

std::optional<int32_t> pointExtension(DesignWirePoint point)
{
  return (point.flags & DesignWirePointFlag::kHasExtension) != 0u ? std::optional<int32_t>{point.extension} : std::nullopt;
}

RouteShape projectShape(const DesignRouteShape& shape)
{
  // The routing adapter consumes the physical fields represented by
  // RouteShape. Storage owns the complete DEF attributes; this projection
  // does not revalidate them or scan sibling Shapes for anchor extensions.
  const bool tapered = (shape.flags & DesignWirePathFlag::kTaper) != 0u;
  return std::visit(
      [&](const auto& geometry) -> RouteShape {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, DesignRouteSegment>) {
          return RouteSegment{.layer = shape.layer,
                              .begin = geometry.begin.position,
                              .end = geometry.end.position,
                              .width = std::nullopt,
                              .begin_extension = pointExtension(geometry.begin),
                              .end_extension = pointExtension(geometry.end),
                              .tapered = tapered};
        } else if constexpr (std::is_same_v<Geometry, DesignRouteVia>) {
          const RouteViaDefinition definition = geometry.via.tech_via ? RouteViaDefinition{geometry.via.tech_via}
                                                                      : RouteViaDefinition{geometry.via.design_via};
          return RouteVia{.definition = definition,
                          .origin = geometry.origin.position,
                          .orientation = geometry.via.orientation,
                          .tapered = tapered};
        } else if constexpr (std::is_same_v<Geometry, DesignRoutePatch>) {
          return RoutePatch{.layer = shape.layer, .rectangle = geometry.rectangle, .tapered = tapered};
        } else {
          throw std::logic_error("route service cannot project a point-only path");
        }
      },
      shape.geometry);
}

uint32_t routeShapeFlags(bool tapered)
{
  return tapered ? DesignWirePathFlag::kTaper : 0u;
}

DesignWirePoint routePoint(Point position, const std::optional<int32_t>& extension)
{
  DesignWirePoint point{.position = position};
  if (extension.has_value()) {
    point.flags |= DesignWirePointFlag::kHasExtension;
    point.extension = *extension;
  }
  return point;
}

Point patchAnchor(Rect rectangle)
{
  const int64_t width = static_cast<int64_t>(rectangle.ur_x) - rectangle.ll_x;
  const int64_t height = static_cast<int64_t>(rectangle.ur_y) - rectangle.ll_y;
  // A zero anchor preserves all int32 coordinates when an extent cannot be
  // expressed as a positive DEF RECT delta from its lower-left corner.
  if (width > std::numeric_limits<int32_t>::max() || height > std::numeric_limits<int32_t>::max()) {
    return {};
  }
  return {.x = rectangle.ll_x, .y = rectangle.ll_y};
}

DesignRouteShape storedShape(const DesignStore& design,
                             const TechStore& technology,
                             const RouteShape& shape,
                             uint32_t source_path)
{
  return std::visit(
      [&](const auto& value) -> DesignRouteShape {
        using Shape = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<Shape, RouteSegment>) {
          ensureRoutingLayer(technology, value.layer);
          if (value.width.has_value()) {
            // DEF NET paths derive their width from the routing layer. Width is
            // only serializable on SPECIALNET paths, which this service excludes.
            throw std::invalid_argument("ordinary net route segments cannot carry an explicit width");
          }
          return {.wire = {},
                  .layer = value.layer,
                  .flags = routeShapeFlags(value.tapered),
                  .source_path = source_path,
                  .geometry = DesignRouteSegment{.begin = routePoint(value.begin, value.begin_extension),
                                                 .end = routePoint(value.end, value.end_extension)}};
        } else if constexpr (std::is_same_v<Shape, RouteVia>) {
          if (!isValidDesignOrientation(value.orientation)) {
            throw std::invalid_argument("route VIA orientation is invalid");
          }
          const auto layers = routeViaLayers(design, technology, value.definition);
          DesignWireVia via;
          via.orientation = value.orientation;
          std::visit(
              [&](const auto definition) {
                using ViaId = std::remove_cvref_t<decltype(definition)>;
                if constexpr (std::is_same_v<ViaId, TechViaMasterId>) {
                  via.tech_via = definition;
                } else {
                  via.design_via = definition;
                }
              },
              value.definition);
          return {.wire = {},
                  .layer = layers.bottom,
                  .flags = routeShapeFlags(value.tapered),
                  .source_path = source_path,
                  .geometry = DesignRouteVia{.origin = routePoint(value.origin, std::nullopt),
                                             .via = std::move(via),
                                             .exit_layer = layers.top}};
        } else {
          ensureRoutingLayer(technology, value.layer);
          if (!value.rectangle.isValid()) {
            throw std::invalid_argument("route patch rectangle is invalid");
          }
          const auto anchor = patchAnchor(value.rectangle);
          return {.wire = {},
                  .layer = value.layer,
                  .flags = routeShapeFlags(value.tapered),
                  .source_path = source_path,
                  .geometry = DesignRoutePatch{.anchor = routePoint(anchor, std::nullopt),
                                                .rectangle = value.rectangle}};
        }
      },
      shape);
}

}  // namespace

NetRouteService::NetRouteService(DesignStore& design, const TechStore& technology) : _design(design), _technology(technology)
{
  if (&design.techRegistry() != &technology.techRegistry()) {
    throw std::invalid_argument("route service DesignStore and TechStore use different technology registries");
  }
}

NetRouteSnapshot NetRouteService::snapshot(DesignNetId net) const
{
  ensureRegularNet(_design, net);

  const auto& routing = _design.routingStorage();
  NetRouteSnapshot result;
  result.net = net;
  for (const auto wire_id : routing.wireIds(net)) {
    const auto& wire = routing.wire(wire_id);
    auto& snapshot = result.wires.emplace_back();
    snapshot.wire = wire_id;
    snapshot.status = wire.status;
    snapshot.shield_net = wire.shield_net;
    snapshot.shapes.reserve(wire.shapes.size());
    routing.forEachShape(wire_id, [&](DesignRouteShapeId, const DesignRouteShape& shape,
                                      const DesignRouteShapeAttributes*) {
      snapshot.shapes.emplace_back(projectShape(shape));
    });
  }
  return result;
}

DesignWireId NetRouteService::replaceRouted(DesignNetId net, std::span<const RouteShape> shapes,
                                          NetRouteWriteProfile* profile)
{
  using Clock = std::chrono::steady_clock;
  const auto begin = profile ? Clock::now() : Clock::time_point{};
  auto checkpoint = begin;
  const auto record = [&](uint64_t NetRouteWriteProfile::*field) {
    if (profile) {
      const auto end = Clock::now();
      profile->*field += std::chrono::duration_cast<std::chrono::nanoseconds>(end - checkpoint).count();
      checkpoint = end;
    }
  };

  const auto replace = [&]() -> DesignWireId {
    ensureRegularNet(_design, net);

    auto& routing = _design.routingStorage();
    std::vector<DesignWireId> replaced_wires;
    for (const auto wire_id : routing.wireIds(net)) {
      const auto& wire = routing.wire(wire_id);
      if (wire.status == DesignWireStatus::kRouted) {
        replaced_wires.push_back(wire_id);
        if (profile) {
          ++profile->replaced_wires;
          profile->replaced_shapes += wire.shapes.size();
        }
      }
    }
    record(&NetRouteWriteProfile::select_wires_ns);

    if (shapes.empty()) {
      for (const auto wire_id : replaced_wires) {
        if (!routing.destroyWire(wire_id)) {
          throw std::logic_error("routed wire disappeared during replacement");
        }
      }
      record(&NetRouteWriteProfile::destroy_old_ns);
      return {};
    }

    // Build directly into a staged Wire. Until this succeeds, the previous
    // route stays intact; failure discards the staged Wire and its Shapes.
    // This avoids allocating and copying a second complete Shape array.
    DesignWire replacement_wire;
    replacement_wire.net = net;
    replacement_wire.status = DesignWireStatus::kRouted;
    const auto replacement = routing.createWire(std::move(replacement_wire));
    record(&NetRouteWriteProfile::create_wire_ns);
    try {
      auto& routing_owner = routing.wire(replacement);
      auto& registry = routing.geometryRegistry().registry();
      auto& metadata_storage = registry.storage<GeometryShape>();
      auto& shape_storage = registry.storage<DesignRouteShape>();
      routing_owner.shapes.reserve(shapes.size());
      for (std::size_t index = 0; index < shapes.size(); ++index) {
        auto value = storedShape(_design, _technology, shapes[index], static_cast<uint32_t>(index));
        value.wire = replacement;
        // storedShape already checks the input. Append its canonical payload
        // without repeating the public single-Shape edit validation.
        GeometryShape metadata{.owner = GeometryOwner::from(GeometryOwnerDomain::kDesignWire, replacement),
                               .kind = routeShapeKind(value),
                               .layer = value.layer,
                               .flags = value.flags,
                               .source_path = value.source_path};
        const auto entity = registry.create();
        try {
          metadata_storage.emplace(entity, std::move(metadata));
          shape_storage.emplace(entity, std::move(value));
          routing_owner.shapes.emplace_back(entity);
        } catch (...) {
          registry.destroy(entity);
          throw;
        }
      }
      // This fresh Wire uses consecutive source-path ordinals. Update the
      // next ordinal once rather than looking up every just-inserted Shape.
      routing_owner.next_source_path = static_cast<uint32_t>(shapes.size());
      routing.markGeometryChanged();
    } catch (...) {
      static_cast<void>(routing.destroyWire(replacement));
      throw;
    }
    record(&NetRouteWriteProfile::append_shapes_ns);
    for (const auto wire_id : replaced_wires) {
      if (!routing.destroyWire(wire_id)) {
        throw std::logic_error("routed wire disappeared during replacement");
      }
    }
    record(&NetRouteWriteProfile::destroy_old_ns);
    return replacement;
  };
  const auto result = replace();
  if (profile) {
    profile->service_total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
    ++profile->calls;
    profile->input_shapes += shapes.size();
  }
  return result;
}

}  // namespace eccdb
