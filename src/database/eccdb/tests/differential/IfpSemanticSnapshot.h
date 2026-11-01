// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "DesignSemanticSnapshot.h"
#include "StructuredDesignSemanticDiffer.h"
#include "geometry/PhysicalDesignService.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb::test {

// This is deliberately narrower than DesignSemanticSnapshot.  It describes
// the physical, DEF-visible state that an iFP provider must preserve after
// importing, running iFP, and writing back.  It intentionally omits entity
// IDs, DEF statement order, and DesignWire grouping, all of which are storage
// representation details rather than placement semantics.
struct IfpSemanticSnapshot
{
  // A false value is fail-closed: the caller must not treat two snapshots as
  // semantically equal merely because they contain the same unsupported item.
  bool complete = true;
  std::vector<std::string> unsupported;
  std::string global;
  std::string core;
  std::vector<std::string> rows;
  std::vector<std::string> track_grids;
  std::vector<std::string> gcell_grids;
  std::vector<std::string> instances;
  std::vector<std::string> instance_pins;
  // One key per dbBTerm.  Its PORT/dbBPin children remain nested keys so a
  // split, merge, or duplicate PORT is observable even though PORT order is
  // not semantic.
  std::vector<std::string> io_pins;
  // Fixed DEF VIA definitions retain their identity and physical primitives.
  // Generated definitions are intentionally absent unless referenced; their
  // geometric fingerprint is substituted at each route/PORT use site.
  std::vector<std::string> vias;
  // Each net key includes connectivity, USE and all route/standalone
  // primitives after canonicalising route grouping and endpoint direction.
  std::vector<std::string> nets;

  bool operator==(const IfpSemanticSnapshot&) const = default;
};

namespace ifp_semantic_detail {

inline std::vector<Point> canonicalAreaBoundary(std::span<const Point> boundary)
{
  std::vector<Point> points(boundary.begin(), boundary.end());
  if (points.size() == 2u) {
    const Rect rectangle{points[0].x, points[0].y, points[1].x, points[1].y};
    const auto normalized = rectangle.normalized();
    points = {{normalized.ll_x, normalized.ll_y}, {normalized.ur_x, normalized.ll_y},
              {normalized.ur_x, normalized.ur_y}, {normalized.ll_x, normalized.ur_y}};
  }
  return structured::canonicalPolygon(std::move(points));
}

inline void appendCanonicalBoundary(detail::SemanticKey& key, bool present, std::span<const Point> boundary)
{
  key.unsignedValue(present ? 1u : 0u);
  if (!present) {
    return;
  }
  key.points(canonicalAreaBoundary(boundary));
}

inline std::string globalKey(const PhysicalDesignService& physical)
{
  detail::SemanticKey key;
  key.unsignedValue(physical.hasDesignInfo() ? 1u : 0u);
  if (physical.hasDesignInfo()) {
    const auto& info = physical.designInfo();
    key.text(info.name);
    key.signedValue(info.database_units_per_micron);
    key.text(info.divider_character);
    key.text(info.bus_bit_characters);
  }
  const auto manufacturing_grid = physical.manufacturingGrid();
  key.unsignedValue(manufacturing_grid.has_value() ? 1u : 0u);
  if (manufacturing_grid.has_value()) {
    key.signedValue(*manufacturing_grid);
  }
  appendCanonicalBoundary(key, physical.hasDieArea(), physical.hasDieArea() ? physical.dieBoundary() : std::span<const Point>{});
  return key.str();
}

inline std::string coreKey(const DesignStore& design)
{
  detail::SemanticKey key;
  const bool present = design.floorplanStorage().hasCoreArea();
  appendCanonicalBoundary(key, present, present ? design.floorplanStorage().coreBoundary() : std::span<const Point>{});
  return key.str();
}

struct ViaReferences;
inline std::string generatedViaFingerprint(ViaReferences& references, DesignViaId id, const DesignVia& via);

struct ViaReferences
{
  const DesignStore& design;
  const PhysicalDesignService& physical;
  std::unordered_map<std::string, std::string> generated_aliases;
  std::vector<std::string> unsupported;

  void fail(std::string message)
  {
    unsupported.push_back(std::move(message));
  }

  [[nodiscard]] std::string reference(TechViaMasterId tech_via, DesignViaId design_via)
  {
    if (tech_via && design_via) {
      fail("via reference sets both a technology and a design definition");
      return "unsupported:multiple-via-definitions";
    }
    if (tech_via) {
      return detail::viaName(design, tech_via, {});
    }
    if (!design_via) {
      fail("via reference has no definition");
      return "unsupported:missing-via-definition";
    }
    try {
      const auto& via = design.routingStorage().via(design_via);
      const auto raw = std::string{"design:"} + via.name;
      if ((via.flags & DesignViaFlag::kGenerated) == 0u) {
        return raw;
      }
      if (const auto found = generated_aliases.find(raw); found != generated_aliases.end()) {
        return found->second;
      }
      const auto fingerprint = "generated:" + generatedViaFingerprint(*this, design_via, via);
      generated_aliases.emplace(raw, fingerprint);
      return fingerprint;
    } catch (const std::exception& error) {
      fail(std::string{"invalid design via reference: "} + error.what());
      return "unsupported:invalid-design-via";
    }
  }
};

inline std::string physicalRectKey(const DesignStore& design, const PhysicalLayerRect& rectangle)
{
  detail::SemanticKey key;
  key.text("rectangle");
  key.text(detail::layerName(design, rectangle.layer));
  key.rectangle(structured::canonicalRect(rectangle.rectangle));
  key.unsignedValue(rectangle.flags);
  key.unsignedValue(rectangle.mask);
  key.signedValue(rectangle.spacing);
  key.signedValue(rectangle.design_rule_width);
  return key.str();
}

inline std::string physicalPolygonKey(const DesignStore& design, const PhysicalLayerPolygon& polygon)
{
  detail::SemanticKey key;
  key.text("polygon");
  key.text(detail::layerName(design, polygon.layer));
  key.points(structured::canonicalPolygon(std::vector<Point>{polygon.points.begin(), polygon.points.end()}));
  key.unsignedValue(polygon.flags);
  key.unsignedValue(polygon.mask);
  key.signedValue(polygon.spacing);
  key.signedValue(polygon.design_rule_width);
  return key.str();
}

inline std::string generatedViaFingerprint(ViaReferences& references, DesignViaId id, const DesignVia& via)
{
  detail::SemanticKey key;
  key.text("generated-design-via");
  // Do not use the generated VIA or VIARULE name here.  Tools are free to
  // choose a deterministic local alias for an equivalent generated VIA.
  const auto& generated = via.generated;
  // The flags only record whether DEF spelt a default-valued field.  The
  // effective values and expanded geometry below are the semantic contract.
  // In particular, `+ ROWCOL 1 1`, omitted ROWCOL, and a zero-valued omitted
  // ORIGIN/OFFSET must not produce a false difference.
  if (!generated.via_rule || !generated.bottom_layer || !generated.cut_layer || !generated.top_layer) {
    references.fail("generated design via " + via.name + " has an incomplete rule/layer definition");
  }
  if (generated.row_count == 0u || generated.column_count == 0u) {
    references.fail("generated design via " + via.name + " has an empty cut array");
  }
  key.text(detail::layerName(references.design, generated.bottom_layer));
  key.text(detail::layerName(references.design, generated.cut_layer));
  key.text(detail::layerName(references.design, generated.top_layer));
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
  key.point((generated.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u ? generated.origin : Point{});
  key.point((generated.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? generated.bottom_offset : Point{});
  key.point((generated.flags & DesignGeneratedViaFlag::kHasOffset) != 0u ? generated.top_offset : Point{});
  if ((generated.flags & DesignGeneratedViaFlag::kHasCutPattern) != 0u && !generated.cut_pattern.empty()) {
    // The physical expander intentionally has no cut-pattern implementation.
    // Treat a real pattern as unsupported rather than claiming a purely box
    // expansion proves equivalence.
    references.fail("generated design via " + via.name + " uses unsupported cut pattern: " + generated.cut_pattern);
  }
  key.text((generated.flags & DesignGeneratedViaFlag::kHasCutPattern) != 0u ? generated.cut_pattern : std::string{});

  std::vector<std::string> primitives;
  bool saw_nested_via = false;
  try {
    references.physical.forEachDesignViaGeometry(
        id, PhysicalGeometryVisitor{.rectangle = [&](const PhysicalLayerRect& rectangle) {
                                       primitives.push_back(physicalRectKey(references.design, rectangle));
                                     },
                                     .polygon = [&](const PhysicalLayerPolygon& polygon) {
                                       primitives.push_back(physicalPolygonKey(references.design, polygon));
                                     },
                                     .via = [&](const PhysicalViaPlacement&) { saw_nested_via = true; }});
  } catch (const std::exception& error) {
    references.fail("cannot expand generated design via " + via.name + ": " + error.what());
  }
  if (saw_nested_via) {
    references.fail("generated design via " + via.name + " expands to an unsupported nested via");
  }
  if (primitives.empty()) {
    references.fail("generated design via " + via.name + " has no expanded physical geometry");
  }
  detail::appendKeys(key, std::move(primitives));
  return key.str();
}

inline std::string staticViaKey(const DesignStore& design, const DesignVia& via)
{
  detail::SemanticKey key;
  key.text(via.name);
  key.unsignedValue(via.flags);
  key.text(via.pattern_name);
  std::vector<std::string> primitives;
  const auto& geometry = design.geometryRegistry();
  primitives.reserve(via.rectangles.size() + via.polygons.size());
  for (const auto id : via.rectangles) {
    const auto& rectangle = geometry.get<const DesignViaRectangle>(id);
    detail::SemanticKey primitive;
    primitive.text("rectangle");
    primitive.text(detail::layerName(design, rectangle.layer));
    primitive.rectangle(structured::canonicalRect(rectangle.rectangle));
    primitive.unsignedValue(rectangle.mask);
    primitives.push_back(primitive.str());
  }
  for (const auto id : via.polygons) {
    const auto& polygon = geometry.get<const DesignViaPolygon>(id);
    detail::SemanticKey primitive;
    primitive.text("polygon");
    primitive.text(detail::layerName(design, polygon.layer));
    primitive.points(structured::canonicalPolygon(polygon.points));
    primitive.unsignedValue(polygon.mask);
    primitives.push_back(primitive.str());
  }
  detail::appendKeys(key, std::move(primitives));
  return key.str();
}

inline std::vector<std::string> staticViaKeys(const DesignStore& design)
{
  std::vector<std::string> result;
  for (const auto id : design.routingStorage().vias()) {
    const auto& via = design.routingStorage().via(id);
    if ((via.flags & DesignViaFlag::kGenerated) == 0u) {
      result.push_back(staticViaKey(design, via));
    }
  }
  detail::sortKeys(result);
  return result;
}

inline std::string pinRectangleKey(const DesignStore& design, const DesignPinRectangle& rectangle, const DesignBlockPin& pin)
{
  detail::SemanticKey key;
  key.text("rectangle");
  key.text(detail::layerName(design, rectangle.layer));
  const bool has_placement = (pin.flags & DesignIoPinPortFlag::kHasPlacement) != 0u;
  const auto orientation = has_placement ? pin.orientation : DesignOrientation::kN;
  const Point origin = has_placement ? pin.origin : Point{};
  key.rectangle(structured::canonicalRect(detail::transformRectangle(rectangle.rectangle, orientation, origin)));
  key.unsignedValue(rectangle.flags);
  key.unsignedValue(rectangle.mask);
  key.signedValue(rectangle.spacing);
  key.signedValue(rectangle.design_rule_width);
  return key.str();
}

inline std::string pinPolygonKey(const DesignStore& design, const DesignPinPolygon& polygon, const DesignBlockPin& pin)
{
  detail::SemanticKey key;
  key.text("polygon");
  key.text(detail::layerName(design, polygon.layer));
  std::vector<Point> points;
  points.reserve(polygon.points.size());
  const bool has_placement = (pin.flags & DesignIoPinPortFlag::kHasPlacement) != 0u;
  const auto orientation = has_placement ? pin.orientation : DesignOrientation::kN;
  const Point origin = has_placement ? pin.origin : Point{};
  for (const auto point : polygon.points) {
    points.push_back(detail::transformPoint(point, orientation, origin));
  }
  key.points(structured::canonicalPolygon(std::move(points)));
  key.unsignedValue(polygon.flags);
  key.unsignedValue(polygon.mask);
  key.signedValue(polygon.spacing);
  key.signedValue(polygon.design_rule_width);
  return key.str();
}

inline std::string pinViaKey(const DesignStore& design, ViaReferences& references, const DesignPinVia& via, const DesignBlockPin& pin)
{
  detail::SemanticKey key;
  key.text("via");
  key.text(references.reference(via.tech_via, via.design_via));
  const bool has_placement = (pin.flags & DesignIoPinPortFlag::kHasPlacement) != 0u;
  const Point origin = has_placement ? detail::transformPoint(via.origin, pin.orientation, pin.origin) : via.origin;
  const auto orientation = has_placement ? pin.orientation : DesignOrientation::kN;
  key.point(origin);
  key.unsignedValue(via.flags);
  key.unsignedValue(via.top_mask);
  key.unsignedValue(via.cut_mask);
  key.unsignedValue(via.bottom_mask);

  // A PORT has no separate VIA orientation field: its placement orientation
  // rotates the contained VIA.  Comparing just the name and transformed
  // origin therefore misses a non-symmetric VIA rotated in place.  Expand
  // the actual metal/cut primitives at the world placement, not a BBox.
  std::vector<std::string> primitives;
  try {
    const PhysicalGeometryVisitor visitor{
        .rectangle = [&](const PhysicalLayerRect& rectangle) { primitives.push_back(physicalRectKey(design, rectangle)); },
        .polygon = [&](const PhysicalLayerPolygon& polygon) { primitives.push_back(physicalPolygonKey(design, polygon)); }};
    if (via.tech_via) {
      references.physical.forEachTechViaGeometry(via.tech_via, origin, orientation, visitor);
    } else if (via.design_via) {
      references.physical.forEachDesignViaGeometry(via.design_via, origin, orientation, visitor);
    }
  } catch (const std::exception& error) {
    references.fail(std::string{"cannot expand IO PORT via: "} + error.what());
  }
  if (primitives.empty()) {
    references.fail("IO PORT via has no expanded physical geometry");
  }
  detail::appendKeys(key, std::move(primitives));
  return key.str();
}

inline std::string blockPinKey(const DesignStore& design, ViaReferences& references, const DesignBlockPin& pin)
{
  detail::SemanticKey key;
  // `+ PORT` is syntax, not a separate physical connection class.  The
  // nested primitive list keeps its strong-connectivity group intact.
  key.unsignedValue(pin.flags & ~DesignIoPinPortFlag::kExplicit);
  key.unsignedValue(static_cast<uint8_t>(pin.placement_status));
  std::vector<std::string> primitives;
  primitives.reserve(pin.rectangles.size() + pin.polygons.size() + pin.vias.size());
  const auto& storage = design.netlistStorage();
  for (const auto id : pin.rectangles) {
    primitives.push_back(pinRectangleKey(design, storage.blockPinRectangle(id), pin));
  }
  for (const auto id : pin.polygons) {
    primitives.push_back(pinPolygonKey(design, storage.blockPinPolygon(id), pin));
  }
  for (const auto id : pin.vias) {
    primitives.push_back(pinViaKey(design, references, storage.blockPinVia(id), pin));
  }
  if (primitives.empty() && (pin.flags & DesignIoPinPortFlag::kHasPlacement) != 0u) {
    detail::SemanticKey location;
    location.text("location");
    location.point(pin.origin);
    location.unsignedValue(static_cast<uint8_t>(pin.orientation));
    primitives.push_back(location.str());
  }
  detail::appendKeys(key, std::move(primitives));
  return key.str();
}

inline std::vector<std::string> ioPinKeys(const DesignStore& design, ViaReferences& references)
{
  std::vector<std::string> result;
  for (const auto id : design.netlistStorage().ioPins()) {
    const auto& pin = design.netlistStorage().ioPin(id);
    detail::SemanticKey key;
    key.text(pin.name);
    key.text(detail::netName(design, pin.net));
    key.text(detail::netName(design, pin.special_net));
    key.unsignedValue(static_cast<uint8_t>(pin.direction));
    key.unsignedValue(static_cast<uint8_t>(canonicalSignalUse(pin.use)));
    key.unsignedValue(pin.flags & ~DesignIoPinFlag::kSpecial);
    std::vector<std::string> ports;
    ports.reserve(pin.pins.size());
    for (const auto port : pin.pins) {
      ports.push_back(blockPinKey(design, references, design.netlistStorage().blockPin(port)));
    }
    detail::appendKeys(key, std::move(ports));
    result.push_back(key.str());
  }
  detail::sortKeys(result);
  return result;
}

template <typename Value, typename Append>
inline void appendCanonicalValues(detail::SemanticKey& key, const std::vector<Value>& values, Append&& append)
{
  std::vector<std::string> keys;
  keys.reserve(values.size());
  for (const auto& value : values) {
    detail::SemanticKey child;
    append(child, value);
    keys.push_back(child.str());
  }
  detail::appendKeys(key, std::move(keys));
}

inline void appendCanonicalNetOptions(detail::SemanticKey& key, const structured::CanonicalNetOptions& options)
{
  key.unsignedValue(options.present ? 1u : 0u);
  if (!options.present) {
    return;
  }
  key.unsignedValue(options.flags);
  key.text(options.original);
  key.unsignedValue(static_cast<uint8_t>(options.pattern));
  key.realValue(options.estimated_capacitance);
  key.realValue(options.frequency);
  key.signedValue(options.xtalk);
  key.signedValue(options.style);
  key.signedValue(options.voltage);
  appendCanonicalValues(key, options.spacing_rules, [](detail::SemanticKey& item, const structured::CanonicalSpacing& spacing) {
    item.text(spacing.layer);
    item.signedValue(spacing.spacing);
    item.unsignedValue(spacing.flags);
    item.signedValue(spacing.range_left);
    item.signedValue(spacing.range_right);
  });
}

inline std::string canonicalViaReference(ViaReferences& references, std::string_view reference)
{
  constexpr std::string_view design_prefix{"design:"};
  if (reference.empty()) {
    references.fail("route via reference is empty");
    return "unsupported:empty-via-reference";
  }
  if (!reference.starts_with(design_prefix)) {
    return std::string{reference};
  }
  const auto name = reference.substr(design_prefix.size());
  if (name.empty()) {
    references.fail("design via reference has no name");
    return "unsupported:unnamed-design-via";
  }
  const auto id = references.design.routingStorage().findVia(name);
  if (!id) {
    references.fail("design via reference cannot be resolved: " + std::string{name});
    return "unsupported:missing-design-via:" + std::string{name};
  }
  return references.reference({}, id);
}

inline std::string netKey(const DesignStore& design, ViaReferences& references, DesignNetId id)
{
  auto net = structured::canonicalNet(design, id);
  for (auto& via : net.wire_vias) {
    via.name = canonicalViaReference(references, via.name);
  }
  for (auto& via : net.net_vias) {
    via.name = canonicalViaReference(references, via.name);
  }
  structured::canonicalSort(net.wire_vias, std::less{});
  structured::canonicalSort(net.net_vias, std::less{});

  detail::SemanticKey key;
  key.text(net.name);
  key.unsignedValue(net.special ? 1u : 0u);
  key.unsignedValue(static_cast<uint8_t>(net.use));
  key.unsignedValue(static_cast<uint8_t>(net.source));
  key.unsignedValue(net.flags);
  key.signedValue(net.weight);
  key.text(net.non_default_rule);
  appendCanonicalNetOptions(key, net.options);
  appendCanonicalValues(key, net.pins, [](detail::SemanticKey& item, const structured::CanonicalPin& pin) {
    item.text(pin.instance);
    item.text(pin.master_term);
    item.text(pin.net);
    item.text(pin.special_net);
  });
  appendCanonicalValues(key, net.segments, [](detail::SemanticKey& item, const structured::CanonicalSegment& segment) {
    item.text(segment.layer);
    item.unsignedValue(static_cast<uint8_t>(segment.status));
    item.text(segment.shield_net);
    item.unsignedValue(segment.path_flags);
    item.signedValue(segment.width);
    item.unsignedValue(segment.mask);
    item.text(segment.taper_rule);
    item.text(segment.shape);
    item.signedValue(segment.style);
    item.point(segment.first);
    item.unsignedValue(segment.first_flags);
    item.signedValue(segment.first_extension);
    item.point(segment.second);
    item.unsignedValue(segment.second_flags);
    item.signedValue(segment.second_extension);
  });
  appendCanonicalValues(key, net.points, [](detail::SemanticKey& item, const structured::CanonicalPoint& point) {
    item.text(point.layer);
    item.unsignedValue(static_cast<uint8_t>(point.status));
    item.text(point.shield_net);
    item.unsignedValue(point.path_flags);
    item.signedValue(point.width);
    item.unsignedValue(point.mask);
    item.text(point.taper_rule);
    item.text(point.shape);
    item.signedValue(point.style);
    item.point(point.position);
    item.unsignedValue(point.flags);
    item.signedValue(point.extension);
  });
  appendCanonicalValues(key, net.wire_vias, [](detail::SemanticKey& item, const structured::CanonicalWireVia& via) {
    item.unsignedValue(static_cast<uint8_t>(via.status));
    item.text(via.shield_net);
    item.point(via.anchor);
    item.unsignedValue(via.anchor_flags);
    item.signedValue(via.anchor_extension);
    item.text(via.name);
    item.unsignedValue(static_cast<uint8_t>(via.orientation));
    item.unsignedValue(via.flags);
    item.unsignedValue(via.top_mask);
    item.unsignedValue(via.cut_mask);
    item.unsignedValue(via.bottom_mask);
    item.unsignedValue(via.rows);
    item.unsignedValue(via.columns);
    item.signedValue(via.step_x);
    item.signedValue(via.step_y);
  });
  appendCanonicalValues(key, net.wire_rectangles, [](detail::SemanticKey& item, const structured::CanonicalWireRectangle& rectangle) {
    item.text(rectangle.layer);
    item.unsignedValue(static_cast<uint8_t>(rectangle.status));
    item.text(rectangle.shield_net);
    item.unsignedValue(rectangle.path_flags);
    item.signedValue(rectangle.width);
    item.unsignedValue(rectangle.mask);
    item.text(rectangle.taper_rule);
    item.text(rectangle.shape);
    item.signedValue(rectangle.style);
    item.rectangle(rectangle.rectangle);
  });
  appendCanonicalValues(key, net.rectangles, [](detail::SemanticKey& item, const structured::CanonicalNetRectangle& rectangle) {
    item.text(rectangle.layer);
    item.rectangle(rectangle.rectangle);
    item.unsignedValue(static_cast<uint8_t>(rectangle.status));
    item.unsignedValue(rectangle.flags);
    item.unsignedValue(rectangle.mask);
    item.text(rectangle.shield_net);
    item.text(rectangle.shape);
  });
  appendCanonicalValues(key, net.polygons, [](detail::SemanticKey& item, const structured::CanonicalNetPolygon& polygon) {
    item.text(polygon.layer);
    item.points(polygon.points);
    item.unsignedValue(static_cast<uint8_t>(polygon.status));
    item.unsignedValue(polygon.flags);
    item.unsignedValue(polygon.mask);
    item.text(polygon.shield_net);
    item.text(polygon.shape);
  });
  appendCanonicalValues(key, net.net_vias, [](detail::SemanticKey& item, const structured::CanonicalNetVia& via) {
    item.text(via.name);
    item.points(via.origins);
    item.unsignedValue(static_cast<uint8_t>(via.orientation));
    item.unsignedValue(static_cast<uint8_t>(via.status));
    item.unsignedValue(via.flags);
    item.unsignedValue(via.top_mask);
    item.unsignedValue(via.cut_mask);
    item.unsignedValue(via.bottom_mask);
    item.text(via.shield_net);
    item.text(via.shape);
  });
  return key.str();
}

inline std::vector<std::string> netKeys(const DesignStore& design, ViaReferences& references)
{
  std::vector<std::string> result;
  for (const auto id : design.netlistStorage().nets()) {
    if (!isSemanticallyComparableNet(design, id)) {
      continue;
    }
    try {
      result.push_back(netKey(design, references, id));
    } catch (const std::exception& error) {
      // CanonicalNet deliberately assumes storage-validated references on its
      // fast path.  A differential test must not turn a broken route/VIA
      // relation into a false pass, so retain a visible marker and make the
      // snapshot incomplete rather than propagating an opaque exception.
      const auto& net = design.netlistStorage().net(id);
      references.fail("cannot canonicalize net " + net.name + ": " + error.what());
      detail::SemanticKey key;
      key.text("unsupported-net");
      key.text(net.name);
      result.push_back(key.str());
    }
  }
  detail::sortKeys(result);
  return result;
}

}  // namespace ifp_semantic_detail

inline IfpSemanticSnapshot makeIfpSemanticSnapshot(const TechStore& technology, const LibraryStore& library, const DesignStore& design)
{
  PhysicalDesignService physical{technology, library, design};
  ifp_semantic_detail::ViaReferences references{.design = design, .physical = physical};

  IfpSemanticSnapshot result{
      .global = ifp_semantic_detail::globalKey(physical),
      .core = ifp_semantic_detail::coreKey(design),
      .rows = detail::rowKeys(design),
      .track_grids = detail::trackGridKeys(design),
      .gcell_grids = detail::gcellGridKeys(design),
      .instances = detail::instanceKeys(design),
      .instance_pins = detail::instancePinKeys(design),
      .io_pins = ifp_semantic_detail::ioPinKeys(design, references),
      .vias = ifp_semantic_detail::staticViaKeys(design),
      .nets = ifp_semantic_detail::netKeys(design, references)};
  detail::sortKeys(references.unsupported);
  references.unsupported.erase(std::unique(references.unsupported.begin(), references.unsupported.end()), references.unsupported.end());
  result.unsupported = std::move(references.unsupported);
  result.complete = result.unsupported.empty();
  return result;
}

}  // namespace eccdb::test
