#include "query/GeometryPayloadService.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "api/internal/DatabaseState.h"
#include "design/DesignStore.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/geometry/DesignGeometryStorage.h"
#include "design/guide/model/GuideComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/routing/storage/DesignRoutingStorage.h"
#include "design/via/component/ViaComponents.h"
#include "geometry/DomainGeometryComponents.h"
#include "library/LibraryStore.h"
#include "service/query/PhysicalDesignService.h"
#include "tech/TechStore.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {
namespace {

constexpr uint64_t kNoId = std::numeric_limits<uint64_t>::max();

[[nodiscard]] GeometryShapeId toShapeId(uint64_t value)
{
  return GeometryShapeId{static_cast<GeometryEntity>(value)};
}

template <typename Id>
[[nodiscard]] uint64_t packedOrNone(Id id) noexcept
{
  return id ? static_cast<uint64_t>(id.packed()) : kNoId;
}

[[nodiscard]] uint32_t checkedIndex(std::size_t value, const char* field)
{
  if (value > std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error(std::string{"geometry payload "} + field + " exceeds uint32 range");
  }
  return static_cast<uint32_t>(value);
}

[[nodiscard]] int32_t checkedCoordinate(int64_t value)
{
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    throw std::overflow_error("geometry payload coordinate exceeds int32 range");
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
    default: throw std::invalid_argument("geometry payload has an invalid orientation");
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
  for (const auto point : corners) {
    result.ll_x = std::min(result.ll_x, point.x);
    result.ll_y = std::min(result.ll_y, point.y);
    result.ur_x = std::max(result.ur_x, point.x);
    result.ur_y = std::max(result.ur_y, point.y);
  }
  return result;
}

[[nodiscard]] Point offsetPoint(Point point, int64_t dx, int64_t dy)
{
  return checkedPoint(static_cast<int64_t>(point.x) + dx, static_cast<int64_t>(point.y) + dy);
}

[[nodiscard]] std::optional<GeometryPayloadRouteAttributes> copiedAttributes(const DesignRouteShapeAttributes* attributes)
{
  if (attributes == nullptr) {
    return std::nullopt;
  }
  return GeometryPayloadRouteAttributes{.width = attributes->width,
                                        .mask = attributes->mask,
                                        .taper_rule = attributes->taper_rule,
                                        .shape = attributes->shape,
                                        .style = attributes->style};
}

[[nodiscard]] int32_t resolvedSegmentWidth(const DesignStore& design, const DesignRouteShape& shape,
                                            const DesignRouteShapeAttributes* attributes)
{
  if ((shape.flags & DesignWirePathFlag::kHasWidth) != 0u) {
    if (attributes == nullptr) {
      throw std::logic_error("route segment declares a width without route attributes");
    }
    return attributes->width;
  }

  const auto& routing = design.routingStorage();
  const auto& net = design.netlistStorage().net(routing.wire(shape.wire).net);
  if (net.design_non_default_rule) {
    const auto& rule = routing.nonDefaultRule(net.design_non_default_rule);
    for (const auto& layer_rule : rule.layer_rules) {
      if (layer_rule.layer.entity() == shape.layer.entity() && layer_rule.width > 0) {
        return layer_rule.width;
      }
    }
  }

  const auto& technology = design.techRegistry().registry();
  if (net.non_default_rule && technology.valid(net.non_default_rule.entity())
      && technology.all_of<TechNdrRoutingRules>(net.non_default_rule.entity())) {
    const auto& rules = technology.get<const TechNdrRoutingRules>(net.non_default_rule.entity()).values;
    for (const auto& layer_rule : rules) {
      if (layer_rule.layer == shape.layer && layer_rule.width > 0) {
        return layer_rule.width;
      }
    }
  }

  if (!shape.layer || !technology.valid(shape.layer.entity()) || !technology.all_of<TechRoutingLayer>(shape.layer.entity())) {
    throw std::logic_error("route segment has no valid routing layer for width resolution");
  }
  return technology.get<const TechRoutingLayer>(shape.layer.entity()).width;
}

struct ResolvedSegmentCaps
{
  double begin_cap = 0.0;
  double end_cap = 0.0;
};

[[nodiscard]] ResolvedSegmentCaps resolvedSegmentCaps(const DesignRouteShape& shape, const DesignRouteSegment& segment,
                                                       int32_t width, const DesignRouteShapeAttributes* attributes)
{
  const bool truncated = (shape.flags & DesignWirePathFlag::kHasShape) != 0u
                         && attributes != nullptr && !attributes->shape.empty();
  if (truncated) {
    return {};
  }

  const auto capFor = [](const DesignWirePoint& point) -> std::optional<double> {
    if ((point.flags & DesignWirePointFlag::kHasExtension) == 0u) {
      return std::nullopt;
    }
    return static_cast<double>(point.extension);
  };
  const auto begin_extension = capFor(segment.begin);
  const auto end_extension = capFor(segment.end);
  const auto lower_half = static_cast<int64_t>(width) / 2;
  const auto upper_half = static_cast<int64_t>(width) - lower_half;
  const auto half_width = static_cast<double>(width) / 2.0;

  if (segment.begin.position == segment.end.position) {
    // A coincident endpoint is a width-by-width square, not a point.  The
    // consumer recognizes coincident endpoints and uses width for that square.
    return {.begin_cap = half_width, .end_cap = half_width};
  }

  if (segment.begin.position.y == segment.end.position.y) {
    const bool forward = segment.begin.position.x < segment.end.position.x;
    return {.begin_cap = begin_extension.value_or(static_cast<double>(forward ? lower_half : upper_half)),
            .end_cap = end_extension.value_or(static_cast<double>(forward ? upper_half : lower_half))};
  }
  if (segment.begin.position.x == segment.end.position.x) {
    const bool forward = segment.begin.position.y < segment.end.position.y;
    return {.begin_cap = begin_extension.value_or(static_cast<double>(forward ? lower_half : upper_half)),
            .end_cap = end_extension.value_or(static_cast<double>(forward ? upper_half : lower_half))};
  }

  return {.begin_cap = begin_extension.value_or(half_width), .end_cap = end_extension.value_or(half_width)};
}

struct PrimitiveAttributes
{
  uint32_t flags = 0;
  uint32_t mask = 0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
};

[[nodiscard]] PrimitiveAttributes attributesForGenericShape(const DesignStore& design, const GeometryShape& metadata)
{
  const auto owner = static_cast<DesignEntity>(metadata.owner.value);
  switch (metadata.owner.domain) {
    case GeometryOwnerDomain::kDesignFill: {
      const auto& fill = design.fillStorage().fill(DesignFillId{owner});
      return {.flags = fill.flags, .mask = fill.mask};
    }
    case GeometryOwnerDomain::kDesignBlockage: {
      const auto& blockage = design.constraintStorage().blockage(DesignBlockageId{owner});
      return {.flags = blockage.flags,
              .mask = blockage.mask,
              .spacing = blockage.spacing,
              .design_rule_width = blockage.design_rule_width};
    }
    default: return {.flags = metadata.flags};
  }
}

[[nodiscard]] GeometryShapeRef headerFor(const DesignStore& design, GeometryShapeId id)
{
  const auto& geometry = design.geometryStorage();
  const auto& metadata = geometry.registry().shape(id);
  const auto layer = geometry.effectiveLayer(id);
  GeometryShapeRef result{.id = id.packed(),
                          .kind = static_cast<uint8_t>(metadata.kind),
                          .owner_domain = static_cast<uint8_t>(metadata.owner.domain),
                          .owner = metadata.owner.value,
                          .layer = packedOrNone(layer),
                          // Payload headers are a read-only transfer view. Do
                          // not materialize a persistent Bounds component for
                          // every shape returned by a large viewport query.
                          .bounds = geometry.boundsUncached(id),
                          .flags = metadata.flags,
                          .source_path = metadata.source_path};
  const auto owner = static_cast<DesignEntity>(metadata.owner.value);
  switch (metadata.owner.domain) {
    case GeometryOwnerDomain::kDesignWire:
      result.wire = metadata.owner.value;
      result.net = design.routingStorage().wire(DesignWireId{owner}).net.packed();
      break;
    case GeometryOwnerDomain::kDesignNet:
    case GeometryOwnerDomain::kDesignGuide:
    case GeometryOwnerDomain::kDesignTrackAssignment:
      result.net = metadata.owner.value;
      break;
    case GeometryOwnerDomain::kDesignBlockPin: {
      const auto& pin = design.netlistStorage().blockPin(DesignBlockPinId{owner});
      const auto& term = design.netlistStorage().blockTerm(pin.term);
      const auto net = term.net ? term.net : term.special_net;
      if (net) {
        result.net = net.packed();
      }
      break;
    }
    default: break;
  }
  return result;
}

class PayloadBuilder
{
 public:
  PayloadBuilder(const DesignStore& design, const TechStore* technology, const LibraryStore* library, GeometryPayloadBatch& batch)
      : _design(design), _batch(batch)
  {
    if (technology != nullptr && library != nullptr) {
      _physical.emplace(*technology, *library, design);
    }
  }

  void appendShape(GeometryShapeId id)
  {
    const auto& geometry = _design.geometryStorage();
    if (!geometry.contains(id)) {
      throw std::out_of_range("geometry payload references a missing shape");
    }
    const auto& registry = geometry.registry();
    const auto& metadata = registry.shape(id);
    const auto header_index = checkedIndex(_batch.headers.size(), "header index");
    _batch.headers.push_back(headerFor(_design, id));

    if (const auto* route = registry.tryGet<const DesignRouteShape>(id)) {
      appendRouteShape(id, header_index, metadata, *route, _design.routingStorage().shapeAttributes(id));
      return;
    }
    if (const auto* rectangle = registry.tryGet<const DesignGeometryRectangle>(id)) {
      requireKind(metadata, GeometryShapeKind::kRectangle, id);
      appendRectangle(header_index, packedOrNone(rectangle->layer), rectangle->rectangle, metadata.flags, rectangle->mask);
      return;
    }
    if (const auto* polygon = registry.tryGet<const DesignGeometryPolygon>(id)) {
      requireKind(metadata, GeometryShapeKind::kPolygon, id);
      appendPolygon(header_index, packedOrNone(polygon->layer), polygon->points, metadata.flags, polygon->mask);
      return;
    }
    if (const auto* via = registry.tryGet<const DesignGeometryNetVia>(id)) {
      requireKind(metadata, GeometryShapeKind::kNetVia, id);
      for (const auto origin : via->origins) {
        appendVia(header_index, origin, via->tech_via, via->design_via, via->orientation, via->flags, via->top_mask,
                  via->cut_mask, via->bottom_mask, 1u, 1u, 0, 0, kNoId, std::nullopt);
      }
      return;
    }
    if (const auto* rectangle = registry.tryGet<const GeometryRectangle>(id)) {
      requireKind(metadata, GeometryShapeKind::kRectangle, id);
      const auto attributes = attributesForGenericShape(_design, metadata);
      appendRectangle(header_index, _batch.headers.back().layer, rectangle->rectangle, attributes.flags, attributes.mask,
                      attributes.spacing, attributes.design_rule_width);
      return;
    }
    if (const auto* polygon = registry.tryGet<const GeometryPolygon>(id)) {
      requireKind(metadata, GeometryShapeKind::kPolygon, id);
      const auto attributes = attributesForGenericShape(_design, metadata);
      appendPolygon(header_index, _batch.headers.back().layer, polygon->points, attributes.flags, attributes.mask,
                    attributes.spacing, attributes.design_rule_width);
      return;
    }
    if (const auto* boundary = registry.tryGet<const GeometryBoundary>(id)) {
      requireKind(metadata, GeometryShapeKind::kBoundary, id);
      appendBoundary(header_index, *boundary, metadata.flags);
      return;
    }
    if (const auto* rectangle = registry.tryGet<const DesignPinRectangle>(id)) {
      requireKind(metadata, GeometryShapeKind::kRectangle, id);
      const auto [origin, orientation] = blockPinTransform(metadata, id);
      const auto transform = [origin, orientation](Point point) { return transformAroundOrigin(point, origin, orientation); };
      appendRectangle(header_index, packedOrNone(rectangle->layer), transformRectangle(rectangle->rectangle, transform), rectangle->flags,
                      rectangle->mask, rectangle->spacing, rectangle->design_rule_width);
      return;
    }
    if (const auto* polygon = registry.tryGet<const DesignPinPolygon>(id)) {
      requireKind(metadata, GeometryShapeKind::kPolygon, id);
      const auto [origin, orientation] = blockPinTransform(metadata, id);
      std::vector<Point> points;
      points.reserve(polygon->points.size());
      for (const auto point : polygon->points) {
        points.push_back(transformAroundOrigin(point, origin, orientation));
      }
      appendPolygon(header_index, packedOrNone(polygon->layer), points, polygon->flags, polygon->mask, polygon->spacing,
                    polygon->design_rule_width);
      return;
    }
    if (const auto* via = registry.tryGet<const DesignPinVia>(id)) {
      requireKind(metadata, GeometryShapeKind::kPinVia, id);
      const auto [origin, orientation] = blockPinTransform(metadata, id);
      appendVia(header_index, transformAroundOrigin(via->origin, origin, orientation), via->tech_via, via->design_via, orientation,
                via->flags, via->top_mask, via->cut_mask, via->bottom_mask, 1u, 1u, 0, 0, kNoId, std::nullopt);
      return;
    }
    if (const auto* rectangle = registry.tryGet<const DesignViaRectangle>(id)) {
      requireKind(metadata, GeometryShapeKind::kRectangle, id);
      appendRectangle(header_index, packedOrNone(rectangle->layer), rectangle->rectangle, metadata.flags, rectangle->mask);
      return;
    }
    if (const auto* polygon = registry.tryGet<const DesignViaPolygon>(id)) {
      requireKind(metadata, GeometryShapeKind::kPolygon, id);
      appendPolygon(header_index, packedOrNone(polygon->layer), polygon->points, metadata.flags, polygon->mask);
      return;
    }
    if (const auto* guide = registry.tryGet<const DesignRouteGuide>(id)) {
      requireKind(metadata, GeometryShapeKind::kGuide, id);
      appendRectangle(header_index, packedOrNone(guide->layer), guide->box, guide->flags, 0, 0, 0,
                      std::nullopt, packedOrNone(guide->via_layer));
      return;
    }
    if (const auto* assignment = registry.tryGet<const DesignTrackAssignmentShape>(id)) {
      requireKind(metadata, GeometryShapeKind::kTrackAssignment, id);
      appendRectangle(header_index, packedOrNone(assignment->layer), assignment->box);
      return;
    }

    throw std::runtime_error("geometry payload has no supported typed component");
  }

 private:
  static void requireKind(const GeometryShape& metadata, GeometryShapeKind expected, GeometryShapeId id)
  {
    if (metadata.kind != expected) {
      throw std::runtime_error("geometry payload kind does not match its typed component for shape " + std::to_string(id.packed()));
    }
  }

  [[nodiscard]] std::pair<Point, DesignOrientation> blockPinTransform(const GeometryShape& metadata, GeometryShapeId id) const
  {
    if (metadata.owner.domain != GeometryOwnerDomain::kDesignBlockPin) {
      throw std::runtime_error("pin geometry payload has a non-block-pin owner for shape " + std::to_string(id.packed()));
    }
    const auto& pin = _design.netlistStorage().blockPin(DesignBlockPinId{static_cast<DesignEntity>(metadata.owner.value)});
    if ((pin.flags & DesignIoPinPortFlag::kHasPlacement) == 0u) {
      return {{}, DesignOrientation::kN};
    }
    return {pin.origin, pin.orientation};
  }

  void appendRectangle(uint32_t header_index, uint64_t layer, Rect rectangle, uint32_t flags = 0, uint32_t mask = 0,
                       int32_t spacing = 0, int32_t design_rule_width = 0,
                       std::optional<GeometryPayloadRouteAttributes> route_attributes = std::nullopt,
                       uint64_t via_layer = kNoId)
  {
    _batch.rectangles.push_back(GeometryPayloadRectangle{.header_index = header_index,
                                                         .layer = layer,
                                                         .rectangle = rectangle,
                                                         .via_layer = via_layer,
                                                         .flags = flags,
                                                         .mask = mask,
                                                         .spacing = spacing,
                                                         .design_rule_width = design_rule_width,
                                                         .route_attributes = std::move(route_attributes)});
  }

  void appendPolygon(uint32_t header_index, uint64_t layer, std::span<const Point> points, uint32_t flags = 0, uint32_t mask = 0,
                     int32_t spacing = 0, int32_t design_rule_width = 0,
                     std::optional<GeometryPayloadRouteAttributes> route_attributes = std::nullopt)
  {
    const auto offset = checkedIndex(_batch.polygon_points.size(), "polygon point offset");
    const auto count = checkedIndex(points.size(), "polygon point count");
    _batch.polygon_points.insert(_batch.polygon_points.end(), points.begin(), points.end());
    _batch.polygons.push_back(GeometryPayloadPolygon{.header_index = header_index,
                                                      .layer = layer,
                                                      .point_offset = offset,
                                                      .point_count = count,
                                                      .flags = flags,
                                                      .mask = mask,
                                                      .spacing = spacing,
                                                      .design_rule_width = design_rule_width,
                                                      .route_attributes = std::move(route_attributes)});
  }

  void appendBoundary(uint32_t header_index, const GeometryBoundary& boundary, uint32_t flags)
  {
    if (boundary.points.size() == 2u) {
      const auto& first = boundary.points.front();
      const auto& second = boundary.points.back();
      appendRectangle(header_index, kNoId,
                      Rect{.ll_x = first.x, .ll_y = first.y, .ur_x = second.x, .ur_y = second.y}.normalized(), flags);
      return;
    }
    if (boundary.points.size() < 3u) {
      throw std::runtime_error("geometry boundary has too few points");
    }
    appendPolygon(header_index, kNoId, boundary.points, flags);
  }

  void appendRouteShape(GeometryShapeId id, uint32_t header_index, const GeometryShape& metadata, const DesignRouteShape& shape,
                        const DesignRouteShapeAttributes* attributes)
  {
    if (metadata.kind != routeShapeKind(shape)) {
      throw std::runtime_error("route geometry payload kind does not match its variant for shape " + std::to_string(id.packed()));
    }
    const auto copied = copiedAttributes(attributes);
    const auto mask = (shape.flags & DesignWirePathFlag::kHasMask) != 0u && attributes != nullptr ? attributes->mask : 0u;
    const auto style = (shape.flags & DesignWirePathFlag::kHasStyle) != 0u && attributes != nullptr ? attributes->style : 0;
    std::visit(
        [&](const auto& value) {
          using Geometry = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::is_same_v<Geometry, DesignRouteSegment>) {
            const auto width = resolvedSegmentWidth(_design, shape, attributes);
            const auto caps = resolvedSegmentCaps(shape, value, width, attributes);
            _batch.segments.push_back(GeometryPayloadSegment{.header_index = header_index,
                                                              .p0 = value.begin.position,
                                                              .p1 = value.end.position,
                                                              .width = width,
                                                              .begin_cap = caps.begin_cap,
                                                              .end_cap = caps.end_cap,
                                                              .begin_flags = value.begin.flags,
                                                              .end_flags = value.end.flags,
                                                              .begin_extension = value.begin.extension,
                                                              .end_extension = value.end.extension,
                                                              .mask = mask,
                                                              .style = style,
                                                              .attributes = copied});
          } else if constexpr (std::is_same_v<Geometry, DesignRoutePatch>) {
            appendRectangle(header_index, packedOrNone(shape.layer), value.rectangle, shape.flags, mask, 0, 0, copied);
          } else if constexpr (std::is_same_v<Geometry, DesignRoutePoint>) {
            _batch.points.push_back(GeometryPayloadPoint{.header_index = header_index,
                                                         .layer = packedOrNone(shape.layer),
                                                         .point = value.point.position,
                                                         .flags = value.point.flags,
                                                         .extension = value.point.extension,
                                                         .mask = mask,
                                                         .style = style,
                                                         .attributes = copied});
          } else {
            appendVia(header_index, value.origin.position, value.via.tech_via, value.via.design_via, value.via.orientation, value.via.flags,
                      value.via.top_mask, value.via.cut_mask, value.via.bottom_mask,
                      (value.via.flags & DesignWireViaFlag::kHasArray) != 0u ? value.via.rows : 1u,
                      (value.via.flags & DesignWireViaFlag::kHasArray) != 0u ? value.via.columns : 1u,
                      (value.via.flags & DesignWireViaFlag::kHasArray) != 0u ? value.via.step_x : 0,
                      (value.via.flags & DesignWireViaFlag::kHasArray) != 0u ? value.via.step_y : 0,
                      packedOrNone(value.exit_layer), copied);
          }
        },
        shape.geometry);
  }

  void appendVia(uint32_t header_index, Point origin, TechViaMasterId tech_via, DesignViaId design_via, DesignOrientation orientation,
                 uint32_t flags, uint32_t top_mask, uint32_t cut_mask, uint32_t bottom_mask, uint32_t rows, uint32_t columns,
                 int32_t step_x, int32_t step_y, uint64_t exit_layer,
                 std::optional<GeometryPayloadRouteAttributes> route_attributes)
  {
    if (static_cast<bool>(tech_via) == static_cast<bool>(design_via)) {
      throw std::runtime_error("geometry VIA placement must reference exactly one definition");
    }
    if (rows == 0u || columns == 0u) {
      throw std::runtime_error("geometry VIA placement has zero array dimensions");
    }
    _batch.vias.push_back(GeometryPayloadVia{.header_index = header_index,
                                             .origin = origin,
                                             .tech_via = packedOrNone(tech_via),
                                             .design_via = packedOrNone(design_via),
                                             .orientation = static_cast<uint8_t>(orientation),
                                             .flags = flags,
                                             .top_mask = top_mask,
                                             .cut_mask = cut_mask,
                                             .bottom_mask = bottom_mask,
                                             .rows = rows,
                                             .columns = columns,
                                             .step_x = step_x,
                                             .step_y = step_y,
                                             .exit_layer = exit_layer,
                                             .route_attributes = std::move(route_attributes)});
    for (uint32_t row = 0; row < rows; ++row) {
      for (uint32_t column = 0; column < columns; ++column) {
        const auto placement = offsetPoint(origin, static_cast<int64_t>(column) * step_x, static_cast<int64_t>(row) * step_y);
        appendExpandedVia(header_index, tech_via, design_via, placement, orientation);
      }
    }
  }

  void appendExpandedVia(uint32_t header_index, TechViaMasterId tech_via, DesignViaId design_via, Point origin,
                         DesignOrientation orientation)
  {
    const auto visitor = PhysicalGeometryVisitor{
        .rectangle = [this, header_index](const PhysicalLayerRect& rectangle) {
          appendRectangle(header_index, packedOrNone(rectangle.layer), rectangle.rectangle, rectangle.flags, rectangle.mask,
                          rectangle.spacing, rectangle.design_rule_width);
        },
        .polygon = [this, header_index](const PhysicalLayerPolygon& polygon) {
          appendPolygon(header_index, packedOrNone(polygon.layer), polygon.points, polygon.flags, polygon.mask, polygon.spacing,
                        polygon.design_rule_width);
        }};
    if (tech_via) {
      if (!_physical.has_value()) {
        throw std::logic_error("cannot expand a technology VIA from a borrowed database without technology storage");
      }
      _physical->forEachTechViaGeometry(tech_via, origin, orientation, visitor);
      return;
    }
    if (_physical.has_value()) {
      _physical->forEachDesignViaGeometry(design_via, origin, orientation, visitor);
      return;
    }
    appendBorrowedDesignVia(header_index, design_via, origin, orientation);
  }

  void appendBorrowedDesignVia(uint32_t header_index, DesignViaId via_id, Point origin, DesignOrientation orientation)
  {
    const auto& via = _design.routingStorage().via(via_id);
    if ((via.flags & DesignViaFlag::kGenerated) != 0u) {
      appendBorrowedGeneratedVia(header_index, via.generated, origin, orientation);
      return;
    }

    const auto& registry = _design.geometryRegistry();
    const auto transform = [origin, orientation](Point point) { return transformAroundOrigin(point, origin, orientation); };
    for (const auto id : via.rectangles) {
      const auto* rectangle = registry.tryGet<const DesignViaRectangle>(id);
      if (rectangle == nullptr) {
        throw std::runtime_error("design VIA references a missing rectangle payload");
      }
      appendRectangle(header_index, packedOrNone(rectangle->layer), transformRectangle(rectangle->rectangle, transform), 0, rectangle->mask);
    }
    for (const auto id : via.polygons) {
      const auto* polygon = registry.tryGet<const DesignViaPolygon>(id);
      if (polygon == nullptr) {
        throw std::runtime_error("design VIA references a missing polygon payload");
      }
      std::vector<Point> points;
      points.reserve(polygon->points.size());
      for (const auto point : polygon->points) {
        points.push_back(transform(point));
      }
      appendPolygon(header_index, packedOrNone(polygon->layer), points, 0, polygon->mask);
    }
  }

  void appendBorrowedGeneratedVia(uint32_t header_index, const DesignGeneratedVia& via, Point origin, DesignOrientation orientation)
  {
    if (via.row_count == 0u || via.column_count == 0u) {
      throw std::runtime_error("generated design VIA has zero array dimensions");
    }
    const uint64_t width = static_cast<uint64_t>(via.column_count) * static_cast<uint64_t>(via.cut_size_x)
                           + static_cast<uint64_t>(via.column_count - 1u) * static_cast<uint64_t>(via.cut_spacing_x);
    const uint64_t height = static_cast<uint64_t>(via.row_count) * static_cast<uint64_t>(via.cut_size_y)
                            + static_cast<uint64_t>(via.row_count - 1u) * static_cast<uint64_t>(via.cut_spacing_y);
    if (width > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())
        || height > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
      throw std::overflow_error("generated design VIA exceeds int32 coordinate range");
    }
    const int64_t min_x = -static_cast<int64_t>(width / 2u);
    const int64_t min_y = -static_cast<int64_t>(height / 2u);
    const int64_t max_x = min_x + static_cast<int64_t>(width);
    const int64_t max_y = min_y + static_cast<int64_t>(height);
    const int64_t via_origin_x = (via.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u ? via.origin.x : 0;
    const int64_t via_origin_y = (via.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u ? via.origin.y : 0;
    const auto transform = [origin, orientation](Point point) { return transformAroundOrigin(point, origin, orientation); };
    const auto emit = [this, header_index, &transform](TechLayerId layer, int64_t ll_x, int64_t ll_y, int64_t ur_x, int64_t ur_y) {
      appendRectangle(header_index, packedOrNone(layer),
                      transformRectangle(Rect{.ll_x = checkedCoordinate(ll_x), .ll_y = checkedCoordinate(ll_y),
                                              .ur_x = checkedCoordinate(ur_x), .ur_y = checkedCoordinate(ur_y)},
                                         transform));
    };
    const int64_t bottom_offset_x = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.bottom_offset.x : 0;
    const int64_t bottom_offset_y = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.bottom_offset.y : 0;
    emit(TechLayerId{via.bottom_layer.entity()}, min_x - via.bottom_enclosure_x + via_origin_x + bottom_offset_x,
         min_y - via.bottom_enclosure_y + via_origin_y + bottom_offset_y,
         max_x + via.bottom_enclosure_x + via_origin_x + bottom_offset_x,
         max_y + via.bottom_enclosure_y + via_origin_y + bottom_offset_y);

    const int64_t step_x = static_cast<int64_t>(via.cut_size_x) + via.cut_spacing_x;
    const int64_t step_y = static_cast<int64_t>(via.cut_size_y) + via.cut_spacing_y;
    for (uint32_t row = 0; row < via.row_count; ++row) {
      for (uint32_t column = 0; column < via.column_count; ++column) {
        const int64_t x = min_x + via_origin_x + static_cast<int64_t>(column) * step_x;
        const int64_t y = min_y + via_origin_y + static_cast<int64_t>(row) * step_y;
        emit(TechLayerId{via.cut_layer.entity()}, x, y, x + via.cut_size_x, y + via.cut_size_y);
      }
    }

    const int64_t top_offset_x = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.top_offset.x : 0;
    const int64_t top_offset_y = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.top_offset.y : 0;
    emit(TechLayerId{via.top_layer.entity()}, min_x - via.top_enclosure_x + via_origin_x + top_offset_x,
         min_y - via.top_enclosure_y + via_origin_y + top_offset_y,
         max_x + via.top_enclosure_x + via_origin_x + top_offset_x,
         max_y + via.top_enclosure_y + via_origin_y + top_offset_y);
  }

  const DesignStore& _design;
  GeometryPayloadBatch& _batch;
  std::optional<PhysicalDesignService> _physical;
};

[[nodiscard]] bool touchesLayer(const GeometryPayloadBatch& batch, std::size_t header_index,
                               const GeometryQueryFilter& filter, std::size_t rectangle_count, std::size_t polygon_count)
{
  if (filter.acceptsLayer(batch.headers.at(header_index).layer)) {
    return true;
  }
  const auto matches = [&filter](const auto& primitive) {
    return filter.acceptsLayer(primitive.layer);
  };
  return std::any_of(batch.rectangles.begin() + static_cast<std::ptrdiff_t>(rectangle_count), batch.rectangles.end(), matches)
         || std::any_of(batch.polygons.begin() + static_cast<std::ptrdiff_t>(polygon_count), batch.polygons.end(), matches);
}

void restrictNewPrimitivesToLayer(GeometryPayloadBatch& batch, std::size_t header_index, const GeometryQueryFilter& filter,
                                  std::size_t segment_count, std::size_t rectangle_count, std::size_t polygon_count,
                                  std::size_t polygon_point_count, std::size_t point_count)
{
  if (!filter.acceptsLayer(batch.headers.at(header_index).layer)) {
    batch.segments.resize(segment_count);
    batch.points.resize(point_count);
  }

  auto rectangle_begin = batch.rectangles.begin() + static_cast<std::ptrdiff_t>(rectangle_count);
  batch.rectangles.erase(std::remove_if(rectangle_begin, batch.rectangles.end(), [&filter](const auto& rectangle) {
                          return !filter.acceptsLayer(rectangle.layer);
                        }),
                        batch.rectangles.end());

  std::vector<GeometryPayloadPolygon> retained_polygons;
  std::vector<Point> retained_points;
  retained_polygons.reserve(batch.polygons.size() - polygon_count);
  for (auto index = polygon_count; index < batch.polygons.size(); ++index) {
    const auto& polygon = batch.polygons[index];
    if (!filter.acceptsLayer(polygon.layer)) {
      continue;
    }
    const auto begin = static_cast<std::size_t>(polygon.point_offset);
    const auto count = static_cast<std::size_t>(polygon.point_count);
    if (begin > batch.polygon_points.size() || count > batch.polygon_points.size() - begin) {
      throw std::runtime_error("geometry payload polygon points are out of range");
    }
    auto retained = polygon;
    retained.point_offset = checkedIndex(polygon_point_count + retained_points.size(), "polygon point offset");
    retained_points.insert(retained_points.end(), batch.polygon_points.begin() + static_cast<std::ptrdiff_t>(begin),
                           batch.polygon_points.begin() + static_cast<std::ptrdiff_t>(begin + count));
    retained_polygons.push_back(std::move(retained));
  }
  batch.polygons.resize(polygon_count);
  batch.polygon_points.resize(polygon_point_count);
  batch.polygons.insert(batch.polygons.end(), std::make_move_iterator(retained_polygons.begin()),
                        std::make_move_iterator(retained_polygons.end()));
  batch.polygon_points.insert(batch.polygon_points.end(), std::make_move_iterator(retained_points.begin()),
                              std::make_move_iterator(retained_points.end()));
}

}  // namespace

GeometryPayloadBatch GeometryPayloadService::fetchShapes(std::span<const uint64_t> ids) const
{
  if (_database == nullptr) {
    throw std::logic_error("GeometryPayloadService has no database");
  }
  const auto& state = _database->state();
  const auto& design = state.design();
  GeometryPayloadBatch result;
  result.geometry_epoch = design.geometryStorage().geometryEpoch();
  result.headers.reserve(ids.size());
  PayloadBuilder builder{design, state.technology(), state.library(), result};
  for (const auto value : ids) {
    builder.appendShape(toShapeId(value));
  }
  return result;
}

GeometryPayloadBatch GeometryPayloadService::queryGeometry(Rect box, const GeometryQueryFilter& filter) const
{
  if (_database == nullptr) {
    throw std::logic_error("GeometryPayloadService has no database");
  }
  const auto& state = _database->state();
  const auto& design = state.design();
  GeometryPayloadBatch result;
  result.geometry_epoch = design.geometryStorage().geometryEpoch();

  const auto ids = design.geometryIndex().query(box.normalized(), filter);
  result.headers.reserve(ids.size());
  PayloadBuilder builder{design, state.technology(), state.library(), result};
  for (const auto id : ids) {
    const auto header_count = result.headers.size();
    const auto segment_count = result.segments.size();
    const auto rectangle_count = result.rectangles.size();
    const auto polygon_count = result.polygons.size();
    const auto polygon_point_count = result.polygon_points.size();
    const auto point_count = result.points.size();
    const auto via_count = result.vias.size();
    builder.appendShape(id);
    const bool layer_filtered = filter.layer != GeometryQueryFilter::kUnspecified || !filter.layers.empty();
    if (layer_filtered && !touchesLayer(result, header_count, filter, rectangle_count, polygon_count)) {
      result.headers.resize(header_count);
      result.segments.resize(segment_count);
      result.rectangles.resize(rectangle_count);
      result.polygons.resize(polygon_count);
      result.polygon_points.resize(polygon_point_count);
      result.points.resize(point_count);
      result.vias.resize(via_count);
    } else if (layer_filtered) {
      restrictNewPrimitivesToLayer(result, header_count, filter, segment_count, rectangle_count, polygon_count,
                                   polygon_point_count, point_count);
    }
  }
  return result;
}

}  // namespace eccdb
