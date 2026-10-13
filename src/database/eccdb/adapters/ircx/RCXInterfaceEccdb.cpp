// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "RCXInterface.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

#include "DataManager.hpp"
#include "Direction.hpp"
#include "LayerShape.hpp"
#include "Logger.hpp"
#include "Patch.hpp"
#include "Pin.hpp"
#include "RoutingLayer.hpp"
#include "Segment.hpp"
#include "TrackInfo.hpp"
#include "Via.hpp"
#include "service/context/DatabaseContext.h"
#include "service/geometry/PhysicalDesignService.h"
#include "storage/design/DesignStore.h"
#include "storage/library/LibraryStore.h"

namespace ircx {
namespace {

[[nodiscard]] Direction toDirection(const eccdb::LibraryMasterTermDirection direction)
{
  switch (direction) {
    case eccdb::LibraryMasterTermDirection::kInput:
      return Direction::kInput;
    case eccdb::LibraryMasterTermDirection::kOutput:
    case eccdb::LibraryMasterTermDirection::kOutputTriState:
      return Direction::kOutput;
    case eccdb::LibraryMasterTermDirection::kInOut:
    case eccdb::LibraryMasterTermDirection::kFeedThru:
      return Direction::kInOut;
    default:
      return Direction::kNone;
  }
}

[[nodiscard]] Direction toDirection(const eccdb::DesignIoPinDirection direction)
{
  switch (direction) {
    case eccdb::DesignIoPinDirection::kInput:
      return Direction::kInput;
    case eccdb::DesignIoPinDirection::kOutput:
      return Direction::kOutput;
    case eccdb::DesignIoPinDirection::kInOut:
    case eccdb::DesignIoPinDirection::kFeedThru:
      return Direction::kInOut;
    default:
      return Direction::kNone;
  }
}

[[nodiscard]] bool isDriver(const eccdb::LibraryMasterTermDirection direction)
{
  return direction == eccdb::LibraryMasterTermDirection::kOutput || direction == eccdb::LibraryMasterTermDirection::kOutputTriState;
}

[[nodiscard]] bool isDriver(const eccdb::DesignIoPinDirection direction)
{
  // A block INPUT is driven from outside of the current block.
  return direction == eccdb::DesignIoPinDirection::kInput;
}

[[nodiscard]] eccdb::Rect bounds(std::span<const eccdb::Point> points)
{
  if (points.empty()) {
    return {};
  }
  eccdb::Rect result{.ll_x = points.front().x, .ll_y = points.front().y, .ur_x = points.front().x, .ur_y = points.front().y};
  for (const auto point : points) {
    result.ll_x = std::min(result.ll_x, point.x);
    result.ll_y = std::min(result.ll_y, point.y);
    result.ur_x = std::max(result.ur_x, point.x);
    result.ur_y = std::max(result.ur_y, point.y);
  }
  return result;
}

[[nodiscard]] eccdb::Rect regularSegmentBounds(const eccdb::Point begin, const eccdb::Point end, const int32_t width)
{
  const int32_t normalized_width = std::max(width, 1);
  const int32_t lower_half = normalized_width / 2;
  const int32_t upper_half = normalized_width - lower_half;

  // Keep the legacy regular-wire projection: iDB's get_segment_rect()
  // includes the default half-width cap at both path endpoints.
  return {.ll_x = std::min(begin.x, end.x) - lower_half,
          .ll_y = std::min(begin.y, end.y) - lower_half,
          .ur_x = std::max(begin.x, end.x) + upper_half,
          .ur_y = std::max(begin.y, end.y) + upper_half};
}

[[nodiscard]] eccdb::Rect specialSegmentBounds(const eccdb::Point begin, const eccdb::Point end, const int32_t width)
{
  const int32_t normalized_width = std::max(width, 1);
  const int32_t lower_half = normalized_width / 2;
  const int32_t upper_half = normalized_width - lower_half;

  // iDB/OpenROAD represent a SPECIALNET stripe using its explicit endpoints
  // along the routing direction. Only its perpendicular dimension expands by
  // route width. Regular NETs deliberately use regularSegmentBounds() above.
  if (begin.y == end.y && begin.x != end.x) {
    return {.ll_x = std::min(begin.x, end.x),
            .ll_y = begin.y - lower_half,
            .ur_x = std::max(begin.x, end.x),
            .ur_y = begin.y + upper_half};
  }
  if (begin.x == end.x && begin.y != end.y) {
    return {.ll_x = begin.x - lower_half,
            .ll_y = std::min(begin.y, end.y),
            .ur_x = begin.x + upper_half,
            .ur_y = std::max(begin.y, end.y)};
  }

  // DEF path endpoints delimit orthogonal stripes. Retain the existing
  // conservative box only for diagonal or zero-length route primitives.
  return {.ll_x = std::min(begin.x, end.x) - lower_half,
          .ll_y = std::min(begin.y, end.y) - lower_half,
          .ur_x = std::max(begin.x, end.x) + upper_half,
          .ur_y = std::max(begin.y, end.y) + upper_half};
}

[[nodiscard]] GTLRectInt toRect(const eccdb::Rect rect)
{
  return {rect.ll_x, rect.ll_y, rect.ur_x, rect.ur_y};
}

[[nodiscard]] GTLPointInt toPoint(const eccdb::Point point)
{
  return {point.x, point.y};
}

[[nodiscard]] int32_t layerIndex(const eccdb::PhysicalDesignService& physical, const eccdb::TechLayerId layer)
{
  return RCXDM.getDatabase().get_layer_table().get_design_idx(physical.layerInfo(layer).name);
}

void appendPhysicalRect(Pin& pin, const eccdb::PhysicalLayerRect& rectangle, const eccdb::PhysicalDesignService& physical)
{
  pin.get_layer_shape_list().emplace_back(layerIndex(physical, rectangle.layer), toRect(rectangle.rectangle));
}

void appendPhysicalPolygon(Pin& pin, const eccdb::PhysicalLayerPolygon& polygon, const eccdb::PhysicalDesignService& physical)
{
  pin.get_layer_shape_list().emplace_back(layerIndex(physical, polygon.layer), toRect(bounds(polygon.points)));
}

struct ViaLayerBounds
{
  std::optional<eccdb::Rect> bottom;
  std::optional<eccdb::Rect> cut;
  std::optional<eccdb::Rect> top;
};

[[nodiscard]] std::optional<eccdb::TechLayerId> viaCutLayer(const eccdb::PhysicalDesignService& physical,
                                                             const eccdb::PhysicalViaPlacement& placement);

void merge(std::optional<eccdb::Rect>& target, const eccdb::Rect value)
{
  target = target ? target->united(value) : value;
}

[[nodiscard]] ViaLayerBounds collectViaBounds(const eccdb::PhysicalDesignService& physical, const eccdb::PhysicalViaPlacement& via)
{
  ViaLayerBounds result;
  const auto visitor = eccdb::PhysicalGeometryVisitor{
      .rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
        const auto layers = physical.routingViaLayers(via.tech_via, via.design_via);
        if (rectangle.layer.entity() == layers.bottom.entity()) {
          merge(result.bottom, rectangle.rectangle);
        } else if (rectangle.layer.entity() == layers.top.entity()) {
          merge(result.top, rectangle.rectangle);
        } else {
          merge(result.cut, rectangle.rectangle);
        }
      },
      .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
        const auto layers = physical.routingViaLayers(via.tech_via, via.design_via);
        const auto rectangle = bounds(polygon.points);
        if (polygon.layer.entity() == layers.bottom.entity()) {
          merge(result.bottom, rectangle);
        } else if (polygon.layer.entity() == layers.top.entity()) {
          merge(result.top, rectangle);
        } else {
          merge(result.cut, rectangle);
        }
      }};
  if (via.tech_via) {
    physical.forEachTechViaGeometry(via.tech_via, via.origin, via.orientation, visitor);
  } else if (via.design_via) {
    physical.forEachDesignViaGeometry(via.design_via, via.origin, via.orientation, visitor);
  }
  return result;
}

void appendVia(Net& net, const eccdb::PhysicalDesignService& physical, const eccdb::PhysicalViaPlacement& placement)
{
  const auto layers = physical.routingViaLayers(placement.tech_via, placement.design_via);
  const auto bounds_by_layer = collectViaBounds(physical, placement);
  const auto cut_layer = viaCutLayer(physical, placement);
  if (!bounds_by_layer.bottom || !bounds_by_layer.cut || !bounds_by_layer.top || !cut_layer) {
    return;
  }
  Via via;
  via.set_via_name(std::string{physical.viaName(placement.tech_via, placement.design_via)});
  via.set_point(toPoint(placement.origin));
  via.set_bottom_layer_shape(LayerShape(layerIndex(physical, eccdb::TechLayerId{layers.bottom.entity()}), toRect(*bounds_by_layer.bottom)));
  via.set_cut_layer_shape(LayerShape(layerIndex(physical, *cut_layer), toRect(*bounds_by_layer.cut)));
  via.set_top_layer_shape(LayerShape(layerIndex(physical, eccdb::TechLayerId{layers.top.entity()}), toRect(*bounds_by_layer.top)));
  net.get_via_list().push_back(std::move(via));
}

void appendRouteVia(Net& net, const eccdb::PhysicalDesignService& physical, const eccdb::DesignRouteVia& route_via)
{
  const bool is_array = (route_via.via.flags & eccdb::DesignWireViaFlag::kHasArray) != 0u;
  const uint32_t columns = is_array ? route_via.via.columns : 1u;
  const uint32_t rows = is_array ? route_via.via.rows : 1u;
  const int64_t step_x = is_array ? route_via.via.step_x : 0;
  const int64_t step_y = is_array ? route_via.via.step_y : 0;

  // ODB materializes SPECIALNET VIA arrays as one physical via per member,
  // in column-major order. iRCX stores the same one-placement-per-Via model.
  for (uint32_t column = 0; column < columns; ++column) {
    for (uint32_t row = 0; row < rows; ++row) {
      const auto origin = eccdb::Point{.x = static_cast<int32_t>(static_cast<int64_t>(route_via.origin.position.x)
                                                                 + static_cast<int64_t>(column) * step_x),
                                       .y = static_cast<int32_t>(static_cast<int64_t>(route_via.origin.position.y)
                                                                 + static_cast<int64_t>(row) * step_y)};
      appendVia(net, physical, {.tech_via = route_via.via.tech_via,
                                .design_via = route_via.via.design_via,
                                .origin = origin,
                                .orientation = route_via.via.orientation});
    }
  }
}

// The cut layer is not encoded in PhysicalRoutingViaLayers. Resolve it from
// canonical VIA geometry rather than assuming a numeric layer ordering.
[[nodiscard]] std::optional<eccdb::TechLayerId> viaCutLayer(const eccdb::PhysicalDesignService& physical,
                                                             const eccdb::PhysicalViaPlacement& placement)
{
  std::optional<eccdb::TechLayerId> result;
  const auto layers = physical.routingViaLayers(placement.tech_via, placement.design_via);
  const auto visitor = eccdb::PhysicalGeometryVisitor{
      .rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
        if (rectangle.layer.entity() != layers.bottom.entity() && rectangle.layer.entity() != layers.top.entity()) result = rectangle.layer;
      },
      .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
        if (polygon.layer.entity() != layers.bottom.entity() && polygon.layer.entity() != layers.top.entity()) result = polygon.layer;
      }};
  if (placement.tech_via) {
    physical.forEachTechViaGeometry(placement.tech_via, placement.origin, placement.orientation, visitor);
  } else if (placement.design_via) {
    physical.forEachDesignViaGeometry(placement.design_via, placement.origin, placement.orientation, visitor);
  }
  return result;
}

void appendRouteShape(Net& net, const eccdb::PhysicalDesignService& physical, const eccdb::DesignRouteShape& shape,
                      const eccdb::DesignRouteShapeAttributes* attributes, const bool special_net)
{
  std::visit(
      [&](const auto& geometry) {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
          Segment segment;
          const int32_t width = attributes != nullptr && (shape.flags & eccdb::DesignWirePathFlag::kHasWidth) != 0u
                                    ? attributes->width
                                    : physical.routingLayer(shape.layer).width;
          segment.set_layer_idx(layerIndex(physical, eccdb::TechLayerId{shape.layer.entity()}));
          segment.set_start_point(toPoint(geometry.begin.position));
          segment.set_end_point(toPoint(geometry.end.position));
          segment.set_shape(toRect(special_net ? specialSegmentBounds(geometry.begin.position, geometry.end.position, width)
                                               : regularSegmentBounds(geometry.begin.position, geometry.end.position, width)));
          net.get_segment_list().push_back(std::move(segment));
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRoutePatch>) {
          Patch patch;
          patch.set_layer_idx(layerIndex(physical, eccdb::TechLayerId{shape.layer.entity()}));
          patch.set_shape(toRect(geometry.rectangle));
          net.get_patch_list().push_back(std::move(patch));
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
          appendRouteVia(net, physical, geometry);
        }
      },
      shape.geometry);
}

void appendInstancePin(Net& net, const eccdb::PhysicalDesignService& physical, const eccdb::DesignInstancePinId pin_id,
                       const eccdb::DesignInstancePin& pin, const eccdb::DesignInstance& instance,
                       const eccdb::LibraryMasterTerm& term, RCXInterface& interface)
{
  Pin result;
  result.set_pin_name(interface.getSPEFName(instance.name) + ":" + interface.getSPEFName(term.name));
  result.set_is_driver(isDriver(term.direction));
  result.set_direction(toDirection(term.direction));
  physical.forEachPlacedInstancePinGeometry(pin_id, {.rectangle = [&](const auto& rectangle) { appendPhysicalRect(result, rectangle, physical); },
                                                      .polygon = [&](const auto& polygon) { appendPhysicalPolygon(result, polygon, physical); }});
  net.get_pin_list().push_back(std::move(result));
}

void appendIoPin(Net& net, const eccdb::PhysicalDesignService& physical, const eccdb::DesignIoPinId pin_id,
                 const eccdb::DesignIoPin& pin, RCXInterface& interface)
{
  Pin result;
  result.set_pin_name(interface.getSPEFName(pin.name));
  result.set_is_driver(isDriver(pin.direction));
  result.set_direction(toDirection(pin.direction));
  physical.forEachIoPinGeometry(pin_id, {.rectangle = [&](const auto& rectangle) { appendPhysicalRect(result, rectangle, physical); },
                                          .polygon = [&](const auto& polygon) { appendPhysicalPolygon(result, polygon, physical); }});
  net.get_pin_list().push_back(std::move(result));
}

}  // namespace

void RCXInterface::wrapDatabaseFromEccdb()
{
  if (_database_context == nullptr || _database_context->technology() == nullptr || _database_context->library() == nullptr) {
    RCXLOG.error(Loc::current(), "Native iRCX requires EccDB Design, Tech, and Library stores.");
    return;
  }

  const eccdb::PhysicalDesignService physical{*_database_context};
  const auto& design = _database_context->design();
  auto& database = RCXDM.getDatabase();
  auto& layout = database.get_layout_data();
  auto& layers = database.get_layer_table();
  layout.get_routing_layer_map().clear();
  layout.get_net_list().clear();
  layout.get_special_net() = Net{};
  layers.get_design_idx_to_name_map().clear();
  layers.get_design_name_to_idx_map().clear();

  if (physical.hasDesignInfo()) {
    layout.set_design_name(design.globalStorage().info().name);
    layout.set_dbu_per_micron(physical.databaseUnitsPerMicron());
  }
  if (physical.hasDieArea()) {
    layout.set_die_shape(toRect(physical.dieBounds()));
  }

  int32_t layer_idx = 1;
  physical.forEachRoutingLayer([&](const auto id, const uint32_t, const uint32_t, const auto& info, const auto& routing) {
    layers.register_design_layer(layer_idx, info.name);
    RoutingLayer target;
    target.set_layer_idx(layer_idx);
    target.set_layer_name(info.name);
    target.set_layer_width(routing.width);
    target.set_is_prefer_horizontal(routing.direction == eccdb::TechRoutingDirection::kHorizontal);
    TrackInfo track;
    physical.forEachTrackGrid(id, [&](const auto, const eccdb::DesignTrackGrid& grid) {
      if (grid.axis == eccdb::DesignAxis::kX) {
        track.set_x_origin(grid.start);
        track.set_x_step(grid.step);
        track.set_x_count(static_cast<int32_t>(grid.track_count));
      } else {
        track.set_y_origin(grid.start);
        track.set_y_step(grid.step);
        track.set_y_count(static_cast<int32_t>(grid.track_count));
      }
    });
    target.set_track_info(track);
    layout.get_routing_layer_map()[layer_idx] = std::move(target);
    ++layer_idx;
  });
  physical.forEachCutLayer([&](const auto, const uint32_t, const auto& info, const auto&) { layers.register_design_layer(layer_idx++, info.name); });

  auto& names = database.get_spef_name_data();
  names.get_net_name_list().clear();
  names.get_port_name_list().clear();
  names.get_port_direction_list().clear();
  names.get_instance_name_list().clear();
  names.get_instance_name_to_cell_name_map().clear();
  design.netlistStorage().forEachInstance([&](const auto, const auto& instance, const auto) {
    const auto& master = _database_context->library()->cellMasterStorage().cellMaster(instance.master);
    const auto instance_name = getSPEFName(instance.name);
    names.get_instance_name_list().push_back(instance_name);
    names.get_instance_name_to_cell_name_map()[instance_name] = getSPEFName(master.name);
  });
  design.netlistStorage().forEachIoPin([&](const auto, const auto& pin) {
    if (!pin.net || pin.special_net) return;
    names.get_port_name_list().push_back(getSPEFName(pin.name));
    names.get_port_direction_list().push_back(toDirection(pin.direction));
  });

  int32_t net_idx = 0;
  physical.forEachRegularNet([&](const auto net_id, const auto& net_data) {
    names.get_net_name_list().push_back(getSPEFName(net_data.name));
    Net net;
    net.set_net_idx(net_idx++);
    net.set_net_name(getSPEFName(net_data.name));
    physical.forEachNetPin(net_id, {.instance_pin = [&](const auto pin_id, const auto& pin, const auto& instance, const auto& term) {
                                      appendInstancePin(net, physical, pin_id, pin, instance, term, *this);
                                    },
                                    .io_pin = [&](const auto pin_id, const auto& pin) { appendIoPin(net, physical, pin_id, pin, *this); }});
    physical.forEachNetWireShape(net_id, [&](const auto, const auto&, const auto, const auto& shape, const auto* attributes) {
      appendRouteShape(net, physical, shape, attributes, false);
    });
    layout.get_net_list().push_back(std::move(net));
  });

  Net& special = layout.get_special_net();
  physical.forEachSpecialNet([&](const auto net_id, const auto&) {
    physical.forEachNetWireShape(net_id, [&](const auto, const auto&, const auto, const auto& shape, const auto* attributes) {
      appendRouteShape(special, physical, shape, attributes, true);
    });
    physical.forEachNetGeometry(net_id, {.rectangle = [&](const eccdb::DesignGeometryRectangle& rectangle) {
                                           Patch patch;
                                           patch.set_layer_idx(layerIndex(physical, eccdb::TechLayerId{rectangle.layer.entity()}));
                                           patch.set_shape(toRect(rectangle.rectangle));
                                           special.get_patch_list().push_back(std::move(patch));
                                         },
                                         .polygon = [&](const eccdb::DesignGeometryPolygon& polygon) {
                                           Patch patch;
                                           patch.set_layer_idx(layerIndex(physical, eccdb::TechLayerId{polygon.layer.entity()}));
                                           patch.set_shape(toRect(bounds(polygon.points)));
                                           special.get_patch_list().push_back(std::move(patch));
                                         },
                                         .via = [&](const eccdb::DesignGeometryNetVia& via) {
                                           for (const auto origin : via.origins) {
                                             appendVia(special, physical, {.tech_via = via.tech_via,
                                                                          .design_via = via.design_via,
                                                                          .origin = origin,
                                                                          .orientation = via.orientation});
                                           }
                                         }});
  });
}

}  // namespace ircx
