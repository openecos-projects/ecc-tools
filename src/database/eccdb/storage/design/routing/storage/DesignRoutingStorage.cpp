#include "design/routing/storage/DesignRoutingStorage.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "design/common/DesignGeometryValidation.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/pin_access/model/PinAccessComponents.h"
#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {
namespace {

constexpr uint32_t kShapeValueFlags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask
                                      | DesignWirePathFlag::kHasTaperRule | DesignWirePathFlag::kHasShape
                                      | DesignWirePathFlag::kHasStyle;
constexpr uint32_t kKnownPathFlags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask | DesignWirePathFlag::kTaper
                                     | DesignWirePathFlag::kHasTaperRule | DesignWirePathFlag::kHasShape
                                     | DesignWirePathFlag::kHasStyle;
constexpr uint32_t kKnownPointFlags = DesignWirePointFlag::kHasExtension | DesignWirePointFlag::kVirtual;
constexpr uint32_t kKnownViaFlags = DesignWireViaFlag::kHasMask | DesignWireViaFlag::kHasArray;
constexpr uint32_t kUnassignedSourcePath = std::numeric_limits<uint32_t>::max();

bool isValidWireStatus(DesignWireStatus status)
{
  switch (status) {
    case DesignWireStatus::kRouted:
    case DesignWireStatus::kFixed:
    case DesignWireStatus::kCover:
    case DesignWireStatus::kShield:
    case DesignWireStatus::kNoShield:
      return true;
  }
  return false;
}

bool isValidNetGeometryStatus(DesignWireStatus status)
{
  return status == DesignWireStatus::kRouted || status == DesignWireStatus::kFixed || status == DesignWireStatus::kCover
         || status == DesignWireStatus::kShield;
}

void validateNetGeometryOptions(DesignWireStatus status, uint32_t flags, uint32_t mask, const std::string& shield_net,
                                const std::string& shape)
{
  constexpr uint32_t kKnownFlags = DesignNetGeometryFlag::kHasMask | DesignNetGeometryFlag::kHasShape;
  const bool has_mask = (flags & DesignNetGeometryFlag::kHasMask) != 0u;
  const bool has_shape = (flags & DesignNetGeometryFlag::kHasShape) != 0u;
  if (!isValidNetGeometryStatus(status) || (flags & ~kKnownFlags) != 0u || has_mask != (mask != 0u) || has_shape == shape.empty()
      || (status == DesignWireStatus::kShield) == shield_net.empty()) {
    throw std::invalid_argument("standalone net geometry options are inconsistent");
  }
}

bool samePoint(const DesignWirePoint& lhs, const DesignWirePoint& rhs) noexcept
{
  return lhs.position == rhs.position && lhs.flags == rhs.flags && lhs.extension == rhs.extension;
}

bool sameAttributes(const DesignWirePathExtra& lhs, const DesignWirePathExtra& rhs) noexcept
{
  return lhs.width == rhs.width && lhs.mask == rhs.mask && lhs.taper_rule == rhs.taper_rule && lhs.shape == rhs.shape
         && lhs.style == rhs.style;
}

bool hasShapeValueAttributes(uint32_t flags) noexcept
{
  return (flags & kShapeValueFlags) != 0u;
}

void validateWireMetadata(const DesignRoutingStorage::registry_type& registry, const DesignWire& wire)
{
  if (!wire.net || !registry.valid(wire.net.entity()) || !registry.all_of<DesignNet>(wire.net.entity())) {
    throw std::invalid_argument("design wire references an invalid net");
  }
  if (!isValidWireStatus(wire.status) || (wire.status == DesignWireStatus::kShield) == wire.shield_net.empty()) {
    throw std::invalid_argument("design wire status or shield name is invalid");
  }
}

void validatePointValue(DesignWirePoint point)
{
  const bool has_extension = (point.flags & DesignWirePointFlag::kHasExtension) != 0u;
  const bool is_virtual = (point.flags & DesignWirePointFlag::kVirtual) != 0u;
  if ((point.flags & ~kKnownPointFlags) != 0u || (!has_extension && point.extension != 0) || (has_extension && is_virtual)) {
    throw std::invalid_argument("design wire point extension and flags are inconsistent");
  }
}

void validatePathValue(const DesignRoutingStorage::registry_type& design, const TechRegistry::registry_type& tech,
                       DesignWirePathView path)
{
  if (!path.layer() || !tech.valid(path.layer().entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(path.layer().entity())) {
    throw std::invalid_argument("design wire path references an invalid routing layer");
  }
  if ((path.flags() & ~kKnownPathFlags) != 0u || path.points().empty()) {
    throw std::invalid_argument("design wire path flags or points are invalid");
  }
  const bool has_width = (path.flags() & DesignWirePathFlag::kHasWidth) != 0u;
  const bool zero_width_via_path
      = has_width && path.width() == 0 && path.points().size() == 1u && !path.vias().empty() && path.rectangles().empty();
  if ((has_width && path.width() < 0) || (has_width && path.width() == 0 && !zero_width_via_path)
      || (!has_width && path.width() != 0)) {
    throw std::invalid_argument("design wire path width and flags are inconsistent");
  }
  const bool has_mask = (path.flags() & DesignWirePathFlag::kHasMask) != 0u;
  const bool has_taper_rule = (path.flags() & DesignWirePathFlag::kHasTaperRule) != 0u;
  const bool has_shape = (path.flags() & DesignWirePathFlag::kHasShape) != 0u;
  const bool has_style = (path.flags() & DesignWirePathFlag::kHasStyle) != 0u;
  if (has_mask != (path.mask() != 0u) || has_taper_rule == path.taperRule().empty() || has_shape == path.shape().empty()
      || (!has_style && path.style() != 0)) {
    throw std::invalid_argument("design wire path optional fields and flags are inconsistent");
  }
  for (const auto point : path.points()) {
    validatePointValue(point);
  }
  for (const auto via : path.vias()) {
    if (via.point_index >= path.points().size() || !isValidDesignOrientation(via.orientation)) {
      throw std::invalid_argument("design wire via anchor or orientation is invalid");
    }
    const bool has_tech_via = static_cast<bool>(via.tech_via);
    const bool has_design_via = static_cast<bool>(via.design_via);
    if (has_tech_via == has_design_via) {
      throw std::invalid_argument("design wire via must reference exactly one Tech or Design via");
    }
    if (has_tech_via && (!tech.valid(via.tech_via.entity()) || !tech.all_of<TechViaMaster>(via.tech_via.entity()))) {
      throw std::invalid_argument("design wire references an invalid technology via");
    }
    if (has_design_via && (!design.valid(via.design_via.entity()) || !design.all_of<DesignVia>(via.design_via.entity()))) {
      throw std::invalid_argument("design wire references an invalid design via");
    }
    const bool has_via_mask = (via.flags & DesignWireViaFlag::kHasMask) != 0u;
    const bool has_array = (via.flags & DesignWireViaFlag::kHasArray) != 0u;
    const bool any_via_mask = via.top_mask != 0u || via.cut_mask != 0u || via.bottom_mask != 0u;
    if ((via.flags & ~kKnownViaFlags) != 0u || has_via_mask != any_via_mask || (has_array && (via.rows == 0u || via.columns == 0u))
        || (!has_array && (via.rows != 1u || via.columns != 1u || via.step_x != 0 || via.step_y != 0))) {
      throw std::invalid_argument("design wire via optional fields and flags are inconsistent");
    }
  }
  for (const auto rectangle : path.rectangles()) {
    if (rectangle.point_index >= path.points().size() || !rectangle.delta.isValid()) {
      throw std::invalid_argument("design wire rectangle anchor or delta is invalid");
    }
  }
}

Rect checkedRect(int64_t ll_x, int64_t ll_y, int64_t ur_x, int64_t ur_y)
{
  const auto checked = [](int64_t value) -> int32_t {
    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
      throw std::overflow_error("wire shape coordinate exceeds int32 range");
    }
    return static_cast<int32_t>(value);
  };
  return Rect{.ll_x = checked(ll_x), .ll_y = checked(ll_y), .ur_x = checked(ur_x), .ur_y = checked(ur_y)};
}

Point checkedPoint(int64_t x, int64_t y)
{
  const auto point = checkedRect(x, y, x, y);
  return {.x = point.ll_x, .y = point.ll_y};
}

DesignWireRectangle relativePatch(Rect rectangle, DesignWirePoint anchor)
{
  return {.point_index = 0u,
          .delta = checkedRect(static_cast<int64_t>(rectangle.ll_x) - anchor.position.x,
                               static_cast<int64_t>(rectangle.ll_y) - anchor.position.y,
                               static_cast<int64_t>(rectangle.ur_x) - anchor.position.x,
                               static_cast<int64_t>(rectangle.ur_y) - anchor.position.y)};
}

Point transformPoint(Point point, Point origin, DesignOrientation orientation)
{
  const int64_t x = point.x;
  const int64_t y = point.y;
  int64_t transformed_x = 0;
  int64_t transformed_y = 0;
  switch (orientation) {
    case DesignOrientation::kN: transformed_x = x; transformed_y = y; break;
    case DesignOrientation::kS: transformed_x = -x; transformed_y = -y; break;
    case DesignOrientation::kE: transformed_x = y; transformed_y = -x; break;
    case DesignOrientation::kW: transformed_x = -y; transformed_y = x; break;
    case DesignOrientation::kFN: transformed_x = -x; transformed_y = y; break;
    case DesignOrientation::kFS: transformed_x = x; transformed_y = -y; break;
    case DesignOrientation::kFE: transformed_x = -y; transformed_y = -x; break;
    case DesignOrientation::kFW: transformed_x = y; transformed_y = x; break;
    default: throw std::invalid_argument("wire VIA orientation is invalid");
  }
  return checkedPoint(transformed_x + origin.x, transformed_y + origin.y);
}

Rect orientedBounds(Rect rectangle, Point origin, DesignOrientation orientation)
{
  const std::array<Point, 4> corners{transformPoint({rectangle.ll_x, rectangle.ll_y}, origin, orientation),
                                     transformPoint({rectangle.ll_x, rectangle.ur_y}, origin, orientation),
                                     transformPoint({rectangle.ur_x, rectangle.ll_y}, origin, orientation),
                                     transformPoint({rectangle.ur_x, rectangle.ur_y}, origin, orientation)};
  int64_t min_x = corners.front().x;
  int64_t min_y = corners.front().y;
  int64_t max_x = corners.front().x;
  int64_t max_y = corners.front().y;
  for (const auto corner : corners) {
    min_x = std::min<int64_t>(min_x, corner.x);
    min_y = std::min<int64_t>(min_y, corner.y);
    max_x = std::max<int64_t>(max_x, corner.x);
    max_y = std::max<int64_t>(max_y, corner.y);
  }
  return checkedRect(min_x, min_y, max_x, max_y);
}

Rect offsetRect(Rect rectangle, int64_t dx, int64_t dy)
{
  return checkedRect(static_cast<int64_t>(rectangle.ll_x) + dx, static_cast<int64_t>(rectangle.ll_y) + dy,
                     static_cast<int64_t>(rectangle.ur_x) + dx, static_cast<int64_t>(rectangle.ur_y) + dy);
}

struct ViaRoutingLayers
{
  TechRoutingLayerId first;
  TechRoutingLayerId second;
};

bool isRoutingLayer(const TechRegistry::registry_type& tech, TechRoutingLayerId layer)
{
  return layer && tech.valid(layer.entity()) && tech.all_of<TechLayerInfo, TechRoutingLayer>(layer.entity());
}

void appendRoutingLayer(const TechRegistry::registry_type& tech, TechLayerId layer,
                        std::array<TechRoutingLayerId, 2>& layers, std::size_t& count)
{
  const TechRoutingLayerId routing_layer{layer.entity()};
  if (!isRoutingLayer(tech, routing_layer)) {
    return;
  }
  for (std::size_t index = 0; index < count; ++index) {
    if (layers[index] == routing_layer) {
      return;
    }
  }
  if (count < layers.size()) {
    layers[count++] = routing_layer;
  } else {
    count = layers.size() + 1u;
  }
}

std::optional<ViaRoutingLayers> routingLayersForVia(const DesignRoutingStorage::registry_type& design,
                                                     const TechRegistry::registry_type& tech, const DesignWireVia& via)
{
  if (via.tech_via) {
    if (!tech.valid(via.tech_via.entity()) || !tech.all_of<TechViaGeometry>(via.tech_via.entity())) {
      return std::nullopt;
    }
    const auto& geometry = tech.get<const TechViaGeometry>(via.tech_via.entity());
    if (geometry.bottom_layer.kind != TechConductorLayerKind::kRouting
        || geometry.top_layer.kind != TechConductorLayerKind::kRouting) {
      return std::nullopt;
    }
    return ViaRoutingLayers{.first = TechRoutingLayerId{geometry.bottom_layer.entity},
                            .second = TechRoutingLayerId{geometry.top_layer.entity}};
  }
  if (!via.design_via || !design.valid(via.design_via.entity()) || !design.all_of<DesignVia>(via.design_via.entity())) {
    return std::nullopt;
  }
  const auto& definition = design.get<const DesignVia>(via.design_via.entity());
  if ((definition.flags & DesignViaFlag::kGenerated) != 0u) {
    return ViaRoutingLayers{.first = definition.generated.bottom_layer, .second = definition.generated.top_layer};
  }
  std::array<TechRoutingLayerId, 2> layers{};
  std::size_t count = 0u;
  for (const auto& rectangle : definition.rectangles) {
    appendRoutingLayer(tech, rectangle.layer, layers, count);
  }
  for (const auto& polygon : definition.polygons) {
    appendRoutingLayer(tech, polygon.layer, layers, count);
  }
  if (count != 2u) {
    return std::nullopt;
  }
  return ViaRoutingLayers{.first = layers[0], .second = layers[1]};
}

void normalizeViaExitLayer(const DesignRoutingStorage::registry_type& design, const TechRegistry::registry_type& tech,
                           TechRoutingLayerId entry_layer, DesignRouteVia& via)
{
  const auto layers = routingLayersForVia(design, tech, via.via);
  const auto expected = !layers.has_value() ? entry_layer
                                             : entry_layer == layers->first ? layers->second
                                                                            : entry_layer == layers->second ? layers->first : entry_layer;
  if (!via.exit_layer) {
    via.exit_layer = expected;
  } else if (!isRoutingLayer(tech, via.exit_layer)) {
    throw std::invalid_argument("route VIA exit layer is invalid");
  } else if (layers.has_value() && via.exit_layer != expected) {
    throw std::invalid_argument("route VIA exit layer disagrees with its definition");
  }
}

Rect generatedDesignViaBounds(const DesignGeneratedVia& via)
{
  if (via.row_count == 0u || via.column_count == 0u || via.cut_size_x <= 0 || via.cut_size_y <= 0
      || via.cut_spacing_x < 0 || via.cut_spacing_y < 0 || via.bottom_enclosure_x < 0 || via.bottom_enclosure_y < 0
      || via.top_enclosure_x < 0 || via.top_enclosure_y < 0) {
    throw std::invalid_argument("generated design via geometry is invalid");
  }
  const uint64_t array_width = static_cast<uint64_t>(via.column_count) * static_cast<uint64_t>(via.cut_size_x)
                               + static_cast<uint64_t>(via.column_count - 1u) * static_cast<uint64_t>(via.cut_spacing_x);
  const uint64_t array_height = static_cast<uint64_t>(via.row_count) * static_cast<uint64_t>(via.cut_size_y)
                                + static_cast<uint64_t>(via.row_count - 1u) * static_cast<uint64_t>(via.cut_spacing_y);
  const int64_t min_x = -static_cast<int64_t>(array_width / 2u);
  const int64_t min_y = -static_cast<int64_t>(array_height / 2u);
  const int64_t max_x = min_x + static_cast<int64_t>(array_width);
  const int64_t max_y = min_y + static_cast<int64_t>(array_height);
  const int64_t origin_x = (via.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u ? via.origin.x : 0;
  const int64_t origin_y = (via.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u ? via.origin.y : 0;
  const int64_t bottom_x = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.bottom_offset.x : 0;
  const int64_t bottom_y = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.bottom_offset.y : 0;
  const int64_t top_x = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.top_offset.x : 0;
  const int64_t top_y = (via.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? via.top_offset.y : 0;
  const Rect bottom = checkedRect(min_x - via.bottom_enclosure_x + origin_x + bottom_x,
                                  min_y - via.bottom_enclosure_y + origin_y + bottom_y,
                                  max_x + via.bottom_enclosure_x + origin_x + bottom_x,
                                  max_y + via.bottom_enclosure_y + origin_y + bottom_y);
  const Rect cut = checkedRect(min_x + origin_x, min_y + origin_y, max_x + origin_x, max_y + origin_y);
  const Rect top = checkedRect(min_x - via.top_enclosure_x + origin_x + top_x,
                               min_y - via.top_enclosure_y + origin_y + top_y,
                               max_x + via.top_enclosure_x + origin_x + top_x,
                               max_y + via.top_enclosure_y + origin_y + top_y);
  return bottom.united(cut).united(top);
}

Rect fixedDesignViaBounds(const DesignVia& via)
{
  std::optional<Rect> result;
  const auto append = [&result](Rect rectangle) {
    result = result.has_value() ? result->united(rectangle) : rectangle;
  };
  for (const auto& rectangle : via.rectangles) {
    append(rectangle.rectangle);
  }
  for (const auto& polygon : via.polygons) {
    if (polygon.points.empty()) {
      continue;
    }
    int64_t min_x = polygon.points.front().x;
    int64_t min_y = polygon.points.front().y;
    int64_t max_x = polygon.points.front().x;
    int64_t max_y = polygon.points.front().y;
    for (const auto point : polygon.points) {
      min_x = std::min<int64_t>(min_x, point.x);
      min_y = std::min<int64_t>(min_y, point.y);
      max_x = std::max<int64_t>(max_x, point.x);
      max_y = std::max<int64_t>(max_y, point.y);
    }
    append(checkedRect(min_x, min_y, max_x, max_y));
  }
  if (!result.has_value()) {
    throw std::invalid_argument("design via has no physical geometry");
  }
  return *result;
}

Rect viaBounds(const DesignRoutingStorage::registry_type& design, const TechRegistry::registry_type& tech,
               const DesignRouteVia& shape)
{
  Rect local;
  if (shape.via.tech_via) {
    if (!tech.valid(shape.via.tech_via.entity()) || !tech.all_of<TechViaGeometry>(shape.via.tech_via.entity())) {
      throw std::invalid_argument("route VIA references an invalid technology geometry");
    }
    local = tech.get<const TechViaGeometry>(shape.via.tech_via.entity()).bounding_box;
  } else {
    if (!shape.via.design_via || !design.valid(shape.via.design_via.entity()) || !design.all_of<DesignVia>(shape.via.design_via.entity())) {
      throw std::invalid_argument("route VIA references an invalid design geometry");
    }
    const auto& definition = design.get<const DesignVia>(shape.via.design_via.entity());
    local = (definition.flags & DesignViaFlag::kGenerated) != 0u ? generatedDesignViaBounds(definition.generated)
                                                                   : fixedDesignViaBounds(definition);
  }
  auto result = orientedBounds(local, shape.origin.position, shape.via.orientation);
  if ((shape.via.flags & DesignWireViaFlag::kHasArray) == 0u) {
    return result;
  }
  if (shape.via.rows == 0u || shape.via.columns == 0u) {
    throw std::invalid_argument("route VIA array dimensions are invalid");
  }
  const int64_t last_x = static_cast<int64_t>(shape.via.step_x) * static_cast<int64_t>(shape.via.columns - 1u);
  const int64_t last_y = static_cast<int64_t>(shape.via.step_y) * static_cast<int64_t>(shape.via.rows - 1u);
  return result.united(offsetRect(result, std::min<int64_t>(0, last_x), std::min<int64_t>(0, last_y)))
      .united(offsetRect(result, std::max<int64_t>(0, last_x), std::max<int64_t>(0, last_y)));
}

int32_t defaultSegmentWidth(const DesignRoutingStorage::registry_type& design, const TechRegistry::registry_type& tech,
                            const DesignRouteShape& shape)
{
  const auto& owner = design.get<const DesignWire>(shape.wire.entity());
  const auto& net = design.get<const DesignNet>(owner.net.entity());
  if (net.design_non_default_rule && design.valid(net.design_non_default_rule.entity())
      && design.all_of<DesignNonDefaultRule>(net.design_non_default_rule.entity())) {
    const auto& rule = design.get<const DesignNonDefaultRule>(net.design_non_default_rule.entity());
    for (const auto& layer_rule : rule.layer_rules) {
      if (layer_rule.layer.entity() == shape.layer.entity() && layer_rule.width > 0) {
        return layer_rule.width;
      }
    }
  }
  if (net.non_default_rule && tech.valid(net.non_default_rule.entity())
      && tech.all_of<TechNdrRoutingRules>(net.non_default_rule.entity())) {
    const auto& rules = tech.get<const TechNdrRoutingRules>(net.non_default_rule.entity()).values;
    for (const auto& layer_rule : rules) {
      if (layer_rule.layer == shape.layer && layer_rule.width > 0) {
        return layer_rule.width;
      }
    }
  }
  // The current technology model has no distinct wrong-direction width.
  return tech.get<const TechRoutingLayer>(shape.layer.entity()).width;
}

Rect segmentBounds(const DesignRoutingStorage::registry_type& design, const TechRegistry::registry_type& tech,
                   const DesignRouteShape& shape, const DesignRouteShapeAttributes* attributes)
{
  const auto& segment = std::get<DesignRouteSegment>(shape.geometry);
  const int32_t width = (shape.flags & DesignWirePathFlag::kHasWidth) != 0u
                            ? (attributes == nullptr ? throw std::invalid_argument("route Shape width is missing") : attributes->width)
                            : defaultSegmentWidth(design, tech, shape);
  const int64_t lower_half = static_cast<int64_t>(width) / 2;
  const int64_t upper_half = static_cast<int64_t>(width) - lower_half;
  const int64_t begin_x = segment.begin.position.x;
  const int64_t begin_y = segment.begin.position.y;
  const int64_t end_x = segment.end.position.x;
  const int64_t end_y = segment.end.position.y;
  const bool truncated = (shape.flags & DesignWirePathFlag::kHasShape) != 0u
                         && attributes != nullptr && !attributes->shape.empty();
  const bool begin_has_extension = truncated || (segment.begin.flags & DesignWirePointFlag::kHasExtension) != 0u;
  const bool end_has_extension = truncated || (segment.end.flags & DesignWirePointFlag::kHasExtension) != 0u;
  const int64_t begin_extension = !truncated && begin_has_extension ? segment.begin.extension : 0;
  const int64_t end_extension = !truncated && end_has_extension ? segment.end.extension : 0;
  if (begin_y == end_y && begin_x != end_x) {
    const int64_t left = begin_x < end_x ? begin_x - (begin_has_extension ? begin_extension : lower_half)
                                         : end_x - (end_has_extension ? end_extension : lower_half);
    const int64_t right = begin_x < end_x ? end_x + (end_has_extension ? end_extension : upper_half)
                                          : begin_x + (begin_has_extension ? begin_extension : upper_half);
    return checkedRect(left, begin_y - lower_half, right, begin_y + upper_half);
  } else if (begin_x == end_x && begin_y != end_y) {
    const int64_t bottom = begin_y < end_y ? begin_y - (begin_has_extension ? begin_extension : lower_half)
                                           : end_y - (end_has_extension ? end_extension : lower_half);
    const int64_t top = begin_y < end_y ? end_y + (end_has_extension ? end_extension : upper_half)
                                        : begin_y + (begin_has_extension ? begin_extension : upper_half);
    return checkedRect(begin_x - lower_half, bottom, begin_x + upper_half, top);
  }
  // Conservative for diagonal geometry; Manhattan bounds above are exact.
  const auto diagonal_extension = std::max<int64_t>({0, begin_extension, end_extension});
  return checkedRect(std::min(begin_x, end_x) - lower_half - diagonal_extension,
                     std::min(begin_y, end_y) - lower_half - diagonal_extension,
                     std::max(begin_x, end_x) + upper_half + diagonal_extension,
                     std::max(begin_y, end_y) + upper_half + diagonal_extension);
}

void destroyShapeEntities(DesignRoutingStorage::registry_type& registry, const std::vector<DesignRouteShapeId>& ids)
{
  for (const auto id : ids) {
    registry.destroy(id.entity());
  }
}

std::size_t dynamicStringCapacityBytes(const std::string& value) noexcept
{
  const auto data = reinterpret_cast<uintptr_t>(value.data());
  const auto object = reinterpret_cast<uintptr_t>(&value);
  return data >= object && data < object + sizeof(value) ? 0u : value.capacity() + 1u;
}

void validateShapeValue(const DesignRoutingStorage::registry_type& design, const TechRegistry::registry_type& tech,
                        const DesignWire& wire, const DesignRouteShape& shape,
                        const std::optional<DesignRouteShapeAttributes>& attributes)
{
  if (!shape.layer || !tech.valid(shape.layer.entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(shape.layer.entity())) {
    throw std::invalid_argument("route Shape references an invalid routing layer");
  }
  if (hasShapeValueAttributes(shape.flags) != attributes.has_value()) {
    throw std::invalid_argument("route Shape attributes do not match its flags");
  }

  std::array<DesignWirePoint, 2> points;
  std::array<DesignWireVia, 1> vias;
  std::array<DesignWireRectangle, 1> rectangles;
  std::size_t point_count = 0u;
  std::size_t via_count = 0u;
  std::size_t rectangle_count = 0u;
  std::visit(
      [&](const auto& geometry) {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, DesignRouteSegment>) {
          points = {geometry.begin, geometry.end};
          point_count = 2u;
        } else if constexpr (std::is_same_v<Geometry, DesignRouteVia>) {
          points[0] = geometry.origin;
          vias[0] = geometry.via;
          vias[0].point_index = 0u;
          point_count = 1u;
          via_count = 1u;
        } else if constexpr (std::is_same_v<Geometry, DesignRoutePatch>) {
          points[0] = geometry.anchor;
          rectangles[0] = relativePatch(geometry.rectangle, geometry.anchor);
          point_count = 1u;
          rectangle_count = 1u;
        } else {
          points[0] = geometry.point;
          point_count = 1u;
        }
      },
      shape.geometry);
  validatePathValue(design, tech,
                    DesignWirePathView::create(shape.layer, shape.flags,
                                               attributes.has_value() ? &*attributes : nullptr,
                                               DesignWirePointRange::fromValues(
                                                   std::span<const DesignWirePoint>{points.data(), point_count}),
                                               DesignWireViaRange::fromValues(std::span<const DesignWireVia>{vias.data(), via_count}),
                                               std::span<const DesignWireRectangle>{rectangles.data(), rectangle_count}));

  const bool special_net = design.all_of<DesignSpecialNet>(wire.net.entity());
  const bool has_width = (shape.flags & DesignWirePathFlag::kHasWidth) != 0u;
  const bool has_shape = (shape.flags & DesignWirePathFlag::kHasShape) != 0u;
  if (special_net != has_width || (!special_net && has_shape)) {
    throw std::invalid_argument("route Shape fields do not match NET or SPECIALNET syntax");
  }
  if (!special_net) {
    if (const auto* via = std::get_if<DesignRouteVia>(&shape.geometry);
        via != nullptr && (via->via.flags & DesignWireViaFlag::kHasArray) != 0u) {
      throw std::invalid_argument("VIA arrays are only valid in SPECIALNET Shapes");
    }
  }
}

uint32_t nextSourcePath(const DesignWire& wire)
{
  if (wire.next_source_path == kUnassignedSourcePath) {
    throw std::overflow_error("route Shape source-path ID exceeds uint32 range");
  }
  return wire.next_source_path;
}

void observeSourcePath(DesignWire& wire, uint32_t source_path)
{
  if (source_path == kUnassignedSourcePath) {
    return;
  }
  if (source_path == std::numeric_limits<uint32_t>::max() - 1u) {
    wire.next_source_path = kUnassignedSourcePath;
    return;
  }
  wire.next_source_path = std::max(wire.next_source_path, source_path + 1u);
}

}  // namespace

DesignViaId DesignRoutingStorage::createVia(DesignVia via)
{
  validateVia(via);
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignVia>(entity, std::move(via));
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignViaId{entity};
}

void DesignRoutingStorage::updateVia(DesignViaId id, DesignVia via)
{
  ensureVia(id);
  validateVia(via, id);
  _registry.replace<DesignVia>(id.entity(), std::move(via));
}

bool DesignRoutingStorage::destroyVia(DesignViaId id)
{
  if (!contains(id) || referencesVia(id)) {
    return false;
  }
  _registry.destroy(id.entity());
  return true;
}

bool DesignRoutingStorage::contains(DesignViaId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignVia>(id.entity());
}

DesignViaId DesignRoutingStorage::findVia(std::string_view name) const
{
  const auto view = _registry.view<const DesignVia>();
  for (const auto entity : view) {
    if (view.get<const DesignVia>(entity).name == name) {
      return DesignViaId{entity};
    }
  }
  return {};
}

const DesignVia& DesignRoutingStorage::via(DesignViaId id) const
{
  ensureVia(id);
  return _registry.get<const DesignVia>(id.entity());
}

std::vector<DesignViaId> DesignRoutingStorage::vias() const
{
  const auto view = _registry.view<const DesignVia>();
  std::vector<DesignViaId> result;
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

DesignNonDefaultRuleId DesignRoutingStorage::createNonDefaultRule(DesignNonDefaultRule rule)
{
  validateNonDefaultRule(rule);
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignNonDefaultRule>(entity, std::move(rule));
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignNonDefaultRuleId{entity};
}

void DesignRoutingStorage::updateNonDefaultRule(DesignNonDefaultRuleId id, DesignNonDefaultRule rule)
{
  ensureNonDefaultRule(id);
  validateNonDefaultRule(rule, id);
  _registry.replace<DesignNonDefaultRule>(id.entity(), std::move(rule));
}

bool DesignRoutingStorage::destroyNonDefaultRule(DesignNonDefaultRuleId id)
{
  if (!contains(id)) {
    return false;
  }
  const auto nets = _registry.view<const DesignNet>();
  for (const auto entity : nets) {
    if (nets.get<const DesignNet>(entity).design_non_default_rule == id) {
      return false;
    }
  }
  _registry.destroy(id.entity());
  return true;
}

bool DesignRoutingStorage::contains(DesignNonDefaultRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignNonDefaultRule>(id.entity());
}

DesignNonDefaultRuleId DesignRoutingStorage::findNonDefaultRule(std::string_view name) const
{
  const auto view = _registry.view<const DesignNonDefaultRule>();
  for (const auto entity : view) {
    if (view.get<const DesignNonDefaultRule>(entity).name == name) {
      return DesignNonDefaultRuleId{entity};
    }
  }
  return {};
}

const DesignNonDefaultRule& DesignRoutingStorage::nonDefaultRule(DesignNonDefaultRuleId id) const
{
  ensureNonDefaultRule(id);
  return _registry.get<const DesignNonDefaultRule>(id.entity());
}

std::vector<DesignNonDefaultRuleId> DesignRoutingStorage::nonDefaultRules() const
{
  const auto view = _registry.view<const DesignNonDefaultRule>();
  std::vector<DesignNonDefaultRuleId> result;
  result.reserve(view.size());
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

DesignWireId DesignRoutingStorage::createWire(DesignWire wire)
{
  validateWireMetadata(_registry, wire);
  if (!wire.shapes.empty()) {
    throw std::invalid_argument("new Shape-first wire cannot import foreign Shape IDs");
  }
  const auto owner = wire.net;
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignWire>(entity, std::move(wire));
    _wires_by_net[owner.packed()].emplace_back(entity);
  } catch (...) {
    const auto indexed = _wires_by_net.find(owner.packed());
    if (indexed != _wires_by_net.end() && indexed->second.empty()) {
      _wires_by_net.erase(indexed);
    }
    _registry.destroy(entity);
    throw;
  }
  return DesignWireId{entity};
}

DesignRouteShapeId DesignRoutingStorage::createShape(DesignWireId wire_id, DesignRouteShape value,
                                                      std::optional<DesignRouteShapeAttributes> attributes)
{
  ensureWire(wire_id);
  auto& owner = _registry.get<DesignWire>(wire_id.entity());
  if (value.wire && value.wire != wire_id) {
    throw std::invalid_argument("route Shape belongs to a different Wire");
  }
  value.wire = wire_id;
  if (value.source_path == kUnassignedSourcePath) {
    value.source_path = nextSourcePath(owner);
  }
  if (auto* via = std::get_if<DesignRouteVia>(&value.geometry)) {
    normalizeViaExitLayer(_registry, _tech_registry.registry(), value.layer, *via);
  }
  validateShapeValue(_registry, _tech_registry.registry(), owner, value, attributes);

  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignRouteShape>(entity, std::move(value));
    if (attributes.has_value()) {
      _registry.emplace<DesignRouteShapeAttributes>(entity, std::move(*attributes));
    }
    owner.shapes.emplace_back(entity);
    observeSourcePath(owner, _registry.get<const DesignRouteShape>(entity).source_path);
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignRouteShapeId{entity};
}

void DesignRoutingStorage::updateShape(DesignRouteShapeId id, DesignRouteShape value,
                                       std::optional<DesignRouteShapeAttributes> attributes)
{
  if (!contains(id)) {
    throw std::out_of_range("invalid route Shape id");
  }
  const auto& previous = _registry.get<const DesignRouteShape>(id.entity());
  if (value.wire && value.wire != previous.wire) {
    throw std::invalid_argument("route Shape cannot change owner Wire");
  }
  value.wire = previous.wire;
  if (value.source_path == kUnassignedSourcePath) {
    value.source_path = previous.source_path;
  }
  auto& owner = _registry.get<DesignWire>(value.wire.entity());
  if (auto* via = std::get_if<DesignRouteVia>(&value.geometry)) {
    normalizeViaExitLayer(_registry, _tech_registry.registry(), value.layer, *via);
  }
  std::optional<DesignRouteShapeAttributes> effective_attributes;
  if (hasShapeValueAttributes(value.flags)) {
    if (attributes.has_value()) {
      effective_attributes = std::move(attributes);
    } else if (const auto* existing = shapeAttributes(id); existing != nullptr) {
      effective_attributes = *existing;
    }
  }
  validateShapeValue(_registry, _tech_registry.registry(), owner, value, effective_attributes);
  _registry.replace<DesignRouteShape>(id.entity(), std::move(value));
  if (effective_attributes.has_value()) {
    _registry.emplace_or_replace<DesignRouteShapeAttributes>(id.entity(), std::move(*effective_attributes));
  } else {
    _registry.remove<DesignRouteShapeAttributes>(id.entity());
  }
  observeSourcePath(owner, _registry.get<const DesignRouteShape>(id.entity()).source_path);
}

bool DesignRoutingStorage::destroyShape(DesignRouteShapeId id)
{
  if (!contains(id)) {
    return false;
  }
  const auto owner = _registry.get<const DesignRouteShape>(id.entity()).wire;
  if (contains(owner)) {
    std::erase(_registry.get<DesignWire>(owner.entity()).shapes, id);
  }
  _registry.destroy(id.entity());
  return true;
}

bool DesignRoutingStorage::contains(DesignRouteShapeId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignRouteShape>(id.entity());
}

Rect DesignRoutingStorage::shapeBoundingBox(DesignRouteShapeId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid route Shape id");
  }
  const auto& value = shape(id);
  const auto* attributes = shapeAttributes(id);
  return std::visit(
      [&](const auto& geometry) -> Rect {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, DesignRouteSegment>) {
          return segmentBounds(_registry, _tech_registry.registry(), value, attributes);
        } else if constexpr (std::is_same_v<Geometry, DesignRouteVia>) {
          return viaBounds(_registry, _tech_registry.registry(), geometry);
        } else if constexpr (std::is_same_v<Geometry, DesignRoutePatch>) {
          return geometry.rectangle;
        } else {
          return checkedRect(geometry.point.position.x, geometry.point.position.y, geometry.point.position.x,
                             geometry.point.position.y);
        }
      },
      value.geometry);
}

bool DesignRoutingStorage::destroyWire(DesignWireId id)
{
  if (!contains(id)) {
    return false;
  }
  auto& wire_value = _registry.get<DesignWire>(id.entity());
  const auto net = wire_value.net;
  auto shapes = std::move(wire_value.shapes);
  auto found = _wires_by_net.find(net.packed());
  if (found != _wires_by_net.end()) {
    std::erase(found->second, id);
    if (found->second.empty()) {
      _wires_by_net.erase(found);
    }
  }
  destroyShapeEntities(_registry, shapes);
  _registry.destroy(id.entity());
  return true;
}

bool DesignRoutingStorage::contains(DesignWireId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignWire>(id.entity());
}

const DesignWire& DesignRoutingStorage::wire(DesignWireId id) const
{
  ensureWire(id);
  return _registry.get<const DesignWire>(id.entity());
}

std::vector<DesignWireId> DesignRoutingStorage::wires() const
{
  const auto view = _registry.view<const DesignWire>();
  std::vector<DesignWireId> result;
  result.reserve(view.size());
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

std::vector<DesignWireId> DesignRoutingStorage::wires(DesignNetId owner) const
{
  const auto ids = wireIds(owner);
  return std::vector<DesignWireId>(ids.begin(), ids.end());
}

std::span<const DesignWireId> DesignRoutingStorage::wireIds(DesignNetId owner) const
{
  if (!owner || !_registry.valid(owner.entity()) || !_registry.all_of<DesignNet>(owner.entity())) {
    throw std::out_of_range("invalid design net id");
  }
  const auto found = _wires_by_net.find(owner.packed());
  return found == _wires_by_net.end() ? std::span<const DesignWireId>{} : std::span<const DesignWireId>{found->second};
}

void DesignRoutingStorage::rebuildWireIndex()
{
  decltype(_wires_by_net) rebuilt;
  const auto view = _registry.view<const DesignWire>();
  for (const auto entity : view) {
    rebuilt[view.get<const DesignWire>(entity).net.packed()].emplace_back(entity);
  }
  _wires_by_net.swap(rebuilt);
}

void DesignRoutingStorage::shrinkRoutingToFit()
{
  const auto wires = _registry.view<DesignWire>();
  for (const auto entity : wires) {
    wires.get<DesignWire>(entity).shapes.shrink_to_fit();
  }
  const auto attributes = _registry.view<DesignRouteShapeAttributes>();
  for (const auto entity : attributes) {
    auto& value = attributes.get<DesignRouteShapeAttributes>(entity);
    value.taper_rule.shrink_to_fit();
    value.shape.shrink_to_fit();
  }
}

DesignRoutingStatistics DesignRoutingStorage::routingStatistics() const
{
  DesignRoutingStatistics result;
  const auto shape_view = _registry.view<const DesignRouteShape>();
  result.shapes.count = shape_view.size();
  result.shapes.capacity = shape_view.size();
  result.shapes.element_size = sizeof(DesignRouteShape);
  result.shapes.capacity_bytes = result.shapes.count * sizeof(DesignRouteShape);
  const auto attribute_view = _registry.view<const DesignRouteShapeAttributes>();
  result.path_extras.count = attribute_view.size();
  result.path_extras.capacity = attribute_view.size();
  result.path_extras.element_size = sizeof(DesignRouteShapeAttributes);
  result.path_extras.capacity_bytes = result.path_extras.count * sizeof(DesignRouteShapeAttributes);
  for (const auto entity : attribute_view) {
    const auto& attributes = attribute_view.get<const DesignRouteShapeAttributes>(entity);
    result.dynamic_string_capacity_bytes += dynamicStringCapacityBytes(attributes.taper_rule);
    result.dynamic_string_capacity_bytes += dynamicStringCapacityBytes(attributes.shape);
  }

  const auto wire_view = _registry.view<const DesignWire>();
  for (const auto entity : wire_view) {
    const auto& wire_value = wire_view.get<const DesignWire>(entity);
    result.wire_shape_ids.count += wire_value.shapes.size();
    result.wire_shape_ids.capacity += wire_value.shapes.capacity();
    result.wire_shape_ids.element_size = sizeof(DesignRouteShapeId);
    result.wire_shape_ids.capacity_bytes += wire_value.shapes.capacity() * sizeof(DesignRouteShapeId);
    result.wire_shape_ids.buffer_count += wire_value.shapes.capacity() != 0u;

    bool open = false;
    uint32_t source_path = kUnassignedSourcePath;
    uint32_t path_flags = 0;
    TechRoutingLayerId active_layer;
    std::optional<DesignWirePoint> last_point;
    const DesignRouteShapeAttributes* current_attributes = nullptr;
    const auto compatible = [&](const DesignRouteShape& value, const DesignRouteShapeAttributes* attributes) {
      if (!open || source_path != value.source_path || active_layer != value.layer || path_flags != value.flags) {
        return false;
      }
      return attributes == nullptr || current_attributes == nullptr ? attributes == current_attributes
                                                                     : sameAttributes(*current_attributes, *attributes);
    };
    const auto begin = [&](const DesignRouteShape& value, const DesignRouteShapeAttributes* attributes,
                           DesignWirePoint point) {
      ++result.paths.count;
      ++result.points.count;
      open = true;
      source_path = value.source_path;
      path_flags = value.flags;
      active_layer = value.layer;
      current_attributes = attributes;
      last_point = point;
    };
    for (const auto shape_id : wire_value.shapes) {
      const auto& value = shape(shape_id);
      const auto* attributes = shapeAttributes(shape_id);
      std::visit(
          [&](const auto& geometry) {
            using Geometry = std::remove_cvref_t<decltype(geometry)>;
            if constexpr (std::is_same_v<Geometry, DesignRouteSegment>) {
              if (!compatible(value, attributes) || !last_point.has_value() || !samePoint(*last_point, geometry.begin)) {
                begin(value, attributes, geometry.begin);
              }
              ++result.points.count;
              last_point = geometry.end;
            } else if constexpr (std::is_same_v<Geometry, DesignRouteVia>) {
              if (!compatible(value, attributes) || !last_point.has_value() || !samePoint(*last_point, geometry.origin)) {
                begin(value, attributes, geometry.origin);
              }
              ++result.vias.count;
              active_layer = geometry.exit_layer;
              last_point = geometry.origin;
            } else if constexpr (std::is_same_v<Geometry, DesignRoutePatch>) {
              if (!compatible(value, attributes) || !last_point.has_value() || !samePoint(*last_point, geometry.anchor)) {
                begin(value, attributes, geometry.anchor);
              }
              ++result.rectangles.count;
              last_point = geometry.anchor;
            } else if (!compatible(value, attributes) || !last_point.has_value() || !samePoint(*last_point, geometry.point)) {
              begin(value, attributes, geometry.point);
            }
          },
          value.geometry);
    }
  }
  // paths/points/vias/rectangles are materialized compatibility projections.
  // They deliberately do not report capacity or allocation bytes as storage.
  return result;
}

void DesignRoutingStorage::validateRestoredRoutingState() const
{
  const auto wire_view = _registry.view<const DesignWire>();
  for (const auto entity : wire_view) {
    const DesignWireId wire_id{entity};
    const auto& wire_value = wire_view.get<const DesignWire>(entity);
    try {
      validateWireMetadata(_registry, wire_value);
      uint32_t highest_source = 0u;
      bool has_source = false;
      for (const auto shape_id : wire_value.shapes) {
        if (!contains(shape_id)) {
          throw std::invalid_argument("Wire references a missing Shape");
        }
        const auto& shape_value = shape(shape_id);
        if (shape_value.wire != wire_id) {
          throw std::invalid_argument("Wire Shape owner reference is inconsistent");
        }
        std::optional<DesignRouteShapeAttributes> attributes;
        if (const auto* value = shapeAttributes(shape_id); value != nullptr) {
          attributes = *value;
        }
        validateShapeValue(_registry, _tech_registry.registry(), wire_value, shape_value, attributes);
        if (const auto* via = std::get_if<DesignRouteVia>(&shape_value.geometry); via != nullptr) {
          const auto layers = routingLayersForVia(_registry, _tech_registry.registry(), via->via);
          if (!isRoutingLayer(_tech_registry.registry(), via->exit_layer)
              || (layers.has_value() && via->exit_layer != viaExitLayer(shape_value.layer, via->via))) {
            throw std::invalid_argument("route VIA exit layer is inconsistent");
          }
        }
        if (shape_value.source_path != kUnassignedSourcePath) {
          highest_source = has_source ? std::max(highest_source, shape_value.source_path) : shape_value.source_path;
          has_source = true;
        }
      }
      if (has_source && wire_value.next_source_path != kUnassignedSourcePath && highest_source >= wire_value.next_source_path) {
        throw std::invalid_argument("Wire source-path counter is inconsistent");
      }
    } catch (const std::exception& error) {
      throw std::runtime_error(std::string{"binary design has invalid Wire Shape state: "} + error.what());
    }
  }
  const auto shape_view = _registry.view<const DesignRouteShape>();
  for (const auto entity : shape_view) {
    const auto& shape_value = shape_view.get<const DesignRouteShape>(entity);
    if (!contains(shape_value.wire)) {
      throw std::runtime_error("binary design has a route Shape with a missing Wire owner");
    }
    const auto& owner = wire(shape_value.wire).shapes;
    const DesignRouteShapeId id{entity};
    if (std::count(owner.begin(), owner.end(), id) != 1) {
      throw std::runtime_error("binary design has a route Shape with inconsistent Wire membership");
    }
  }
}

void DesignRoutingStorage::setNetGeometry(DesignNetId net, DesignNetGeometry geometry)
{
  validateNetGeometry(net, geometry);
  _registry.emplace_or_replace<DesignNetGeometry>(net.entity(), std::move(geometry));
}


const DesignNetGeometry* DesignRoutingStorage::netGeometry(DesignNetId net) const
{
  if (!net || !_registry.valid(net.entity()) || !_registry.all_of<DesignNet>(net.entity())) {
    throw std::out_of_range("invalid design net id");
  }
  return _registry.try_get<const DesignNetGeometry>(net.entity());
}

uint32_t DesignRoutingStorage::viaCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignVia>().size());
}

uint32_t DesignRoutingStorage::nonDefaultRuleCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignNonDefaultRule>().size());
}

uint32_t DesignRoutingStorage::wireCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignWire>().size());
}

void DesignRoutingStorage::validateVia(const DesignVia& value, DesignViaId ignored) const
{
  constexpr uint32_t kKnownFlags = DesignViaFlag::kGenerated | DesignViaFlag::kHasPatternName;
  if (value.name.empty() || viaNameInUse(value.name, ignored) || (value.flags & ~kKnownFlags) != 0u
      || ((value.flags & DesignViaFlag::kHasPatternName) != 0u) == value.pattern_name.empty()) {
    throw std::invalid_argument("design via name is empty or duplicated");
  }
  const bool generated = (value.flags & DesignViaFlag::kGenerated) != 0u;
  if (generated) {
    if (!value.rectangles.empty() || !value.polygons.empty()) {
      throw std::invalid_argument("generated design via cannot also contain fixed geometry");
    }
    const auto& formula = value.generated;
    constexpr uint32_t kKnownGeneratedFlags = DesignGeneratedViaFlag::kHasRowCol | DesignGeneratedViaFlag::kHasOrigin
                                              | DesignGeneratedViaFlag::kHasOffset | DesignGeneratedViaFlag::kHasCutPattern;
    const auto& tech = _tech_registry.registry();
    if ((formula.flags & ~kKnownGeneratedFlags) != 0u || !formula.via_rule || !tech.valid(formula.via_rule.entity())
        || !tech.all_of<TechViaRuleGenerate, TechViaRuleGenerateBottomLayer, TechViaRuleGenerateCutLayer, TechViaRuleGenerateTopLayer>(
            formula.via_rule.entity())
        || !formula.bottom_layer || !tech.valid(formula.bottom_layer.entity())
        || !tech.all_of<TechLayerInfo, TechRoutingLayer>(formula.bottom_layer.entity()) || !formula.cut_layer
        || !tech.valid(formula.cut_layer.entity()) || !tech.all_of<TechLayerInfo, TechCutLayer>(formula.cut_layer.entity())
        || !formula.top_layer || !tech.valid(formula.top_layer.entity())
        || !tech.all_of<TechLayerInfo, TechRoutingLayer>(formula.top_layer.entity()) || formula.cut_size_x <= 0 || formula.cut_size_y <= 0
        || formula.cut_spacing_x < 0 || formula.cut_spacing_y < 0 || formula.bottom_enclosure_x < 0 || formula.bottom_enclosure_y < 0
        || formula.top_enclosure_x < 0 || formula.top_enclosure_y < 0) {
      throw std::invalid_argument("generated design via formula or references are invalid");
    }
    if (tech.get<const TechViaRuleGenerateBottomLayer>(formula.via_rule.entity()).layer != formula.bottom_layer
        || tech.get<const TechViaRuleGenerateCutLayer>(formula.via_rule.entity()).layer != formula.cut_layer
        || tech.get<const TechViaRuleGenerateTopLayer>(formula.via_rule.entity()).layer != formula.top_layer) {
      throw std::invalid_argument("generated design via layers do not match its technology VIARULE");
    }
    const bool has_row_col = (formula.flags & DesignGeneratedViaFlag::kHasRowCol) != 0u;
    const bool has_origin = (formula.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u;
    const bool has_offset = (formula.flags & DesignGeneratedViaFlag::kHasOffset) != 0u;
    const bool has_cut_pattern = (formula.flags & DesignGeneratedViaFlag::kHasCutPattern) != 0u;
    if ((has_row_col && (formula.row_count == 0u || formula.column_count == 0u))
        || (!has_row_col && (formula.row_count != 1u || formula.column_count != 1u)) || (!has_origin && formula.origin != Point{})
        || (!has_offset && (formula.bottom_offset != Point{} || formula.top_offset != Point{}))
        || has_cut_pattern == formula.cut_pattern.empty()) {
      throw std::invalid_argument("generated design via optional fields and flags are inconsistent");
    }
    return;
  }

  const auto& formula = value.generated;
  if (formula.via_rule || formula.bottom_layer || formula.cut_layer || formula.top_layer || formula.flags != 0u || formula.cut_size_x != 0
      || formula.cut_size_y != 0 || formula.cut_spacing_x != 0 || formula.cut_spacing_y != 0 || formula.bottom_enclosure_x != 0
      || formula.bottom_enclosure_y != 0 || formula.top_enclosure_x != 0 || formula.top_enclosure_y != 0 || formula.row_count != 1u
      || formula.column_count != 1u || formula.origin != Point{} || formula.bottom_offset != Point{} || formula.top_offset != Point{}
      || !formula.cut_pattern.empty() || (value.rectangles.empty() && value.polygons.empty())) {
    throw std::invalid_argument("fixed design via contains generated fields or no geometry");
  }
  const auto& tech = _tech_registry.registry();
  for (const auto& rectangle : value.rectangles) {
    if (!rectangle.layer || !tech.valid(rectangle.layer.entity()) || !tech.all_of<TechLayerInfo>(rectangle.layer.entity())
        || !rectangle.rectangle.hasArea()) {
      throw std::invalid_argument("fixed design via rectangle is invalid");
    }
  }
  for (const auto& polygon : value.polygons) {
    if (!polygon.layer || !tech.valid(polygon.layer.entity()) || !tech.all_of<TechLayerInfo>(polygon.layer.entity())
        || polygon.points.size() < 3u) {
      throw std::invalid_argument("fixed design via polygon is invalid");
    }
  }
}

void DesignRoutingStorage::validateNonDefaultRule(const DesignNonDefaultRule& value, DesignNonDefaultRuleId ignored) const
{
  if (value.name.empty() || nonDefaultRuleNameInUse(value.name, ignored)
      || (value.flags & ~DesignNonDefaultRuleFlag::kHardSpacing) != 0u) {
    throw std::invalid_argument("design non-default rule name or flags are invalid");
  }

  const auto& tech = _tech_registry.registry();
  constexpr uint32_t kKnownLayerFlags
      = DesignNdrLayerRuleFlag::kHasDiagWidth | DesignNdrLayerRuleFlag::kHasSpacing | DesignNdrLayerRuleFlag::kHasWireExtension;
  for (std::size_t index = 0; index < value.layer_rules.size(); ++index) {
    const auto& rule = value.layer_rules[index];
    const bool has_diag_width = (rule.flags & DesignNdrLayerRuleFlag::kHasDiagWidth) != 0u;
    const bool has_spacing = (rule.flags & DesignNdrLayerRuleFlag::kHasSpacing) != 0u;
    const bool has_wire_extension = (rule.flags & DesignNdrLayerRuleFlag::kHasWireExtension) != 0u;
    if (!rule.layer || !tech.valid(rule.layer.entity()) || !tech.all_of<TechLayerInfo>(rule.layer.entity()) || rule.width <= 0
        || (rule.flags & ~kKnownLayerFlags) != 0u || (has_diag_width ? rule.diag_width <= 0 : rule.diag_width != 0)
        || (has_spacing ? rule.spacing < 0 : rule.spacing != 0)
        || (has_wire_extension ? rule.wire_extension < 0 : rule.wire_extension != 0)) {
      throw std::invalid_argument("design non-default layer rule is invalid");
    }
    for (std::size_t prior = 0; prior < index; ++prior) {
      if (value.layer_rules[prior].layer == rule.layer) {
        throw std::invalid_argument("design non-default rule repeats a layer");
      }
    }
  }

  for (const auto& via : value.vias) {
    const bool has_tech_via = static_cast<bool>(via.tech_via);
    const bool has_design_via = static_cast<bool>(via.design_via);
    if (has_tech_via == has_design_via
        || (has_tech_via && (!tech.valid(via.tech_via.entity()) || !tech.all_of<TechViaMaster>(via.tech_via.entity())))
        || (has_design_via && !contains(via.design_via))) {
      throw std::invalid_argument("design non-default rule VIA reference is invalid");
    }
  }
  for (const auto via_rule : value.via_rules) {
    if (!via_rule || !tech.valid(via_rule.entity()) || !tech.all_of<TechViaRuleGenerate>(via_rule.entity())) {
      throw std::invalid_argument("design non-default rule VIARULE reference is invalid");
    }
  }
  for (std::size_t index = 0; index < value.min_cuts.size(); ++index) {
    const auto& min_cuts = value.min_cuts[index];
    if (!min_cuts.layer || !tech.valid(min_cuts.layer.entity()) || !tech.all_of<TechLayerInfo, TechCutLayer>(min_cuts.layer.entity())
        || min_cuts.cut_count == 0u) {
      throw std::invalid_argument("design non-default rule MINCUTS is invalid");
    }
    for (std::size_t prior = 0; prior < index; ++prior) {
      if (value.min_cuts[prior].layer == min_cuts.layer) {
        throw std::invalid_argument("design non-default rule repeats MINCUTS for a layer");
      }
    }
  }
  if (std::any_of(value.properties.begin(), value.properties.end(), [](const auto& property) { return property.name.empty(); })) {
    throw std::invalid_argument("design non-default rule property name is required");
  }
}

TechRoutingLayerId DesignRoutingStorage::viaExitLayer(TechRoutingLayerId layer, const DesignWireVia& via) const
{
  const auto layers = routingLayersForVia(_registry, _tech_registry.registry(), via);
  if (!layers.has_value()) {
    return layer;
  }
  if (layer == layers->first) {
    return layers->second;
  }
  if (layer == layers->second) {
    return layers->first;
  }
  throw std::invalid_argument("wire VIA does not touch its routing layer");
}

void DesignRoutingStorage::validateNetGeometry(DesignNetId net, const DesignNetGeometry& geometry) const
{
  if (!net || !_registry.valid(net.entity()) || !_registry.all_of<DesignNet, DesignSpecialNet>(net.entity())) {
    throw std::invalid_argument("standalone net geometry requires a valid SPECIALNET owner");
  }
  if (geometry.rectangles.empty() && geometry.polygons.empty() && geometry.vias.empty()) {
    throw std::invalid_argument("standalone net geometry cannot be empty");
  }

  const auto& tech = _tech_registry.registry();
  for (const auto& rectangle : geometry.rectangles) {
    validateNetGeometryOptions(rectangle.route_status, rectangle.flags, rectangle.mask, rectangle.shield_net, rectangle.shape);
    if (!rectangle.layer || !tech.valid(rectangle.layer.entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(rectangle.layer.entity())
        || !rectangle.rectangle.hasArea()) {
      throw std::invalid_argument("standalone net rectangle is invalid");
    }
  }
  for (const auto& polygon : geometry.polygons) {
    validateNetGeometryOptions(polygon.route_status, polygon.flags, polygon.mask, polygon.shield_net, polygon.shape);
    if (!polygon.layer || !tech.valid(polygon.layer.entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(polygon.layer.entity())
        || polygon.points.size() < 3u) {
      throw std::invalid_argument("standalone net polygon is invalid");
    }
  }
  for (const auto& via : geometry.vias) {
    validateNetGeometryOptions(via.route_status, via.flags, via.top_mask || via.cut_mask || via.bottom_mask, via.shield_net, via.shape);
    const bool has_tech_via = static_cast<bool>(via.tech_via);
    const bool has_design_via = static_cast<bool>(via.design_via);
    if (has_tech_via == has_design_via || via.origins.empty() || !isValidDesignOrientation(via.orientation)
        || (has_tech_via && (!tech.valid(via.tech_via.entity()) || !tech.all_of<TechViaMaster>(via.tech_via.entity())))
        || (has_design_via && !contains(via.design_via))) {
      throw std::invalid_argument("standalone net VIA is invalid");
    }
  }
}

bool DesignRoutingStorage::viaNameInUse(std::string_view name, DesignViaId ignored) const
{
  const auto view = _registry.view<const DesignVia>();
  for (const auto entity : view) {
    if (entity != ignored.entity() && view.get<const DesignVia>(entity).name == name) {
      return true;
    }
  }
  return false;
}

bool DesignRoutingStorage::nonDefaultRuleNameInUse(std::string_view name, DesignNonDefaultRuleId ignored) const
{
  const auto view = _registry.view<const DesignNonDefaultRule>();
  for (const auto entity : view) {
    if (entity != ignored.entity() && view.get<const DesignNonDefaultRule>(entity).name == name) {
      return true;
    }
  }
  return false;
}

bool DesignRoutingStorage::referencesVia(DesignViaId id) const
{
  const auto pin_access_references = [id](const auto& groups) {
    for (const auto& group : groups) {
      for (const auto& point : group.points) {
        if (std::any_of(point.vias.begin(), point.vias.end(), [id](const auto& via) { return via.design_via == id; })) {
          return true;
        }
      }
    }
    return false;
  };
  const auto ndr_view = _registry.view<const DesignNonDefaultRule>();
  for (const auto entity : ndr_view) {
    for (const auto& via : ndr_view.get<const DesignNonDefaultRule>(entity).vias) {
      if (via.design_via == id) {
        return true;
      }
    }
  }
  const auto shape_view = _registry.view<const DesignRouteShape>();
  for (const auto entity : shape_view) {
    const auto& shape_value = shape_view.get<const DesignRouteShape>(entity);
    if (const auto* via = std::get_if<DesignRouteVia>(&shape_value.geometry); via != nullptr && via->via.design_via == id) {
      return true;
    }
  }
  const auto pin_view = _registry.view<const DesignBlockPin>();
  for (const auto entity : pin_view) {
    for (const auto& via : pin_view.get<const DesignBlockPin>(entity).vias) {
      if (via.design_via == id) {
        return true;
      }
    }
  }
  const auto geometry_view = _registry.view<const DesignNetGeometry>();
  for (const auto entity : geometry_view) {
    for (const auto& via : geometry_view.get<const DesignNetGeometry>(entity).vias) {
      if (via.design_via == id) {
        return true;
      }
    }
  }
  const auto catalog_view = _registry.view<const DesignPinAccessCatalog>();
  for (const auto entity : catalog_view) {
    for (const auto& port_groups : catalog_view.get<const DesignPinAccessCatalog>(entity).master_port_groups) {
      if (pin_access_references(port_groups.groups)) {
        return true;
      }
    }
  }
  const auto block_pin_access_view = _registry.view<const DesignBlockPin, const DesignBlockPinAccessGroups>();
  for (const auto entity : block_pin_access_view) {
    if (pin_access_references(block_pin_access_view.get<const DesignBlockPinAccessGroups>(entity).groups)) {
      return true;
    }
  }
  return false;
}

void DesignRoutingStorage::ensureVia(DesignViaId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design via id");
  }
}

void DesignRoutingStorage::ensureNonDefaultRule(DesignNonDefaultRuleId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design non-default rule id");
  }
}

void DesignRoutingStorage::ensureWire(DesignWireId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design wire id");
  }
}

}  // namespace eccdb
