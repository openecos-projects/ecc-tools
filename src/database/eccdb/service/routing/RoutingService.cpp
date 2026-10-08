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

#include "service/routing/RoutingService.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <unordered_set>

#include "design/DesignStore.h"
#include "design/routing/storage/DesignRoutingStorage.h"
#include "geometry/GeometryMutation.h"
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
    throw std::out_of_range("routing service requires a valid design net");
  }
  if (netlist.isSpecialNet(net)) {
    throw std::invalid_argument("routed Wire replacement only supports ordinary design nets");
  }
}

void ensureRoutingLayer(const TechStore& technology, TechRoutingLayerId layer)
{
  if (!technology.routingLayerStorage().contains(layer)) {
    throw std::invalid_argument("route Shape references an invalid routing layer");
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
      [&](const auto via) -> RouteViaLayers { return routeViaLayers(design, technology, via); }, definition);
}

std::optional<int32_t> pointExtension(DesignWirePoint point)
{
  return (point.flags & DesignWirePointFlag::kHasExtension) != 0u ? std::optional<int32_t>{point.extension} : std::nullopt;
}

RouteShape projectShape(const DesignRouteShape& shape)
{
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
          throw std::logic_error("route snapshot cannot project a point-only Shape");
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
                  .geometry = DesignRoutePatch{.anchor = routePoint(anchor, std::nullopt), .rectangle = value.rectangle}};
        }
      },
      shape);
}

template <typename Id>
std::span<const std::remove_cv_t<Id>> one(const Id& id) noexcept
{
  return std::span<const std::remove_cv_t<Id>>{&id, 1u};
}

}  // namespace

RoutingService::RoutingService(DatabaseContext& context) : _context(context)
{
  if (const auto* technology = context.technology(); technology != nullptr
      && &context.design().techRegistry() != &technology->techRegistry()) {
    throw std::invalid_argument("routing service DesignStore and TechStore use different technology registries");
  }
}

DesignStore& RoutingService::design() noexcept
{
  return _context.design();
}

const DesignStore& RoutingService::design() const noexcept
{
  return _context.design();
}

DatabaseChangeDomain RoutingService::routingGeometryDomains() const noexcept
{
  return DatabaseChangeDomain::kRouting | DatabaseChangeDomain::kGeometry;
}

void RoutingService::notifyBefore(DatabaseChangeDomain domains,
                                  std::span<const DesignInstanceId> instances,
                                  std::span<const DesignNetId> nets,
                                  std::span<const GeometryShapeId> shapes) noexcept
{
  DatabaseChange change;
  change.domains = domains;
  change.instances = instances;
  change.nets = nets;
  change.shapes = shapes;
  _context.notifyBefore(change);
}

void RoutingService::notifyAfter(DatabaseChangeDomain domains,
                                 std::span<const DesignInstanceId> instances,
                                 std::span<const DesignNetId> nets,
                                 std::span<const GeometryShapeId> shapes,
                                 std::span<const GeometryOwner> geometry_replacements) noexcept
{
  DatabaseChange change;
  change.domains = domains;
  change.instances = instances;
  change.nets = nets;
  change.shapes = shapes;
  change.geometry_replacements = geometry_replacements;
  _context.notifyAfter(change);
}

DesignViaId RoutingService::createVia(DesignViaInput via)
{
  notifyBefore(routingGeometryDomains());
  const auto id = design().routingStorage().createVia(std::move(via));
  notifyAfter(routingGeometryDomains());
  return id;
}

DesignViaId RoutingService::findVia(std::string_view name) const
{
  return design().routingStorage().findVia(name);
}

const DesignVia& RoutingService::via(DesignViaId id) const
{
  return design().routingStorage().via(id);
}

void RoutingService::updateVia(DesignViaId id, DesignViaInput via)
{
  notifyBefore(routingGeometryDomains());
  design().routingStorage().updateVia(id, std::move(via));
  const auto owner = designViaOwner(id);
  notifyAfter(routingGeometryDomains(), {}, {}, {}, one(owner));
}

bool RoutingService::destroyVia(DesignViaId id)
{
  // Storage must also inspect route and pin-access references. Avoid a
  // destructive before notification when those references make this a no-op.
  if (design().pinAccessStorage().referencesDesignVia(id) || !design().routingStorage().destroyVia(id)) {
    return false;
  }
  notifyAfter(routingGeometryDomains());
  return true;
}

DesignWireId RoutingService::createWire(DesignWire wire)
{
  auto net = wire.net;
  notifyBefore(routingGeometryDomains(), {}, one(net));
  const auto id = design().routingStorage().createWire(std::move(wire));
  notifyAfter(routingGeometryDomains(), {}, one(net));
  return id;
}

bool RoutingService::destroyWire(DesignWireId id)
{
  auto& routing = design().routingStorage();
  if (!routing.contains(id)) {
    return false;
  }
  const auto net = routing.wire(id).net;
  const auto shapes = routing.wire(id).shapes;
  auto changed_net = net;
  notifyBefore(routingGeometryDomains(), {}, one(changed_net), shapes);
  if (!routing.destroyWire(id)) {
    return false;
  }
  notifyAfter(routingGeometryDomains(), {}, one(changed_net), shapes);
  return true;
}

bool RoutingService::contains(DesignWireId id) const
{
  return design().routingStorage().contains(id);
}

const DesignWire& RoutingService::wire(DesignWireId id) const
{
  return design().routingStorage().wire(id);
}

GeometryShapeId RoutingService::createShape(DesignWireId wire_id,
                                            DesignRouteShape shape,
                                            std::optional<DesignRouteShapeAttributes> attributes)
{
  auto& routing = design().routingStorage();
  auto net = routing.wire(wire_id).net;
  notifyBefore(routingGeometryDomains(), {}, one(net));
  const auto id = routing.createShape(wire_id, std::move(shape), std::move(attributes));
  auto changed_shape = id;
  notifyAfter(routingGeometryDomains(), {}, one(net), one(changed_shape));
  return id;
}

void RoutingService::updateShape(GeometryShapeId id,
                                 DesignRouteShape shape,
                                 std::optional<DesignRouteShapeAttributes> attributes)
{
  const auto mutation = detail::captureGeometryMutation(design(), id);
  _context.notifyBefore(mutation.change());
  design().routingStorage().updateShape(id, std::move(shape), std::move(attributes));
  detail::finishGeometryMutation(design(), mutation);
  _context.notifyAfter(mutation.change());
}

bool RoutingService::destroyShape(GeometryShapeId id)
{
  auto& routing = design().routingStorage();
  if (!routing.contains(id)) {
    return false;
  }
  const auto mutation = detail::captureGeometryMutation(design(), id);
  _context.notifyBefore(mutation.change(true));
  if (!routing.destroyShape(id)) {
    return false;
  }
  detail::finishGeometryMutation(design(), mutation, true);
  _context.notifyAfter(mutation.change(true));
  return true;
}

bool RoutingService::contains(GeometryShapeId id) const
{
  return design().routingStorage().contains(id);
}

const DesignRouteShape& RoutingService::shape(GeometryShapeId id) const
{
  return design().routingStorage().shape(id);
}

const DesignRouteShapeAttributes* RoutingService::shapeAttributes(GeometryShapeId id) const
{
  return design().routingStorage().shapeAttributes(id);
}

Rect RoutingService::shapeBoundingBox(GeometryShapeId id) const
{
  return design().routingStorage().shapeBoundingBox(id);
}

NetRouteSnapshot RoutingService::snapshot(DesignNetId net) const
{
  ensureRegularNet(design(), net);

  const auto& routing = design().routingStorage();
  NetRouteSnapshot result;
  result.net = net;
  for (const auto wire_id : routing.wireIds(net)) {
    const auto& wire_value = routing.wire(wire_id);
    auto& snapshot_value = result.wires.emplace_back();
    snapshot_value.wire = wire_id;
    snapshot_value.status = wire_value.status;
    snapshot_value.shield_net = wire_value.shield_net;
    snapshot_value.shapes.reserve(wire_value.shapes.size());
    routing.forEachShape(wire_id, [&](GeometryShapeId, const DesignRouteShape& shape_value, const DesignRouteShapeAttributes*) {
      snapshot_value.shapes.emplace_back(projectShape(shape_value));
    });
  }
  return result;
}

DesignWireId RoutingService::replaceRouted(DesignNetId net,
                                           std::span<const RouteShape> shapes,
                                           NetRouteWriteProfile* profile)
{
  using Clock = std::chrono::steady_clock;
  const auto begin = profile != nullptr ? Clock::now() : Clock::time_point{};
  auto checkpoint = begin;
  const auto record = [&](uint64_t NetRouteWriteProfile::*field) {
    if (profile != nullptr) {
      const auto end = Clock::now();
      profile->*field += std::chrono::duration_cast<std::chrono::nanoseconds>(end - checkpoint).count();
      checkpoint = end;
    }
  };

  ensureRegularNet(design(), net);
  const auto* technology = _context.technology();
  if (technology == nullptr) {
    throw std::logic_error("routed Wire replacement requires a TechStore in DatabaseContext");
  }

  auto& routing = design().routingStorage();
  std::vector<DesignWireId> replaced_wires;
  for (const auto wire_id : routing.wireIds(net)) {
    const auto& wire_value = routing.wire(wire_id);
    if (wire_value.status == DesignWireStatus::kRouted) {
      replaced_wires.push_back(wire_id);
      if (profile != nullptr) {
        ++profile->replaced_wires;
        profile->replaced_shapes += wire_value.shapes.size();
      }
    }
  }
  record(&NetRouteWriteProfile::select_wires_ns);

  if (replaced_wires.empty() && shapes.empty()) {
    if (profile != nullptr) {
      profile->service_total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
      ++profile->calls;
    }
    return {};
  }

  auto changed_net = net;
  notifyBefore(routingGeometryDomains(), {}, one(changed_net));
  DesignWireId result;
  if (shapes.empty()) {
    for (const auto wire_id : replaced_wires) {
      if (!routing.destroyWire(wire_id)) {
        throw std::logic_error("routed Wire disappeared during replacement");
      }
    }
    record(&NetRouteWriteProfile::destroy_old_ns);
  } else {
    DesignWire replacement_wire;
    replacement_wire.net = net;
    replacement_wire.status = DesignWireStatus::kRouted;
    const auto replacement = routing.createWire(std::move(replacement_wire));
    record(&NetRouteWriteProfile::create_wire_ns);
    try {
      auto& owner = routing.wire(replacement);
      auto& registry = routing.geometryRegistry().registry();
      auto& metadata_storage = registry.storage<GeometryShape>();
      auto& shape_storage = registry.storage<DesignRouteShape>();
      owner.shapes.reserve(shapes.size());
      for (std::size_t index = 0; index < shapes.size(); ++index) {
        auto value = storedShape(design(), *technology, shapes[index], static_cast<uint32_t>(index));
        value.wire = replacement;
        GeometryShape metadata{.owner = GeometryOwner::from(GeometryOwnerDomain::kDesignWire, replacement),
                               .kind = routeShapeKind(value),
                               .layer = value.layer,
                               .flags = value.flags,
                               .source_path = value.source_path};
        const auto entity = registry.create();
        try {
          metadata_storage.emplace(entity, std::move(metadata));
          shape_storage.emplace(entity, std::move(value));
          owner.shapes.emplace_back(entity);
        } catch (...) {
          registry.destroy(entity);
          throw;
        }
      }
      owner.next_source_path = static_cast<uint32_t>(shapes.size());
      routing.markGeometryChanged();
    } catch (...) {
      static_cast<void>(routing.destroyWire(replacement));
      throw;
    }
    record(&NetRouteWriteProfile::append_shapes_ns);
    for (const auto wire_id : replaced_wires) {
      if (!routing.destroyWire(wire_id)) {
        throw std::logic_error("routed Wire disappeared during replacement");
      }
    }
    record(&NetRouteWriteProfile::destroy_old_ns);
    result = replacement;
  }

  notifyAfter(routingGeometryDomains(), {}, one(changed_net));
  if (profile != nullptr) {
    profile->service_total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
    ++profile->calls;
    profile->input_shapes += shapes.size();
  }
  return result;
}

std::vector<DesignWireId> RoutingService::commitRoutedResults(std::span<const RoutedNetResult> results)
{
  struct Target { std::size_t index; std::vector<DesignWireId> old_wires; };
  std::vector<Target> targets;
  std::vector<DesignNetId> changed_nets;
  std::vector<DesignWireId> replacements(results.size());
  std::unordered_set<uint64_t> seen;
  targets.reserve(results.size());
  changed_nets.reserve(results.size());
  seen.reserve(results.size());
  auto& routing = design().routingStorage();
  for (std::size_t index = 0; index < results.size(); ++index) {
    const auto& result = results[index];
    if (!seen.insert(result.net.packed()).second) throw std::invalid_argument("duplicate Net in routed results");
    if (result.outcome == RoutedResultOutcome::kPreserve || result.outcome == RoutedResultOutcome::kSkipped
        || result.outcome == RoutedResultOutcome::kFailed) continue;
    ensureRegularNet(design(), result.net);
    if ((result.outcome == RoutedResultOutcome::kClear && !result.shapes.empty())
        || (result.outcome == RoutedResultOutcome::kReplace && result.shapes.empty())
        || (!result.attributes.empty() && result.attributes.size() != result.shapes.size())
        || (result.outcome != RoutedResultOutcome::kClear && result.outcome != RoutedResultOutcome::kReplace))
      throw std::invalid_argument("routed result requires explicit Clear or nonempty Replace");
    Target target{.index = index};
    for (const auto wire : routing.wireIds(result.net)) {
      if (routing.wire(wire).status == DesignWireStatus::kRouted) target.old_wires.push_back(wire);
    }
    if (!target.old_wires.empty() || !result.shapes.empty()) {
      targets.push_back(std::move(target));
      changed_nets.push_back(result.net);
    }
  }
  if (targets.empty()) return replacements;
  try {
    for (const auto& target : targets) {
      const auto& result = results[target.index];
      if (result.outcome != RoutedResultOutcome::kReplace) continue;
      const auto wire = routing.createWire(DesignWire{.net = result.net, .status = DesignWireStatus::kRouted});
      replacements[target.index] = wire;
      for (std::size_t index = 0; index < result.shapes.size(); ++index) {
        static_cast<void>(routing.createShape(wire, result.shapes[index],
                                               result.attributes.empty() ? std::nullopt : result.attributes[index]));
      }
    }
  } catch (...) {
    for (const auto wire : replacements) if (wire) static_cast<void>(routing.destroyWire(wire));
    throw;
  }
  // The single-threaded commit suffix cannot allocate: all replacements and
  // notifications were prepared while the original routes were still intact.
  notifyBefore(routingGeometryDomains(), {}, changed_nets);
  for (const auto& target : targets)
    for (const auto wire : target.old_wires) static_cast<void>(routing.destroyWire(wire));
  notifyAfter(routingGeometryDomains(), {}, changed_nets);
  return replacements;
}

void RoutingService::replacePinAccess(DesignPinAccessUpdate update)
{
  if (update.master_ports.empty() && update.block_pins.empty() && update.instances.empty()) return;
  std::vector<DesignInstanceId> instances;
  instances.reserve(update.instances.size());
  for (const auto& instance : update.instances) instances.push_back(instance.instance);
  design().pinAccessStorage().replacePinAccess(std::move(update));
  notifyAfter(DatabaseChangeDomain::kPinAccess, instances);
}

bool RoutingService::hasGuides(DesignNetId net) const
{
  return design().guideStorage().hasGuides(net);
}

std::span<const GeometryShapeId> RoutingService::guides(DesignNetId net) const
{
  return design().guideStorage().guides(net);
}

const DesignRouteGuide& RoutingService::guide(GeometryShapeId id) const
{
  return design().guideStorage().guide(id);
}

void RoutingService::replaceGuides(DesignNetId net, std::vector<DesignRouteGuide> guides)
{
  replaceGuideStage(net, std::move(guides), {});
}

void RoutingService::clearGuides(DesignNetId net)
{
  if (!design().guideStorage().hasGuides(net) && !design().guideStorage().hasTrackAssignments(net)) {
    return;
  }
  replaceGuideStage(net, std::vector<DesignRouteGuide>{}, {});
}

void RoutingService::replaceGuideStage(DesignNetId net,
                                      std::optional<std::vector<DesignRouteGuide>> guides,
                                      std::vector<DesignTrackAssignmentShape> assignments)
{
  auto changed_net = net;
  notifyBefore(routingGeometryDomains(), {}, one(changed_net));
  const bool replace_guides = guides.has_value();
  design().guideStorage().replaceGuideStage(net, std::move(guides), std::move(assignments));
  const std::array owners{designTrackAssignmentOwner(net), designGuideOwner(net)};
  notifyAfter(routingGeometryDomains(), {}, one(changed_net), {},
              std::span<const GeometryOwner>{owners.data(), replace_guides ? 2u : 1u});
}

bool RoutingService::hasTrackAssignments(DesignNetId net) const
{
  return design().guideStorage().hasTrackAssignments(net);
}

std::span<const GeometryShapeId> RoutingService::trackAssignments(DesignNetId net) const
{
  return design().guideStorage().trackAssignments(net);
}

const DesignTrackAssignmentShape& RoutingService::trackAssignment(GeometryShapeId id) const
{
  return design().guideStorage().trackAssignment(id);
}

void RoutingService::replaceTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> assignments)
{
  auto changed_net = net;
  notifyBefore(routingGeometryDomains(), {}, one(changed_net));
  design().guideStorage().replaceTrackAssignments(net, std::move(assignments));
  const auto owner = designTrackAssignmentOwner(net);
  notifyAfter(routingGeometryDomains(), {}, one(changed_net), {}, one(owner));
}

void RoutingService::appendTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> assignments)
{
  if (assignments.empty()) {
    return;
  }
  auto changed_net = net;
  notifyBefore(routingGeometryDomains(), {}, one(changed_net));
  design().guideStorage().appendTrackAssignments(net, std::move(assignments));
  notifyAfter(routingGeometryDomains(), {}, one(changed_net));
}

void RoutingService::clearTrackAssignments(DesignNetId net)
{
  if (!design().guideStorage().hasTrackAssignments(net)) {
    return;
  }
  auto changed_net = net;
  notifyBefore(routingGeometryDomains(), {}, one(changed_net));
  design().guideStorage().clearTrackAssignments(net);
  const auto owner = designTrackAssignmentOwner(net);
  notifyAfter(routingGeometryDomains(), {}, one(changed_net), {}, one(owner));
}

bool RoutingService::hasMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index) const
{
  return design().pinAccessStorage().hasMasterPortCandidateGroup(master_port, group_index);
}

std::span<const DesignPinAccessCandidateGroup> RoutingService::masterPortCandidateGroups(LibraryMasterPortId master_port) const
{
  return design().pinAccessStorage().masterPortCandidateGroups(master_port);
}

const DesignPinAccessCandidateGroup& RoutingService::masterPortCandidateGroup(LibraryMasterPortId master_port,
                                                                                uint32_t group_index) const
{
  return design().pinAccessStorage().masterPortCandidateGroup(master_port, group_index);
}

void RoutingService::setMasterPortCandidateGroup(LibraryMasterPortId master_port,
                                                  uint32_t group_index,
                                                  std::vector<DesignPinAccessPoint> points)
{
  notifyBefore(DatabaseChangeDomain::kPinAccess);
  design().pinAccessStorage().setMasterPortCandidateGroup(master_port, group_index, std::move(points));
  notifyAfter(DatabaseChangeDomain::kPinAccess);
}

bool RoutingService::clearMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index)
{
  if (!design().pinAccessStorage().clearMasterPortCandidateGroup(master_port, group_index)) {
    return false;
  }
  notifyAfter(DatabaseChangeDomain::kPinAccess);
  return true;
}

bool RoutingService::clearMasterPortCandidateGroups(LibraryMasterPortId master_port)
{
  if (!design().pinAccessStorage().clearMasterPortCandidateGroups(master_port)) {
    return false;
  }
  notifyAfter(DatabaseChangeDomain::kPinAccess);
  return true;
}

bool RoutingService::hasBlockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index) const
{
  return design().pinAccessStorage().hasBlockPinCandidateGroup(pin, group_index);
}

std::span<const DesignPinAccessCandidateGroup> RoutingService::blockPinCandidateGroups(DesignBlockPinId pin) const
{
  return design().pinAccessStorage().blockPinCandidateGroups(pin);
}

const DesignPinAccessCandidateGroup& RoutingService::blockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index) const
{
  return design().pinAccessStorage().blockPinCandidateGroup(pin, group_index);
}

void RoutingService::setBlockPinCandidateGroup(DesignBlockPinId pin,
                                                uint32_t group_index,
                                                std::vector<DesignPinAccessPoint> points)
{
  notifyBefore(DatabaseChangeDomain::kPinAccess);
  design().pinAccessStorage().setBlockPinCandidateGroup(pin, group_index, std::move(points));
  notifyAfter(DatabaseChangeDomain::kPinAccess);
}

void RoutingService::appendBlockPinCandidatePoints(DesignBlockPinId pin,
                                                   uint32_t group_index,
                                                   std::vector<DesignPinAccessPoint> points)
{
  if (points.empty()) {
    return;
  }
  notifyBefore(DatabaseChangeDomain::kPinAccess);
  design().pinAccessStorage().appendBlockPinCandidatePoints(pin, group_index, std::move(points));
  notifyAfter(DatabaseChangeDomain::kPinAccess);
}

bool RoutingService::clearBlockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index)
{
  if (!design().pinAccessStorage().clearBlockPinCandidateGroup(pin, group_index)) {
    return false;
  }
  notifyAfter(DatabaseChangeDomain::kPinAccess);
  return true;
}

bool RoutingService::clearBlockPinCandidateGroups(DesignBlockPinId pin)
{
  if (!design().pinAccessStorage().clearBlockPinCandidateGroups(pin)) {
    return false;
  }
  notifyAfter(DatabaseChangeDomain::kPinAccess);
  return true;
}

bool RoutingService::hasIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index) const
{
  return design().pinAccessStorage().hasIoPinPortCandidateGroup(pin, port_index, group_index);
}

std::span<const DesignPinAccessCandidateGroup> RoutingService::ioPinPortCandidateGroups(DesignIoPinId pin,
                                                                                            uint32_t port_index) const
{
  return design().pinAccessStorage().ioPinPortCandidateGroups(pin, port_index);
}

const DesignPinAccessCandidateGroup& RoutingService::ioPinPortCandidateGroup(DesignIoPinId pin,
                                                                                uint32_t port_index,
                                                                                uint32_t group_index) const
{
  return design().pinAccessStorage().ioPinPortCandidateGroup(pin, port_index, group_index);
}

void RoutingService::setIoPinPortCandidateGroup(DesignIoPinId pin,
                                                 uint32_t port_index,
                                                 uint32_t group_index,
                                                 std::vector<DesignPinAccessPoint> points)
{
  notifyBefore(DatabaseChangeDomain::kPinAccess);
  design().pinAccessStorage().setIoPinPortCandidateGroup(pin, port_index, group_index, std::move(points));
  notifyAfter(DatabaseChangeDomain::kPinAccess);
}

void RoutingService::appendIoPinPortCandidatePoints(DesignIoPinId pin,
                                                    uint32_t port_index,
                                                    uint32_t group_index,
                                                    std::vector<DesignPinAccessPoint> points)
{
  if (points.empty()) {
    return;
  }
  notifyBefore(DatabaseChangeDomain::kPinAccess);
  design().pinAccessStorage().appendIoPinPortCandidatePoints(pin, port_index, group_index, std::move(points));
  notifyAfter(DatabaseChangeDomain::kPinAccess);
}

bool RoutingService::clearIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index)
{
  if (!design().pinAccessStorage().clearIoPinPortCandidateGroup(pin, port_index, group_index)) {
    return false;
  }
  notifyAfter(DatabaseChangeDomain::kPinAccess);
  return true;
}

bool RoutingService::clearIoPinPortCandidateGroups(DesignIoPinId pin, uint32_t port_index)
{
  if (!design().pinAccessStorage().clearIoPinPortCandidateGroups(pin, port_index)) {
    return false;
  }
  notifyAfter(DatabaseChangeDomain::kPinAccess);
  return true;
}

void RoutingService::setInstancePinAccessGroup(DesignInstanceId instance, uint32_t group_index)
{
  auto changed_instance = instance;
  notifyBefore(DatabaseChangeDomain::kPinAccess, one(changed_instance));
  design().pinAccessStorage().setInstancePinAccessGroup(instance, group_index);
  notifyAfter(DatabaseChangeDomain::kPinAccess, one(changed_instance));
}

std::optional<uint32_t> RoutingService::instancePinAccessGroup(DesignInstanceId instance) const
{
  return design().pinAccessStorage().instancePinAccessGroup(instance);
}

bool RoutingService::clearInstancePinAccessGroup(DesignInstanceId instance)
{
  auto changed_instance = instance;
  if (!design().pinAccessStorage().instancePinAccessGroup(instance).has_value()) {
    return false;
  }
  notifyBefore(DatabaseChangeDomain::kPinAccess, one(changed_instance));
  if (!design().pinAccessStorage().clearInstancePinAccessGroup(instance)) {
    return false;
  }
  notifyAfter(DatabaseChangeDomain::kPinAccess, one(changed_instance));
  return true;
}

void RoutingService::setPreferredInstancePinAccess(DesignInstancePinId pin,
                                                   std::vector<DesignPreferredPinAccessRef> preferences)
{
  const auto instance = design().netlistStorage().instanceTerm(pin).instance;
  auto changed_instance = instance;
  notifyBefore(DatabaseChangeDomain::kPinAccess, one(changed_instance));
  design().pinAccessStorage().setPreferredInstancePinAccess(pin, std::move(preferences));
  notifyAfter(DatabaseChangeDomain::kPinAccess, one(changed_instance));
}

std::span<const DesignPreferredPinAccessRef> RoutingService::preferredInstancePinAccess(DesignInstancePinId pin) const
{
  return design().pinAccessStorage().preferredInstancePinAccess(pin);
}

bool RoutingService::clearPreferredInstancePinAccess(DesignInstancePinId pin)
{
  const auto instance = design().netlistStorage().instanceTerm(pin).instance;
  if (design().pinAccessStorage().preferredInstancePinAccess(pin).empty()) {
    return false;
  }
  auto changed_instance = instance;
  notifyBefore(DatabaseChangeDomain::kPinAccess, one(changed_instance));
  if (!design().pinAccessStorage().clearPreferredInstancePinAccess(pin)) {
    return false;
  }
  notifyAfter(DatabaseChangeDomain::kPinAccess, one(changed_instance));
  return true;
}

}  // namespace eccdb
