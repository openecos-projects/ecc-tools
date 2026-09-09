// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "design/DesignStore.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/non_default_rule/component/NonDefaultRuleComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/via/component/ViaComponents.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "library/site/model/SiteComponents.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb::test {

struct DesignSemanticSnapshot
{
  std::array<std::size_t, 15> component_counts{};
  std::string global;
  std::vector<std::string> rows;
  std::vector<std::string> track_grids;
  std::vector<std::string> gcell_grids;
  std::vector<std::string> instances;
  std::vector<std::string> instance_pins;
  std::vector<std::string> io_pins;
  std::vector<std::string> vias;
  std::vector<std::string> non_default_rules;
  std::vector<std::string> nets;
  std::vector<std::string> regions;
  std::vector<std::string> groups;
  std::vector<std::string> blockages;
  std::vector<std::string> fills;

  bool operator==(const DesignSemanticSnapshot&) const = default;
};

namespace detail {

class SemanticKey
{
 public:
  void text(std::string_view value) { _stream << 's' << value.size() << ':' << value << ';'; }
  void signedValue(int64_t value) { _stream << 'i' << value << ';'; }
  void unsignedValue(uint64_t value) { _stream << 'u' << value << ';'; }
  void realValue(double value) { _stream << 'd' << std::hexfloat << value << std::defaultfloat << ';'; }

  void point(Point value)
  {
    signedValue(value.x);
    signedValue(value.y);
  }

  void rectangle(Rect value)
  {
    signedValue(value.ll_x);
    signedValue(value.ll_y);
    signedValue(value.ur_x);
    signedValue(value.ur_y);
  }

  void points(const std::vector<Point>& values)
  {
    unsignedValue(values.size());
    for (const auto value : values) {
      point(value);
    }
  }

  [[nodiscard]] std::string str() const { return _stream.str(); }

 private:
  std::ostringstream _stream;
};

inline void sortKeys(std::vector<std::string>& keys)
{
  std::sort(keys.begin(), keys.end());
}

inline void appendKeys(SemanticKey& key, std::vector<std::string> values)
{
  sortKeys(values);
  key.unsignedValue(values.size());
  for (const auto& value : values) {
    key.text(value);
  }
}

inline std::string rectangleKey(Rect rectangle)
{
  SemanticKey key;
  key.rectangle(rectangle);
  return key.str();
}

inline std::vector<std::string> rectangleKeys(const std::vector<Rect>& rectangles)
{
  std::vector<std::string> result;
  result.reserve(rectangles.size());
  for (const auto rectangle : rectangles) {
    result.push_back(rectangleKey(rectangle));
  }
  return result;
}

template <typename LayerId>
std::string layerName(const DesignStore& design, LayerId id)
{
  if (!id) {
    return {};
  }
  return design.techRegistry().registry().get<const TechLayerInfo>(id.entity()).name;
}

inline std::string siteName(const DesignStore& design, LibrarySiteId id)
{
  if (!id) {
    return {};
  }
  return design.libraryRegistry().registry().get<const LibrarySite>(id.entity()).name;
}

inline std::string masterName(const DesignStore& design, LibraryCellMasterId id)
{
  if (!id) {
    return {};
  }
  return design.libraryRegistry().registry().get<const LibraryCellMaster>(id.entity()).name;
}

inline std::string masterTermName(const DesignStore& design, LibraryMasterTermId id)
{
  if (!id) {
    return {};
  }
  return design.libraryRegistry().registry().get<const LibraryMasterTerm>(id.entity()).name;
}

inline std::string regionName(const DesignStore& design, DesignRegionId id)
{
  return id ? design.constraintStorage().region(id).name : std::string{};
}

inline std::string instanceName(const DesignStore& design, DesignInstanceId id)
{
  return id ? design.netlistStorage().instance(id).name : std::string{};
}

inline std::string netName(const DesignStore& design, DesignNetId id)
{
  if (!id) {
    return {};
  }
  return std::string(design.netlistStorage().isSpecialNet(id) ? "special:" : "regular:") + design.netlistStorage().net(id).name;
}

inline std::string designViaName(const DesignStore& design, DesignViaId id)
{
  return id ? design.routingStorage().via(id).name : std::string{};
}

inline std::string techViaName(const DesignStore& design, TechViaMasterId id)
{
  if (!id) {
    return {};
  }
  return design.techRegistry().registry().get<const TechViaMaster>(id.entity()).name;
}

inline std::string viaName(const DesignStore& design, TechViaMasterId tech_via, DesignViaId design_via)
{
  return tech_via ? "tech:" + techViaName(design, tech_via) : "design:" + designViaName(design, design_via);
}

inline Point transformPoint(Point point, DesignOrientation orientation, Point origin)
{
  Point transformed;
  switch (orientation) {
    case DesignOrientation::kN:
      transformed = point;
      break;
    case DesignOrientation::kS:
      transformed = {-point.x, -point.y};
      break;
    case DesignOrientation::kE:
      transformed = {point.y, -point.x};
      break;
    case DesignOrientation::kW:
      transformed = {-point.y, point.x};
      break;
    case DesignOrientation::kFN:
      transformed = {-point.x, point.y};
      break;
    case DesignOrientation::kFS:
      transformed = {point.x, -point.y};
      break;
    case DesignOrientation::kFE:
      transformed = {-point.y, -point.x};
      break;
    case DesignOrientation::kFW:
      transformed = {point.y, point.x};
      break;
  }
  transformed.x += origin.x;
  transformed.y += origin.y;
  return transformed;
}

inline Rect transformRectangle(Rect rectangle, DesignOrientation orientation, Point origin)
{
  const std::array points{transformPoint({rectangle.ll_x, rectangle.ll_y}, orientation, origin),
                          transformPoint({rectangle.ll_x, rectangle.ur_y}, orientation, origin),
                          transformPoint({rectangle.ur_x, rectangle.ll_y}, orientation, origin),
                          transformPoint({rectangle.ur_x, rectangle.ur_y}, orientation, origin)};
  Rect result{.ll_x = points.front().x, .ll_y = points.front().y, .ur_x = points.front().x, .ur_y = points.front().y};
  for (const auto point : points) {
    result.ll_x = std::min(result.ll_x, point.x);
    result.ll_y = std::min(result.ll_y, point.y);
    result.ur_x = std::max(result.ur_x, point.x);
    result.ur_y = std::max(result.ur_y, point.y);
  }
  return result;
}

inline std::string ndrName(const DesignStore& design, TechNonDefaultRuleId id)
{
  if (!id) {
    return {};
  }
  return design.techRegistry().registry().get<const TechNonDefaultRule>(id.entity()).name;
}

inline std::string ndrName(const DesignStore& design, DesignNonDefaultRuleId id)
{
  return id ? design.routingStorage().nonDefaultRule(id).name : std::string{};
}

inline std::string viaRuleName(const DesignStore& design, TechViaRuleGenerateId id)
{
  if (!id) {
    return {};
  }
  return design.techRegistry().registry().get<const TechViaRuleGenerate>(id.entity()).name;
}

template <typename Component>
std::size_t componentCount(const DesignStore& design)
{
  return design.designRegistry().registry().view<const Component>().size();
}

inline std::string globalKey(const DesignStore& design)
{
  SemanticKey key;
  const auto& info = design.globalStorage().info();
  key.text(info.name);
  key.signedValue(info.database_units_per_micron);
  key.text(info.divider_character);
  key.text(info.bus_bit_characters);
  key.points(design.globalStorage().dieArea().boundary);
  return key.str();
}

inline std::vector<std::string> rowKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.floorplanStorage().rows()) {
    const auto& row = design.floorplanStorage().row(id);
    SemanticKey key;
    key.text(row.name);
    key.text(siteName(design, row.site));
    key.point(row.origin);
    key.unsignedValue(static_cast<uint8_t>(row.orientation));
    key.unsignedValue(row.flags);
    key.unsignedValue(row.repeat_count_x);
    key.unsignedValue(row.repeat_count_y);
    key.signedValue(row.step_x);
    key.signedValue(row.step_y);
    std::vector<std::string> properties;
    properties.reserve(row.properties.size());
    for (const auto& property : row.properties) {
      SemanticKey property_key;
      property_key.text(property.name);
      property_key.text(property.value);
      property_key.unsignedValue(static_cast<uint8_t>(property.type));
      properties.push_back(property_key.str());
    }
    appendKeys(key, std::move(properties));
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline std::vector<std::string> trackGridKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.floorplanStorage().trackGrids()) {
    const auto& grid = design.floorplanStorage().trackGrid(id);
    SemanticKey key;
    key.unsignedValue(static_cast<uint8_t>(grid.axis));
    key.signedValue(grid.start);
    key.unsignedValue(grid.track_count);
    key.signedValue(grid.step);
    key.unsignedValue(grid.flags);
    key.unsignedValue(grid.mask);
    std::vector<std::string> layers;
    layers.reserve(grid.layers.size());
    for (const auto layer : grid.layers) {
      layers.push_back(layerName(design, layer));
    }
    appendKeys(key, std::move(layers));
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline std::vector<std::string> gcellGridKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.floorplanStorage().gcellGrids()) {
    const auto& grid = design.floorplanStorage().gcellGrid(id);
    SemanticKey key;
    key.unsignedValue(static_cast<uint8_t>(grid.axis));
    key.signedValue(grid.start);
    key.unsignedValue(grid.line_count);
    key.signedValue(grid.step);
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline std::vector<std::string> instanceKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.netlistStorage().instances()) {
    const auto& instance = design.netlistStorage().instance(id);
    SemanticKey key;
    key.text(instance.name);
    key.text(masterName(design, instance.master));
    key.point(instance.origin);
    key.unsignedValue(static_cast<uint8_t>(instance.orientation));
    key.unsignedValue(static_cast<uint8_t>(instance.placement_status));
    key.unsignedValue(static_cast<uint8_t>(instance.source));
    key.unsignedValue(instance.flags);
    key.signedValue(instance.weight);
    key.text(regionName(design, instance.region));
    appendKeys(key, rectangleKeys(instance.region_bounds));
    key.signedValue(instance.halo.left);
    key.signedValue(instance.halo.bottom);
    key.signedValue(instance.halo.right);
    key.signedValue(instance.halo.top);
    key.signedValue(instance.route_halo.distance);
    key.text(layerName(design, instance.route_halo.min_layer));
    key.text(layerName(design, instance.route_halo.max_layer));
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline std::vector<std::string> instancePinKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto instance_id : design.netlistStorage().instances()) {
    for (const auto pin_id : design.netlistStorage().instancePins(instance_id)) {
      const auto& pin = design.netlistStorage().instancePin(pin_id);
      SemanticKey key;
      key.text(instanceName(design, pin.instance));
      key.text(masterTermName(design, pin.master_term));
      key.text(netName(design, pin.net));
      key.text(netName(design, pin.special_net));
      result.push_back(key.str());
    }
  }
  sortKeys(result);
  return result;
}

inline std::string pinRectangleKey(const DesignStore& design, const DesignPinRectangle& rectangle, const DesignIoPinPort& port)
{
  SemanticKey key;
  key.text("rectangle");
  key.text(layerName(design, rectangle.layer));
  key.rectangle(transformRectangle(rectangle.rectangle, port.orientation, port.origin));
  key.unsignedValue(rectangle.flags);
  key.unsignedValue(rectangle.mask);
  key.signedValue(rectangle.spacing);
  key.signedValue(rectangle.design_rule_width);
  return key.str();
}

inline std::string pinPolygonKey(const DesignStore& design, const DesignPinPolygon& polygon, const DesignIoPinPort& port)
{
  SemanticKey key;
  key.text("polygon");
  key.text(layerName(design, polygon.layer));
  std::vector<Point> points;
  points.reserve(polygon.points.size());
  for (const auto point : polygon.points) {
    points.push_back(transformPoint(point, port.orientation, port.origin));
  }
  key.points(points);
  key.unsignedValue(polygon.flags);
  key.unsignedValue(polygon.mask);
  key.signedValue(polygon.spacing);
  key.signedValue(polygon.design_rule_width);
  return key.str();
}

inline std::string pinViaKey(const DesignStore& design, const DesignPinVia& via, const DesignIoPinPort& port)
{
  SemanticKey key;
  key.text("via");
  key.text(viaName(design, via.tech_via, via.design_via));
  key.point(transformPoint(via.origin, port.orientation, port.origin));
  key.unsignedValue(via.flags);
  key.unsignedValue(via.top_mask);
  key.unsignedValue(via.cut_mask);
  key.unsignedValue(via.bottom_mask);
  return key.str();
}

inline std::string pinPortKey(const DesignStore& design, const DesignIoPinPort& port)
{
  SemanticKey key;
  key.unsignedValue(port.flags & ~DesignIoPinPortFlag::kExplicit);
  key.unsignedValue(static_cast<uint8_t>(port.placement_status));
  std::vector<std::string> primitives;
  primitives.reserve(port.rectangles.size() + port.polygons.size() + port.vias.size());
  for (const auto& rectangle : port.rectangles) {
    primitives.push_back(pinRectangleKey(design, rectangle, port));
  }
  for (const auto& polygon : port.polygons) {
    primitives.push_back(pinPolygonKey(design, polygon, port));
  }
  for (const auto& via : port.vias) {
    primitives.push_back(pinViaKey(design, via, port));
  }
  if (primitives.empty() && (port.flags & DesignIoPinPortFlag::kHasPlacement) != 0u) {
    SemanticKey location;
    location.text("location");
    location.point(port.origin);
    location.unsignedValue(static_cast<uint8_t>(port.orientation));
    primitives.push_back(location.str());
  }
  appendKeys(key, std::move(primitives));
  return key.str();
}

inline std::vector<std::string> ioPinKeys(const DesignStore& design, bool default_unspecified_use_to_signal = false)
{
  std::vector<std::string> result;
  for (const auto id : design.netlistStorage().ioPins()) {
    const auto& pin = design.netlistStorage().ioPin(id);
    SemanticKey key;
    key.text(pin.name);
    key.text(netName(design, pin.net));
    key.text(netName(design, pin.special_net));
    key.unsignedValue(static_cast<uint8_t>(pin.direction));
    const auto use = default_unspecified_use_to_signal && pin.use == DesignSignalUse::kNone ? DesignSignalUse::kSignal : pin.use;
    key.unsignedValue(static_cast<uint8_t>(use));
    key.unsignedValue(pin.flags & ~DesignIoPinFlag::kSpecial);
    std::vector<std::string> ports;
    ports.reserve(pin.ports.size());
    for (const auto& port : pin.ports) {
      ports.push_back(pinPortKey(design, port));
    }
    appendKeys(key, std::move(ports));
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline std::string viaRectangleKey(const DesignStore& design, const DesignViaRectangle& rectangle)
{
  SemanticKey key;
  key.text("rectangle");
  key.text(layerName(design, rectangle.layer));
  key.rectangle(rectangle.rectangle);
  key.unsignedValue(rectangle.mask);
  return key.str();
}

inline std::string viaPolygonKey(const DesignStore& design, const DesignViaPolygon& polygon)
{
  SemanticKey key;
  key.text("polygon");
  key.text(layerName(design, polygon.layer));
  key.points(polygon.points);
  key.unsignedValue(polygon.mask);
  return key.str();
}

inline std::vector<std::string> viaKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.routingStorage().vias()) {
    const auto& via = design.routingStorage().via(id);
    SemanticKey key;
    key.text(via.name);
    key.unsignedValue(via.flags);
    key.text(via.pattern_name);
    std::vector<std::string> geometry;
    geometry.reserve(via.rectangles.size() + via.polygons.size());
    for (const auto& rectangle : via.rectangles) {
      geometry.push_back(viaRectangleKey(design, rectangle));
    }
    for (const auto& polygon : via.polygons) {
      geometry.push_back(viaPolygonKey(design, polygon));
    }
    appendKeys(key, std::move(geometry));

    const auto& generated = via.generated;
    key.text(viaRuleName(design, generated.via_rule));
    key.text(layerName(design, generated.bottom_layer));
    key.text(layerName(design, generated.cut_layer));
    key.text(layerName(design, generated.top_layer));
    key.unsignedValue(generated.flags);
    key.signedValue(generated.cut_size_x);
    key.signedValue(generated.cut_size_y);
    key.signedValue(generated.cut_spacing_x);
    key.signedValue(generated.cut_spacing_y);
    key.signedValue(generated.bottom_enclosure_x);
    key.signedValue(generated.bottom_enclosure_y);
    key.signedValue(generated.top_enclosure_x);
    key.signedValue(generated.top_enclosure_y);
    key.unsignedValue(generated.row_count);
    key.unsignedValue(generated.column_count);
    key.point(generated.origin);
    key.point(generated.bottom_offset);
    key.point(generated.top_offset);
    key.text(generated.cut_pattern);
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline std::vector<std::string> nonDefaultRuleKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.routingStorage().nonDefaultRules()) {
    const auto& rule = design.routingStorage().nonDefaultRule(id);
    SemanticKey key;
    key.text(rule.name);
    key.unsignedValue(rule.flags);
    key.unsignedValue(rule.layer_rules.size());
    for (const auto& layer_rule : rule.layer_rules) {
      key.text(layerName(design, layer_rule.layer));
      key.unsignedValue(layer_rule.flags);
      key.signedValue(layer_rule.width);
      key.signedValue(layer_rule.diag_width);
      key.signedValue(layer_rule.spacing);
      key.signedValue(layer_rule.wire_extension);
    }
    key.unsignedValue(rule.vias.size());
    for (const auto& via : rule.vias) {
      key.text(via.tech_via ? techViaName(design, via.tech_via) : designViaName(design, via.design_via));
    }
    key.unsignedValue(rule.via_rules.size());
    for (const auto via_rule : rule.via_rules) {
      key.text(viaRuleName(design, via_rule));
    }
    key.unsignedValue(rule.min_cuts.size());
    for (const auto& min_cuts : rule.min_cuts) {
      key.text(layerName(design, min_cuts.layer));
      key.unsignedValue(min_cuts.cut_count);
    }
    key.unsignedValue(rule.properties.size());
    for (const auto& property : rule.properties) {
      key.text(property.name);
      key.text(property.value);
      key.unsignedValue(static_cast<uint8_t>(property.type));
    }
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline bool pointLess(Point lhs, Point rhs)
{
  return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y < rhs.y);
}

inline void appendWirePathSemantics(SemanticKey& key, const DesignStore& design, const DesignWire& wire, DesignWirePathView path,
                                    bool ignore_unrepresented_mask = false)
{
  const bool drop_mask = ignore_unrepresented_mask && path.width() == 0 && path.mask() == 1u;
  key.unsignedValue(static_cast<uint8_t>(wire.status));
  key.text(wire.shield_net);
  key.text(layerName(design, path.layer()));
  key.unsignedValue(drop_mask ? path.flags() & ~DesignWirePathFlag::kHasMask : path.flags());
  key.signedValue(path.width());
  key.unsignedValue(drop_mask ? 0u : path.mask());
  key.text(path.taperRule());
  key.text(path.shape());
  key.signedValue(path.style());
}

inline void appendWireSemantics(SemanticKey& key, const DesignWire& wire)
{
  key.unsignedValue(static_cast<uint8_t>(wire.status));
  key.text(wire.shield_net);
}

inline std::vector<std::string> wirePrimitiveKeys(const DesignStore& design, DesignNetId id, bool ignore_unrepresented_mask = false)
{
  std::vector<std::string> result;
  for (const auto wire_id : design.routingStorage().wires(id)) {
    const auto& wire = design.routingStorage().wire(wire_id);
    for (std::size_t path_index = 0; path_index < design.routingStorage().pathCount(wire_id); ++path_index) {
      const auto path = design.routingStorage().path(wire_id, path_index);
      for (std::size_t point_index = 1; point_index < path.points().size(); ++point_index) {
        auto first = path.points()[point_index - 1u];
        auto second = path.points()[point_index];
        if (pointLess(second.position, first.position)) {
          std::swap(first, second);
        }

        SemanticKey key;
        key.text("segment");
        appendWirePathSemantics(key, design, wire, path, ignore_unrepresented_mask);
        key.point(first.position);
        key.unsignedValue(first.flags);
        key.signedValue(first.extension);
        key.point(second.position);
        key.unsignedValue(second.flags);
        key.signedValue(second.extension);
        result.push_back(key.str());
      }

      if (path.points().size() == 1u && path.vias().empty() && path.rectangles().empty()) {
        SemanticKey key;
        key.text("point");
        appendWirePathSemantics(key, design, wire, path, ignore_unrepresented_mask);
        key.point(path.points().front().position);
        key.unsignedValue(path.points().front().flags);
        key.signedValue(path.points().front().extension);
        result.push_back(key.str());
      }

      for (const auto& via : path.vias()) {
        SemanticKey key;
        key.text("via");
        appendWireSemantics(key, wire);
        const auto& anchor = path.points()[via.point_index];
        key.point(anchor.position);
        key.unsignedValue(anchor.flags);
        key.signedValue(anchor.extension);
        key.text(viaName(design, via.tech_via, via.design_via));
        key.unsignedValue(static_cast<uint8_t>(via.orientation));
        key.unsignedValue(via.flags);
        key.unsignedValue(via.top_mask);
        key.unsignedValue(via.cut_mask);
        key.unsignedValue(via.bottom_mask);
        key.unsignedValue(via.rows);
        key.unsignedValue(via.columns);
        key.signedValue(via.step_x);
        key.signedValue(via.step_y);
        result.push_back(key.str());
      }

      for (const auto& rectangle : path.rectangles()) {
        SemanticKey key;
        key.text("rectangle");
        appendWirePathSemantics(key, design, wire, path, ignore_unrepresented_mask);
        const auto& anchor = path.points()[rectangle.point_index];
        key.unsignedValue(anchor.flags);
        key.signedValue(anchor.extension);
        key.rectangle({.ll_x = anchor.position.x + rectangle.delta.ll_x,
                       .ll_y = anchor.position.y + rectangle.delta.ll_y,
                       .ur_x = anchor.position.x + rectangle.delta.ur_x,
                       .ur_y = anchor.position.y + rectangle.delta.ur_y});
        result.push_back(key.str());
      }
    }
  }
  sortKeys(result);
  return result;
}

inline std::vector<std::string> netGeometryKeys(const DesignStore& design, DesignNetId id)
{
  const auto* geometry = design.routingStorage().netGeometry(id);
  if (geometry == nullptr) {
    return {};
  }

  std::vector<std::string> result;
  result.reserve(geometry->rectangles.size() + geometry->polygons.size() + geometry->vias.size());
  for (const auto& rectangle : geometry->rectangles) {
    SemanticKey key;
    key.text("rectangle");
    key.text(layerName(design, rectangle.layer));
    key.rectangle(rectangle.rectangle);
    key.unsignedValue(static_cast<uint8_t>(rectangle.route_status));
    key.unsignedValue(rectangle.flags);
    key.unsignedValue(rectangle.mask);
    key.text(rectangle.shield_net);
    key.text(rectangle.shape);
    result.push_back(key.str());
  }
  for (const auto& polygon : geometry->polygons) {
    SemanticKey key;
    key.text("polygon");
    key.text(layerName(design, polygon.layer));
    key.points(polygon.points);
    key.unsignedValue(static_cast<uint8_t>(polygon.route_status));
    key.unsignedValue(polygon.flags);
    key.unsignedValue(polygon.mask);
    key.text(polygon.shield_net);
    key.text(polygon.shape);
    result.push_back(key.str());
  }
  for (const auto& via : geometry->vias) {
    for (const auto origin : via.origins) {
      SemanticKey key;
      key.text("via");
      key.text(viaName(design, via.tech_via, via.design_via));
      key.unsignedValue(static_cast<uint8_t>(via.orientation));
      key.unsignedValue(static_cast<uint8_t>(via.route_status));
      key.unsignedValue(via.flags);
      key.unsignedValue(via.top_mask);
      key.unsignedValue(via.cut_mask);
      key.unsignedValue(via.bottom_mask);
      key.text(via.shield_net);
      key.text(via.shape);
      key.point(origin);
      result.push_back(key.str());
    }
  }
  sortKeys(result);
  return result;
}

inline void appendNetOptions(SemanticKey& key, const DesignStore& design, DesignNetId id)
{
  const auto* options = design.netlistStorage().netOptions(id);
  key.unsignedValue(options != nullptr);
  if (options == nullptr) {
    return;
  }
  key.unsignedValue(options->flags);
  key.text(options->original);
  key.unsignedValue(static_cast<uint8_t>(options->pattern));
  key.realValue(options->estimated_capacitance);
  key.realValue(options->frequency);
  key.signedValue(options->xtalk);
  key.signedValue(options->style);
  key.signedValue(options->voltage);
  std::vector<std::string> spacing_rules;
  spacing_rules.reserve(options->spacing_rules.size());
  for (const auto& spacing : options->spacing_rules) {
    SemanticKey spacing_key;
    spacing_key.text(layerName(design, spacing.layer));
    spacing_key.signedValue(spacing.spacing);
    spacing_key.unsignedValue(spacing.flags);
    spacing_key.signedValue(spacing.range_left);
    spacing_key.signedValue(spacing.range_right);
    spacing_rules.push_back(spacing_key.str());
  }
  appendKeys(key, std::move(spacing_rules));
}

inline bool isEmptyRegularAliasOfSpecialNet(const DesignStore& design, DesignNetId id)
{
  if (design.netlistStorage().isSpecialNet(id)) {
    return false;
  }
  const auto& net = design.netlistStorage().net(id);
  if (!design.netlistStorage().findSpecialNet(net.name)) {
    return false;
  }
  return net.use == DesignSignalUse::kNone && net.source == DesignNetSource::kNone && net.flags == 0u && net.weight == 0
         && !net.non_default_rule && !net.design_non_default_rule && design.netlistStorage().netOptions(id) == nullptr
         && design.netlistStorage().instancePins(id).empty() && design.netlistStorage().ioPins(id).empty()
         && design.routingStorage().wires(id).empty() && design.routingStorage().netGeometry(id) == nullptr;
}

inline std::size_t semanticNetCount(const DesignStore& design)
{
  std::size_t result = 0;
  for (const auto id : design.netlistStorage().nets()) {
    result += !isEmptyRegularAliasOfSpecialNet(design, id);
  }
  return result;
}

inline std::vector<std::string> netKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.netlistStorage().nets()) {
    if (isEmptyRegularAliasOfSpecialNet(design, id)) {
      continue;
    }
    const auto& net = design.netlistStorage().net(id);
    SemanticKey key;
    key.text(netName(design, id));
    const auto semantic_use = net.use == DesignSignalUse::kNone ? DesignSignalUse::kSignal : net.use;
    key.unsignedValue(static_cast<uint8_t>(semantic_use));
    key.unsignedValue(static_cast<uint8_t>(net.source));
    key.unsignedValue(net.flags);
    key.signedValue(net.weight);
    key.text(net.design_non_default_rule ? ndrName(design, net.design_non_default_rule) : ndrName(design, net.non_default_rule));
    appendNetOptions(key, design, id);

    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline std::vector<std::string> regionKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.constraintStorage().regions()) {
    const auto& region = design.constraintStorage().region(id);
    SemanticKey key;
    key.text(region.name);
    key.unsignedValue(static_cast<uint8_t>(region.type));
    appendKeys(key, rectangleKeys(region.rectangles));
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline std::vector<std::string> groupKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.constraintStorage().groups()) {
    const auto& group = design.constraintStorage().group(id);
    SemanticKey key;
    key.text(group.name);
    key.unsignedValue(group.flags);
    key.text(regionName(design, group.region));
    std::vector<std::string> instances;
    instances.reserve(group.instances.size());
    for (const auto instance : group.instances) {
      instances.push_back(instanceName(design, instance));
    }
    appendKeys(key, std::move(instances));
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline std::string polygonKey(const std::vector<Point>& polygon)
{
  SemanticKey key;
  key.points(polygon);
  return key.str();
}

inline std::vector<std::string> blockageKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.constraintStorage().blockages()) {
    const auto& blockage = design.constraintStorage().blockage(id);
    SemanticKey key;
    key.unsignedValue(static_cast<uint8_t>(blockage.kind));
    key.unsignedValue(blockage.flags);
    key.text(layerName(design, blockage.layer));
    key.text(instanceName(design, blockage.component));
    key.realValue(blockage.partial);
    key.signedValue(blockage.spacing);
    key.signedValue(blockage.design_rule_width);
    key.unsignedValue(blockage.mask);
    appendKeys(key, rectangleKeys(blockage.rectangles));
    std::vector<std::string> polygons;
    polygons.reserve(blockage.polygons.size());
    for (const auto& polygon : blockage.polygons) {
      polygons.push_back(polygonKey(polygon));
    }
    appendKeys(key, std::move(polygons));
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

inline std::vector<std::string> fillKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.fillStorage().fills()) {
    const auto& fill = design.fillStorage().fill(id);
    SemanticKey key;
    key.text(layerName(design, fill.layer));
    key.unsignedValue(fill.flags);
    key.unsignedValue(fill.mask);
    appendKeys(key, rectangleKeys(fill.rectangles));
    result.push_back(key.str());
  }
  sortKeys(result);
  return result;
}

}  // namespace detail

inline DesignSemanticSnapshot makeDesignSemanticSnapshot(const DesignStore& design, bool include_net_keys = true,
                                                         bool default_unspecified_pin_use_to_signal = false)
{
  return DesignSemanticSnapshot{
      .component_counts
      = {detail::componentCount<DesignRow>(design), detail::componentCount<DesignTrackGrid>(design),
         detail::componentCount<DesignGCellGrid>(design), detail::componentCount<DesignInstance>(design),
         detail::componentCount<DesignInstancePin>(design), detail::componentCount<DesignIoPin>(design), detail::semanticNetCount(design),
         detail::componentCount<DesignWire>(design), detail::componentCount<DesignVia>(design),
         detail::componentCount<DesignNonDefaultRule>(design), detail::componentCount<DesignRegion>(design),
         detail::componentCount<DesignGroup>(design), detail::componentCount<DesignBlockage>(design),
         detail::componentCount<DesignFill>(design), detail::componentCount<DesignSpecialNet>(design)},
      .global = detail::globalKey(design),
      .rows = detail::rowKeys(design),
      .track_grids = detail::trackGridKeys(design),
      .gcell_grids = detail::gcellGridKeys(design),
      .instances = detail::instanceKeys(design),
      .instance_pins = detail::instancePinKeys(design),
      .io_pins = detail::ioPinKeys(design, default_unspecified_pin_use_to_signal),
      .vias = detail::viaKeys(design),
      .non_default_rules = detail::nonDefaultRuleKeys(design),
      .nets = include_net_keys ? detail::netKeys(design) : std::vector<std::string>{},
      .regions = detail::regionKeys(design),
      .groups = detail::groupKeys(design),
      .blockages = detail::blockageKeys(design),
      .fills = detail::fillKeys(design)};
}

}  // namespace eccdb::test
