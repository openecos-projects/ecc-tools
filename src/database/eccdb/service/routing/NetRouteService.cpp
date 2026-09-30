// SPDX-License-Identifier: Apache-2.0
#include "service/routing/NetRouteService.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>

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

  const auto& geometry = vias.geometry(via);
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

  const auto& via = vias.via(via_id);
  if ((via.flags & DesignViaFlag::kGenerated) != 0u) {
    return makeViaLayers(technology, via.generated.bottom_layer, via.generated.top_layer);
  }

  std::vector<TechRoutingLayerId> routing_layers;
  const auto append_routing_layer = [&](TechLayerId layer) {
    const auto routing_layer = TechRoutingLayerId{layer.entity()};
    if (technology.routingLayerStorage().contains(routing_layer)
        && std::find(routing_layers.begin(), routing_layers.end(), routing_layer) == routing_layers.end()) {
      routing_layers.push_back(routing_layer);
    }
  };
  for (const auto& rectangle : via.rectangles) {
    append_routing_layer(rectangle.layer);
  }
  for (const auto& polygon : via.polygons) {
    append_routing_layer(polygon.layer);
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

RouteViaDefinition routeViaDefinition(const DesignWireVia& via)
{
  if (via.tech_via && !via.design_via) {
    return RouteViaDefinition{via.tech_via};
  }
  if (via.design_via && !via.tech_via) {
    return RouteViaDefinition{via.design_via};
  }
  throw std::logic_error("wire VIA does not have exactly one definition");
}

TechRoutingLayerId nextRouteLayer(TechRoutingLayerId current, RouteViaLayers layers)
{
  if (current == layers.bottom) {
    return layers.top;
  }
  if (current == layers.top) {
    return layers.bottom;
  }
  throw std::logic_error("wire VIA does not touch the active routing layer");
}

std::optional<int32_t> pointExtension(DesignWirePoint point)
{
  return (point.flags & DesignWirePointFlag::kHasExtension) != 0u ? std::optional<int32_t>{point.extension} : std::nullopt;
}

void ensureProjectablePathMetadata(DesignWirePathView path)
{
  // RouteShape intentionally only models physical segment, via, and patch
  // geometry. TAPER has a RouteShape representation; every other path-level
  // attribute below would be lost by a snapshot and must remain explicit.
  constexpr uint32_t kProjectableFlags = DesignWirePathFlag::kTaper;
  if ((path.flags() & ~kProjectableFlags) != 0u || path.width() != 0 || path.mask() != 0u || !path.taperRule().empty()
      || !path.shape().empty() || path.style() != 0) {
    throw std::logic_error("route service cannot project path width, mask, taper rule, shape, or style");
  }

  const auto points = path.points();
  if (points.empty()) {
    throw std::logic_error("route service cannot project a path without points");
  }
  if (points.size() == 1u && path.vias().empty() && path.rectangles().empty()) {
    throw std::logic_error("route service cannot project a point-only path");
  }

  constexpr uint32_t kKnownPointFlags = DesignWirePointFlag::kHasExtension | DesignWirePointFlag::kVirtual;
  for (const auto point : points) {
    const bool has_extension = (point.flags & DesignWirePointFlag::kHasExtension) != 0u;
    if ((point.flags & ~kKnownPointFlags) != 0u || (!has_extension && point.extension != 0)) {
      throw std::logic_error("route service cannot project unknown point attributes");
    }
    if ((point.flags & DesignWirePointFlag::kVirtual) != 0u) {
      throw std::logic_error("virtual wire points cannot be projected as physical route geometry");
    }
    // Extensions describe a segment endpoint. A path with only one point has
    // no segment to carry that value into RouteShape.
    if (points.size() == 1u && has_extension) {
      throw std::logic_error("route service cannot project an extension without a segment");
    }
  }

  for (const auto via : path.vias()) {
    if (!isValidDesignOrientation(via.orientation)) {
      throw std::logic_error("route service cannot project an invalid VIA orientation");
    }
    if (via.flags != 0u || via.top_mask != 0u || via.cut_mask != 0u || via.bottom_mask != 0u || via.rows != 1u
        || via.columns != 1u || via.step_x != 0 || via.step_y != 0) {
      throw std::logic_error("route service cannot project VIA masks or arrays");
    }
  }
}

Rect absolutePatch(DesignWireRectangle rectangle, Point anchor)
{
  const auto checked_offset = [](int32_t value, int32_t offset) -> int32_t {
    const int64_t result = static_cast<int64_t>(value) + offset;
    if (result < std::numeric_limits<int32_t>::min() || result > std::numeric_limits<int32_t>::max()) {
      throw std::overflow_error("wire patch coordinate overflows int32");
    }
    return static_cast<int32_t>(result);
  };
  return {.ll_x = checked_offset(rectangle.delta.ll_x, anchor.x),
          .ll_y = checked_offset(rectangle.delta.ll_y, anchor.y),
          .ur_x = checked_offset(rectangle.delta.ur_x, anchor.x),
          .ur_y = checked_offset(rectangle.delta.ur_y, anchor.y)};
}

// Wire primitives normally arrive in point order, so retain that zero-allocation
// path. Trusted import data can be unordered; in that case an index order keeps
// the stored value order intact for primitives sharing an anchor.
template <typename Range>
class AnchoredPrimitiveCursor
{
 public:
  using Value = typename Range::value_type;

  AnchoredPrimitiveCursor(Range values, std::size_t point_count, const char* out_of_range_message)
      : _values(std::move(values))
  {
    bool monotonic = true;
    uint32_t previous_anchor = 0u;
    for (std::size_t index = 0; index < _values.size(); ++index) {
      const auto value = _values[index];
      if (value.point_index >= point_count) {
        throw std::logic_error(out_of_range_message);
      }
      monotonic = monotonic && (index == 0u || previous_anchor <= value.point_index);
      previous_anchor = value.point_index;
    }

    if (!monotonic) {
      _order.resize(_values.size());
      std::iota(_order.begin(), _order.end(), std::size_t{0});
      std::stable_sort(_order.begin(), _order.end(), [this](std::size_t lhs, std::size_t rhs) {
        return _values[lhs].point_index < _values[rhs].point_index;
      });
    }
  }

  [[nodiscard]] bool at(std::size_t point_index) const
  {
    return _cursor < _values.size() && valueAt(_cursor).point_index == point_index;
  }

  [[nodiscard]] Value take()
  {
    return valueAt(_cursor++);
  }

 private:
  [[nodiscard]] Value valueAt(std::size_t index) const
  {
    return _values[_order.empty() ? index : _order[index]];
  }

  Range _values;
  std::vector<std::size_t> _order;
  std::size_t _cursor = 0u;
};

template <typename Sink>
void visitPathShapes(const DesignStore& design, const TechStore& technology, DesignWirePathView path, Sink&& sink)
{
  ensureProjectablePathMetadata(path);
  const auto points = path.points();
  const bool tapered = (path.flags() & DesignWirePathFlag::kTaper) != 0u;
  AnchoredPrimitiveCursor vias{path.vias(), points.size(), "wire VIA anchor is out of range"};
  AnchoredPrimitiveCursor patches{path.rectangles(), points.size(), "wire patch anchor is out of range"};

  TechRoutingLayerId current_layer = path.layer();
  ensureRoutingLayer(technology, current_layer);
  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto point = points[index];
    if (index != 0u) {
      const auto previous = points[index - 1u];
      sink(RouteShape{RouteSegment{.layer = current_layer,
                                   .begin = previous.position,
                                   .end = point.position,
                                   .width = std::nullopt,
                                   .begin_extension = pointExtension(previous),
                                   .end_extension = pointExtension(point),
                                   .tapered = tapered}});
    }

    if (vias.at(index) && patches.at(index)) {
      throw std::logic_error("wire path has ambiguous VIA and RECT ordering at one anchor");
    }
    while (patches.at(index)) {
      const auto patch = patches.take();
      if (!patch.delta.isValid()) {
        throw std::logic_error("wire path has an invalid patch rectangle");
      }
      sink(RouteShape{RoutePatch{.layer = current_layer, .rectangle = absolutePatch(patch, point.position), .tapered = tapered}});
    }
    while (vias.at(index)) {
      const auto via = vias.take();
      const auto definition = routeViaDefinition(via);
      const auto layers = routeViaLayers(design, technology, definition);
      const auto next_layer = nextRouteLayer(current_layer, layers);
      sink(RouteShape{RouteVia{.definition = definition,
                               .origin = point.position,
                               .orientation = via.orientation,
                               .tapered = tapered}});
      current_layer = next_layer;
    }
  }
}

void appendPathShapes(const DesignStore& design, const TechStore& technology, DesignWirePathView path,
                      std::vector<RouteShape>& output)
{
  visitPathShapes(design, technology, path, [&](RouteShape shape) { output.emplace_back(std::move(shape)); });
}

DesignWirePath canonicalPath(const DesignStore& design, const TechStore& technology, const RouteShape& shape)
{
  return std::visit(
      [&](const auto& value) -> DesignWirePath {
        using Shape = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<Shape, RouteSegment>) {
          ensureRoutingLayer(technology, value.layer);
          if (value.width.has_value()) {
            // DEF NET paths derive their width from the routing layer. Width is
            // only serializable on SPECIALNET paths, which this service excludes.
            throw std::invalid_argument("ordinary net route segments cannot carry an explicit width");
          }
          DesignWirePath path;
          path.layer = value.layer;
          if (value.tapered) {
            path.flags |= DesignWirePathFlag::kTaper;
          }
          path.points = {DesignWirePoint{.position = value.begin}, DesignWirePoint{.position = value.end}};
          if (value.begin_extension.has_value()) {
            path.points[0].flags |= DesignWirePointFlag::kHasExtension;
            path.points[0].extension = *value.begin_extension;
          }
          if (value.end_extension.has_value()) {
            path.points[1].flags |= DesignWirePointFlag::kHasExtension;
            path.points[1].extension = *value.end_extension;
          }
          return path;
        } else if constexpr (std::is_same_v<Shape, RouteVia>) {
          if (!isValidDesignOrientation(value.orientation)) {
            throw std::invalid_argument("route VIA orientation is invalid");
          }
          const auto layers = routeViaLayers(design, technology, value.definition);
          DesignWireVia via;
          via.point_index = 0u;
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
          DesignWirePath path;
          path.layer = layers.bottom;
          if (value.tapered) {
            path.flags |= DesignWirePathFlag::kTaper;
          }
          path.points = {DesignWirePoint{.position = value.origin}};
          path.vias = {via};
          return path;
        } else {
          ensureRoutingLayer(technology, value.layer);
          if (!value.rectangle.isValid()) {
            throw std::invalid_argument("route patch rectangle is invalid");
          }

          const int64_t width = static_cast<int64_t>(value.rectangle.ur_x) - value.rectangle.ll_x;
          const int64_t height = static_cast<int64_t>(value.rectangle.ur_y) - value.rectangle.ll_y;
          Point anchor{.x = value.rectangle.ll_x, .y = value.rectangle.ll_y};
          Rect delta;
          if (width <= std::numeric_limits<int32_t>::max() && height <= std::numeric_limits<int32_t>::max()) {
            delta = {.ll_x = 0, .ll_y = 0, .ur_x = static_cast<int32_t>(width), .ur_y = static_cast<int32_t>(height)};
          } else {
            // A zero anchor preserves all int32 coordinates when the extent is
            // too large to express relative to the lower-left corner.
            anchor = {};
            delta = value.rectangle;
          }
          DesignWirePath path;
          path.layer = value.layer;
          if (value.tapered) {
            path.flags |= DesignWirePathFlag::kTaper;
          }
          path.points = {DesignWirePoint{.position = anchor}};
          path.rectangles = {{.point_index = 0u, .delta = delta}};
          return path;
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
    routing.forEachPath(wire_id, [&](DesignWirePathView path) { appendPathShapes(_design, _technology, path, snapshot.shapes); });
  }
  return result;
}

DesignWireId NetRouteService::replaceRouted(DesignNetId net, std::span<const RouteShape> shapes)
{
  ensureRegularNet(_design, net);

  auto& routing = _design.routingStorage();
  std::vector<DesignWireId> replaced_wires;
  for (const auto wire_id : routing.wireIds(net)) {
    if (routing.wire(wire_id).status == DesignWireStatus::kRouted) {
      replaced_wires.push_back(wire_id);
    }
  }

  if (shapes.empty()) {
    for (const auto wire_id : replaced_wires) {
      if (!routing.destroyWire(wire_id)) {
        throw std::logic_error("routed wire disappeared during replacement");
      }
    }
    return {};
  }

  DesignWireRoutingInput candidate;
  candidate.reservePaths(shapes.size());
  for (const auto& shape : shapes) {
    candidate.appendPath(canonicalPath(_design, _technology, shape));
  }

  // Create before deleting old routed wires: Storage validates the complete
  // candidate first, so an invalid replacement leaves the old route untouched.
  DesignWire replacement_wire;
  replacement_wire.net = net;
  replacement_wire.status = DesignWireStatus::kRouted;
  const auto replacement = routing.createWire(std::move(replacement_wire), std::move(candidate));
  for (const auto wire_id : replaced_wires) {
    if (!routing.destroyWire(wire_id)) {
      throw std::logic_error("routed wire disappeared during replacement");
    }
  }
  return replacement;
}

}  // namespace eccdb
