// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "MetalFillEccdb.h"

#include <algorithm>
#include <any>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>
#include <utility>
#include <vector>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/index/predicates.hpp>
#include <boost/geometry/index/rtree.hpp>

#include "metal/MetalFillKernel.hpp"
#include "context/DatabaseContext.h"
#include "design/fill/model/FillComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "geometry/GeometryEntity.h"
#include "service/geometry/GeometryEditService.h"
#include "service/geometry/GeometryPayloadService.h"
#include "service/geometry/PhysicalDesignService.h"
#include "tech/TechStore.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/routing_layer/model/RoutingRuleComponents.h"
#include "tech/routing_layer/storage/RoutingLayerStorage.h"

namespace imj {
namespace {

using IndexPoint = boost::geometry::model::point<int32_t, 2, boost::geometry::cs::cartesian>;
using IndexBox = boost::geometry::model::box<IndexPoint>;
using IndexValue = std::pair<IndexBox, std::size_t>;
using OccupancyIndex = boost::geometry::index::rtree<IndexValue, boost::geometry::index::quadratic<16>>;

struct MetalFillLayer
{
  eccdb::TechRoutingLayerId id;
  std::string name;
  uint32_t sequence_index = 0;
  const eccdb::TechRoutingLayer* routing = nullptr;
};

struct LayerSnapshot
{
  std::vector<MetalFillOccupancy> occupancies;
  OccupancyIndex index;
};

[[nodiscard]] std::string configString(const std::map<std::string, std::any>& config, std::string_view key)
{
  const auto found = config.find(std::string{key});
  if (found == config.end()) {
    return {};
  }
  if (const auto* value = std::any_cast<std::string>(&found->second); value != nullptr) {
    return *value;
  }
  throw std::invalid_argument("iMJ metal fill option '" + std::string{key} + "' must be a string");
}

[[nodiscard]] int32_t effectiveWidth(const eccdb::TechRoutingLayer& layer)
{
  return layer.min_width > 0 ? layer.min_width : layer.width;
}

[[nodiscard]] int32_t ceilDivPositive(int64_t dividend, int32_t divisor)
{
  if (dividend <= 0 || divisor <= 0) {
    return 0;
  }
  const auto result = (dividend - 1) / divisor + 1;
  if (result > std::numeric_limits<int32_t>::max()) {
    throw std::overflow_error("iMJ metal fill minimum wire length exceeds int32");
  }
  return static_cast<int32_t>(result);
}

[[nodiscard]] int32_t minimumArea(const eccdb::TechRoutingLayerStorage& storage, eccdb::TechRoutingLayerId layer,
                                  const eccdb::TechRoutingLayer& routing)
{
  int64_t area = routing.area > 0 ? routing.area : 0;
  for (const auto rule_id : storage.minEncloseAreaRules(layer)) {
    const auto& rule = storage.rule(rule_id);
    if (rule.area > 0 && (area == 0 || rule.area < area)) {
      area = rule.area;
    }
  }
  if (area > std::numeric_limits<int32_t>::max()) {
    throw std::overflow_error("iMJ metal fill minimum area exceeds legacy planner range");
  }
  return static_cast<int32_t>(area);
}

[[nodiscard]] int32_t maximumSpacing(const eccdb::TechRoutingLayerStorage& storage, eccdb::TechRoutingLayerId layer)
{
  int32_t spacing = 0;
  for (const auto rule_id : storage.spacingRules(layer)) {
    spacing = std::max(spacing, storage.rule(rule_id).min_spacing);
  }
  for (const auto rule_id : storage.prlSpacingTableRules(layer)) {
    for (const auto value : storage.prlSpacingTableCells(rule_id)) {
      spacing = std::max(spacing, value);
    }
  }
  return std::max(spacing, 0);
}

[[nodiscard]] bool contributesToDensity(eccdb::GeometryOwnerDomain domain) noexcept
{
  // This mirrors the legacy GeometryStore source: routed metal/vias, pin
  // ports, existing fills, and LEF/DEF obstructions count as metal.
  switch (domain) {
    case eccdb::GeometryOwnerDomain::kDesignWire:
    case eccdb::GeometryOwnerDomain::kDesignNet:
    case eccdb::GeometryOwnerDomain::kDesignBlockPin:
    case eccdb::GeometryOwnerDomain::kDesignBlockage:
    case eccdb::GeometryOwnerDomain::kDesignFill: return true;
    default: return false;
  }
}

[[nodiscard]] bool blocksFill(eccdb::GeometryOwnerDomain domain) noexcept
{
  // The legacy planner ignores floorplan/grid bookkeeping records. The
  // payload query is layer-filtered, so unlayered records normally never
  // appear here; retaining this policy makes that invariant explicit.
  switch (domain) {
    case eccdb::GeometryOwnerDomain::kDesignDieArea:
    case eccdb::GeometryOwnerDomain::kDesignCoreArea:
    case eccdb::GeometryOwnerDomain::kDesignGuide:
    case eccdb::GeometryOwnerDomain::kDesignTrackAssignment: return false;
    default: return domain != eccdb::GeometryOwnerDomain::kNone;
  }
}

[[nodiscard]] IndexBox indexBox(const MIRect& rectangle) noexcept
{
  return IndexBox{IndexPoint{rectangle.get_ll_x(), rectangle.get_ll_y()}, IndexPoint{rectangle.get_ur_x(), rectangle.get_ur_y()}};
}

[[nodiscard]] MIRect toMIRect(eccdb::Rect rectangle) noexcept
{
  rectangle = rectangle.normalized();
  return MIRect{rectangle.ll_x, rectangle.ll_y, rectangle.ur_x, rectangle.ur_y};
}

[[nodiscard]] MIRect polygonBounds(std::span<const eccdb::Point> points);

[[nodiscard]] MIRect polygonBounds(const eccdb::GeometryPayloadBatch& batch, const eccdb::GeometryPayloadPolygon& polygon)
{
  const auto begin = static_cast<std::size_t>(polygon.point_offset);
  const auto count = static_cast<std::size_t>(polygon.point_count);
  if (count == 0 || begin > batch.polygon_points.size() || count > batch.polygon_points.size() - begin) {
    return {};
  }
  return polygonBounds(std::span<const eccdb::Point>{batch.polygon_points}.subspan(begin, count));
}

[[nodiscard]] MIRect polygonBounds(std::span<const eccdb::Point> points)
{
  if (points.empty()) {
    return {};
  }
  auto lower_x = points.front().x;
  auto lower_y = points.front().y;
  auto upper_x = lower_x;
  auto upper_y = lower_y;
  for (const auto point : points.subspan(1)) {
    lower_x = std::min(lower_x, point.x);
    lower_y = std::min(lower_y, point.y);
    upper_x = std::max(upper_x, point.x);
    upper_y = std::max(upper_y, point.y);
  }
  return MIRect{lower_x, lower_y, upper_x, upper_y};
}

class EccdbMetalFillSource final : public MetalFillSource
{
 public:
  EccdbMetalFillSource(eccdb::DatabaseContext& context, eccdb::Rect die, std::vector<MetalFillLayer> layers)
      : _context(context), _die(die.normalized()), _payload(context), _physical(context)
  {
    for (auto& layer : layers) {
      _layer_names.emplace(eccdb::TechLayerId{layer.id.entity()}.packed(), layer.name);
      _layers.emplace(layer.name, std::move(layer));
    }
  }

  [[nodiscard]] std::vector<MetalFillOccupancy> query(const MILayer& mi_layer, const MIRect& region) const override
  {
    const auto& snapshot = snapshotFor(mi_layer.get_layer_name());
    std::vector<IndexValue> matches;
    snapshot.index.query(boost::geometry::index::intersects(indexBox(region)), std::back_inserter(matches));

    std::vector<MetalFillOccupancy> result;
    result.reserve(matches.size());
    for (const auto& [unused, index] : matches) {
      result.push_back(snapshot.occupancies[index]);
    }
    return result;
  }

  [[nodiscard]] int32_t spacing(const MILayer& mi_layer, int32_t wire_width, int32_t parallel_length) const override
  {
    const auto& descriptor = layerFor(mi_layer.get_layer_name());
    const auto& storage = _context.technology()->routingLayerStorage();
    int32_t default_spacing = 0;
    for (const auto rule_id : storage.spacingRules(descriptor.id)) {
      const auto& rule = storage.rule(rule_id);
      if (rule.type == eccdb::TechRoutingSpacingType::kDefault
          || (rule.type == eccdb::TechRoutingSpacingType::kRange && rule.min_width <= wire_width && wire_width <= rule.max_width)) {
        default_spacing = std::max(default_spacing, rule.min_spacing);
      }
    }

    int32_t prl_spacing = 0;
    for (const auto rule_id : storage.prlSpacingTableRules(descriptor.id)) {
      prl_spacing = std::max(prl_spacing, storage.prlSpacingFor(rule_id, wire_width, wire_width, std::max(parallel_length, 0)));
    }
    return std::max(default_spacing, prl_spacing);
  }

 private:
  [[nodiscard]] const MetalFillLayer& layerFor(const std::string& name) const
  {
    const auto found = _layers.find(name);
    if (found == _layers.end()) {
      throw std::logic_error("iMJ metal-fill source has no routing layer named '" + name + "'");
    }
    return found->second;
  }

  [[nodiscard]] const LayerSnapshot& snapshotFor(const std::string& name) const
  {
    const auto found = _snapshots.find(name);
    if (found != _snapshots.end()) {
      return found->second;
    }

    ensureDerivedPhysicalOccupancy();
    const auto& descriptor = layerFor(name);
    LayerSnapshot snapshot;
    eccdb::GeometryQueryFilter filter;
    filter.layer = eccdb::TechLayerId{descriptor.id.entity()}.packed();
    const auto batch = _payload.queryGeometry(_die, filter);
    const auto add = [&](MetalFillOccupancy occupancy) {
      if (!occupancy.bounds.is_valid()) {
        return;
      }
      snapshot.occupancies.push_back(occupancy);
    };
    const auto addPayload = [&](const MIRect& bounds, uint32_t header_index) {
      if (!bounds.is_valid() || header_index >= batch.headers.size()) {
        return;
      }
      const auto domain = static_cast<eccdb::GeometryOwnerDomain>(batch.headers[header_index].owner_domain);
      add(MetalFillOccupancy{.bounds = bounds, .contributes_density = contributesToDensity(domain), .blocks_fill = blocksFill(domain)});
    };

    // Route segments use their resolved physical header bounds. VIA entries
    // deliberately do not use their logical header bounds: queryGeometry has
    // already expanded their individual metal/cut primitives, and the layer
    // filter below retains only this routing layer's physical shapes.
    for (const auto& segment : batch.segments) {
      if (segment.header_index < batch.headers.size()) {
        addPayload(toMIRect(batch.headers[segment.header_index].bounds), segment.header_index);
      }
    }
    for (const auto& rectangle : batch.rectangles) {
      if (rectangle.header_index < batch.headers.size() && rectangle.layer == filter.layer) {
        addPayload(toMIRect(rectangle.rectangle), rectangle.header_index);
      }
    }
    for (const auto& polygon : batch.polygons) {
      if (polygon.header_index < batch.headers.size() && polygon.layer == filter.layer) {
        addPayload(polygonBounds(batch, polygon), polygon.header_index);
      }
    }

    if (const auto extras = _derived_physical.find(name); extras != _derived_physical.end()) {
      for (const auto& occupancy : extras->second) {
        add(occupancy);
      }
    }

    // A layer-local shape without a primitive payload is not a physical metal
    // candidate. In particular this prevents a logical VIA's cross-layer
    // bounding box from becoming an obstacle on every layer.
    std::vector<IndexValue> entries;
    entries.reserve(snapshot.occupancies.size());
    for (std::size_t index = 0; index < snapshot.occupancies.size(); ++index) {
      entries.emplace_back(indexBox(snapshot.occupancies[index].bounds), index);
    }
    snapshot.index = OccupancyIndex{entries.begin(), entries.end()};
    const auto [inserted, success] = _snapshots.emplace(name, std::move(snapshot));
    static_cast<void>(success);
    return inserted->second;
  }

  void appendDerivedPhysical(eccdb::TechLayerId layer, const MIRect& bounds, bool contributes_density, bool blocks_fill) const
  {
    if (!bounds.is_valid()) {
      return;
    }
    const auto found = _layer_names.find(layer.packed());
    if (found == _layer_names.end()) {
      return;
    }
    _derived_physical[found->second].push_back(
        MetalFillOccupancy{.bounds = bounds, .contributes_density = contributes_density, .blocks_fill = blocks_fill});
  }

  void appendDerivedVia(const eccdb::PhysicalViaPlacement& via, bool contributes_density, bool blocks_fill) const
  {
    const eccdb::PhysicalGeometryVisitor expanded{
        .rectangle = [this, contributes_density, blocks_fill](const eccdb::PhysicalLayerRect& rectangle) {
          appendDerivedPhysical(rectangle.layer, toMIRect(rectangle.rectangle), contributes_density, blocks_fill);
        },
        .polygon = [this, contributes_density, blocks_fill](const eccdb::PhysicalLayerPolygon& polygon) {
          appendDerivedPhysical(polygon.layer, polygonBounds(polygon.points), contributes_density, blocks_fill);
        }};
    if (via.tech_via) {
      _physical.forEachTechViaGeometry(via.tech_via, via.origin, via.orientation, expanded);
    } else {
      _physical.forEachDesignViaGeometry(via.design_via, via.origin, via.orientation, expanded);
    }
  }

  void ensureDerivedPhysicalOccupancy() const
  {
    if (_derived_physical_built) {
      return;
    }
    const eccdb::PhysicalGeometryVisitor visitor{
        .rectangle = [this](const eccdb::PhysicalLayerRect& rectangle) {
          appendDerivedPhysical(rectangle.layer, toMIRect(rectangle.rectangle), true, true);
        },
        .polygon = [this](const eccdb::PhysicalLayerPolygon& polygon) {
          appendDerivedPhysical(polygon.layer, polygonBounds(polygon.points), true, true);
        },
        .via = [this](const eccdb::PhysicalViaPlacement& via) { appendDerivedVia(via, true, true); }};
    _physical.forEachInstance([this, &visitor](eccdb::DesignInstanceId instance, const eccdb::DesignInstance&,
                                                std::span<const eccdb::DesignInstanceTermId> terms) {
      if (!_physical.isPlaced(instance)) {
        return;
      }
      for (const auto term : terms) {
        _physical.forEachPlacedInstancePinGeometry(term, visitor);
      }
      _physical.forEachPlacedMasterObstructionGeometry(instance, visitor);
    });
    _derived_physical_built = true;
  }

  eccdb::DatabaseContext& _context;
  eccdb::Rect _die;
  eccdb::GeometryPayloadService _payload;
  eccdb::PhysicalDesignService _physical;
  std::map<std::string, MetalFillLayer, std::less<>> _layers;
  std::map<uint64_t, std::string> _layer_names;
  mutable std::map<std::string, LayerSnapshot, std::less<>> _snapshots;
  mutable std::map<std::string, std::vector<MetalFillOccupancy>, std::less<>> _derived_physical;
  mutable bool _derived_physical_built = false;
};

[[nodiscard]] std::pair<int32_t, int32_t> preferredTrack(const eccdb::PhysicalDesignService& physical,
                                                          const MetalFillLayer& descriptor, bool horizontal)
{
  const auto axis = horizontal ? eccdb::DesignAxis::kY : eccdb::DesignAxis::kX;
  std::optional<std::pair<uint64_t, std::pair<int32_t, int32_t>>> selected;
  physical.forEachTrackGrid(descriptor.id, [&](eccdb::DesignTrackGridId id, const eccdb::DesignTrackGrid& grid) {
    if (grid.axis != axis || grid.step <= 0) {
      return;
    }
    const auto candidate = std::make_pair(grid.start, grid.step);
    if (!selected.has_value() || id.packed() < selected->first) {
      selected = std::make_pair(id.packed(), candidate);
    }
  });
  if (selected.has_value()) {
    return selected->second;
  }
  return horizontal ? std::make_pair(descriptor.routing->offset_y, descriptor.routing->pitch_y)
                    : std::make_pair(descriptor.routing->offset_x, descriptor.routing->pitch_x);
}

[[nodiscard]] MILayer buildLayer(const eccdb::PhysicalDesignService& physical, const eccdb::TechRoutingLayerStorage& storage,
                                 const MetalFillLayer& descriptor)
{
  const auto& routing = *descriptor.routing;
  const auto width = effectiveWidth(routing);
  if (width <= 0) {
    throw std::invalid_argument("iMJ metal fill requires a positive routing width on " + descriptor.name);
  }
  const bool horizontal = routing.direction == eccdb::TechRoutingDirection::kHorizontal;
  if (!horizontal && routing.direction != eccdb::TechRoutingDirection::kVertical) {
    throw std::invalid_argument("iMJ metal fill supports only horizontal or vertical routing layer " + descriptor.name);
  }
  const auto [track_start, track_pitch] = preferredTrack(physical, descriptor, horizontal);
  if (track_pitch <= 0) {
    throw std::invalid_argument("iMJ metal fill requires routing tracks or a positive preferred pitch on " + descriptor.name);
  }

  MILayer result;
  result.set_layer_name(descriptor.name);
  result.set_geometry_layer_idx(static_cast<int32_t>(descriptor.sequence_index));
  result.set_is_horizontal(horizontal);
  result.set_wire_width(width);
  result.set_min_wire_length(std::max(width, ceilDivPositive(minimumArea(storage, descriptor.id, routing), width)));
  result.set_track_start(track_start);
  result.set_track_pitch(track_pitch);
  result.set_max_spacing(maximumSpacing(storage, descriptor.id));
  return result;
}

}  // namespace

void insertMetalEccdb(eccdb::DatabaseContext& context, std::map<std::string, std::any>& config)
{
  if (context.technology() == nullptr) {
    throw std::logic_error("iMJ metal fill requires a complete EccDB technology context");
  }

  eccdb::PhysicalDesignService physical{context};
  if (!physical.hasDieArea()) {
    throw std::invalid_argument("iMJ metal fill requires a die area");
  }
  const auto die = physical.dieBounds().normalized();
  if (die.ll_x >= die.ur_x || die.ll_y >= die.ur_y) {
    throw std::invalid_argument("iMJ metal fill requires a non-empty die area");
  }
  const auto dbu = physical.databaseUnitsPerMicron();
  if (dbu <= 0) {
    throw std::invalid_argument("iMJ metal fill requires a positive DBU-per-micron");
  }

  std::vector<MetalFillLayer> all_layers;
  physical.forEachRoutingLayer([&](eccdb::TechRoutingLayerId id, uint32_t sequence_index, uint32_t,
                                   const eccdb::TechLayerInfo& info, const eccdb::TechRoutingLayer& routing) {
    all_layers.push_back(MetalFillLayer{.id = id, .name = info.name, .sequence_index = sequence_index, .routing = &routing});
  });
  if (all_layers.empty()) {
    throw std::invalid_argument("iMJ metal fill requires at least one routing layer");
  }

  const auto min_name = configString(config, "-min_fill_layer");
  const auto max_name = configString(config, "-max_fill_layer");
  const auto layerIndex = [&](const std::string& name, bool minimum) {
    if (name.empty()) {
      return minimum ? 0 : static_cast<int32_t>(all_layers.size()) - 1;
    }
    for (int32_t index = 0; index < static_cast<int32_t>(all_layers.size()); ++index) {
      if (all_layers[static_cast<std::size_t>(index)].name == name) {
        return index;
      }
    }
    throw std::invalid_argument("iMJ metal fill layer is not a routing layer: " + name);
  };
  const auto min_index = layerIndex(min_name, true);
  const auto max_index = layerIndex(max_name, false);
  if (min_index > max_index) {
    throw std::invalid_argument("iMJ metal fill layer range is invalid");
  }
  std::vector<MetalFillLayer> selected_layers;
  selected_layers.reserve(static_cast<std::size_t>(max_index - min_index + 1));
  for (int32_t index = min_index; index <= max_index; ++index) {
    selected_layers.push_back(all_layers[static_cast<std::size_t>(index)]);
  }

  const auto& storage = context.technology()->routingLayerStorage();
  MIModel model;
  MIComParam parameters{100.0, 50.0, 0.10, 0.80};
  parameters.set_min_fill_layer(selected_layers.front().name);
  parameters.set_max_fill_layer(selected_layers.back().name);
  model.set_mi_com_param(parameters);
  model.set_die(MIRect{die.ll_x, die.ll_y, die.ur_x, die.ur_y});
  model.set_micron_dbu(dbu);
  model.set_manufacture_grid(std::max(physical.manufacturingGrid().value_or(1), 1));

  std::vector<MILayer> model_layers;
  model_layers.reserve(selected_layers.size());
  for (const auto& layer : selected_layers) {
    model_layers.push_back(buildLayer(physical, storage, layer));
  }
  model.set_mi_layer_list(model_layers);

  // Each source layer snapshots EccDB's exact physical primitives once, then
  // services the many density/track queries through its compact R-tree.
  EccdbMetalFillSource source{context, die, selected_layers};
  MetalFillKernel::build(model, source);

  eccdb::GeometryEditService edits{context};
  std::vector<eccdb::DesignFillId> created;
  try {
    for (std::size_t index = 0; index < model.get_mi_layer_list().size(); ++index) {
      const auto& planned = model.get_mi_layer_list()[index];
      if (planned.get_fill_rect_list().empty()) {
        continue;
      }
      eccdb::DesignFillInput input{.layer = eccdb::TechLayerId{selected_layers[index].id.entity()}};
      input.rectangles.reserve(planned.get_fill_rect_list().size());
      for (const auto& rectangle : planned.get_fill_rect_list()) {
        input.rectangles.push_back(eccdb::Rect{.ll_x = rectangle.get_ll_x(),
                                               .ll_y = rectangle.get_ll_y(),
                                               .ur_x = rectangle.get_ur_x(),
                                               .ur_y = rectangle.get_ur_y()});
      }
      created.push_back(edits.createFill(std::move(input)));
    }
  } catch (...) {
    for (auto it = created.rbegin(); it != created.rend(); ++it) {
      static_cast<void>(edits.destroyFill(*it));
    }
    throw;
  }
}

}  // namespace imj
