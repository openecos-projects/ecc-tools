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

#include "LVSInterface.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "ConnectType.hpp"
#include "DataManager.hpp"
#include "DefData.hpp"
#include "DefRoutingData.hpp"
#include "Die.hpp"
#include "Logger.hpp"
#include "Net.hpp"
#include "NetRoutingData.hpp"
#include "RoutingShape.hpp"
#include "RoutingVia.hpp"
#include "Shape.hpp"
#include "service/context/DatabaseContext.h"
#include "service/geometry/PhysicalDesignService.h"
#include "storage/design/DesignStore.h"

namespace ilvs {
namespace {

[[nodiscard]] std::string ioTerminalName(const std::string& name)
{
  return "PIN/" + name;
}

// EccDB intentionally retains a DEF identifier exactly as it appeared on
// input so a later DEF export can round-trip it.  iLVS, however, compares
// that physical name with the reference Verilog identifier.  Decode only
// paired DEF escape markers at this boundary: a doubled slash represents one
// literal slash, while an unmatched slash remains visible instead of being
// silently discarded.
[[nodiscard]] std::string lvsNetName(std::string_view name)
{
  std::string result;
  result.reserve(name.size());
  for (std::size_t index = 0; index < name.size(); ++index) {
    if (name[index] == '\\' && index + 1u < name.size()) {
      ++index;
    }
    result.push_back(name[index]);
  }
  return result;
}

void registerLvsNetName(std::unordered_map<std::string, std::string>& physical_names, std::string_view physical_name,
                        const std::string& lvs_name)
{
  const auto [found, inserted] = physical_names.try_emplace(lvs_name, physical_name);
  if (!inserted && found->second != physical_name) {
    throw std::runtime_error("iLVS cannot compare DEF nets '" + found->second + "' and '" + std::string(physical_name)
                             + "': both decode to '" + lvs_name + "'");
  }
}

[[nodiscard]] std::string instanceTerminalName(const eccdb::DesignInstance& instance, const eccdb::LibraryMasterTerm& term)
{
  return instance.name + "/" + term.name;
}

[[nodiscard]] bool isPower(const eccdb::DesignSignalUse use)
{
  return use == eccdb::DesignSignalUse::kPower;
}

[[nodiscard]] bool isGround(const eccdb::DesignSignalUse use)
{
  return use == eccdb::DesignSignalUse::kGround;
}

[[nodiscard]] bool isDriver(const eccdb::LibraryMasterTermDirection direction)
{
  return direction == eccdb::LibraryMasterTermDirection::kOutput || direction == eccdb::LibraryMasterTermDirection::kOutputTriState;
}

[[nodiscard]] eccdb::Rect polygonBounds(const std::span<const eccdb::Point> points)
{
  if (points.empty()) {
    return {};
  }
  eccdb::Rect bounds{.ll_x = points.front().x, .ll_y = points.front().y, .ur_x = points.front().x, .ur_y = points.front().y};
  for (const auto point : points) {
    bounds.ll_x = std::min(bounds.ll_x, point.x);
    bounds.ll_y = std::min(bounds.ll_y, point.y);
    bounds.ur_x = std::max(bounds.ur_x, point.x);
    bounds.ur_y = std::max(bounds.ur_y, point.y);
  }
  return bounds;
}

[[nodiscard]] eccdb::Rect segmentBounds(const eccdb::Point begin, const eccdb::Point end, const int32_t width)
{
  const int32_t normalized_width = std::max(width, 1);
  const int32_t lower_half = normalized_width / 2;
  const int32_t upper_half = normalized_width - lower_half;
  return {.ll_x = std::min(begin.x, end.x) - lower_half,
          .ll_y = std::min(begin.y, end.y) - lower_half,
          .ur_x = std::max(begin.x, end.x) + upper_half,
          .ur_y = std::max(begin.y, end.y) + upper_half};
}

[[nodiscard]] RoutingShape toRoutingShape(const eccdb::PhysicalDesignService& physical, const eccdb::TechLayerId layer,
                                           const eccdb::Rect rectangle)
{
  const auto routing_layer = eccdb::TechRoutingLayerId{layer.entity()};
  RoutingShape result;
  Shape shape;
  shape.set_layer_idx(static_cast<int32_t>(layer.packed()));
  shape.set_ll_x(rectangle.ll_x);
  shape.set_ll_y(rectangle.ll_y);
  shape.set_ur_x(rectangle.ur_x);
  shape.set_ur_y(rectangle.ur_y);
  result.set_shape(shape);
  result.set_layer_order(static_cast<int32_t>(physical.routingLevel(routing_layer).value_or(0)));
  return result;
}

void appendVia(NetRoutingData& routing, const eccdb::PhysicalDesignService& physical, const eccdb::PhysicalViaPlacement& placement)
{
  const auto layers = physical.routingViaLayers(placement.tech_via, placement.design_via);
  std::optional<eccdb::Rect> bottom;
  std::optional<eccdb::Rect> top;
  const auto merge = [](std::optional<eccdb::Rect>& target, const eccdb::Rect rectangle) {
    target = target ? target->united(rectangle) : rectangle;
  };
  const auto visitor = eccdb::PhysicalGeometryVisitor{
      .rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
        if (rectangle.layer.entity() == layers.bottom.entity()) {
          merge(bottom, rectangle.rectangle);
        } else if (rectangle.layer.entity() == layers.top.entity()) {
          merge(top, rectangle.rectangle);
        }
      },
      .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
        const auto rectangle = polygonBounds(polygon.points);
        if (polygon.layer.entity() == layers.bottom.entity()) {
          merge(bottom, rectangle);
        } else if (polygon.layer.entity() == layers.top.entity()) {
          merge(top, rectangle);
        }
      }};
  if (placement.tech_via) {
    physical.forEachTechViaGeometry(placement.tech_via, placement.origin, placement.orientation, visitor);
  } else if (placement.design_via) {
    physical.forEachDesignViaGeometry(placement.design_via, placement.origin, placement.orientation, visitor);
  }
  if (!bottom || !top) {
    return;
  }
  RoutingVia via;
  via.set_bottom_routing_shape(toRoutingShape(physical, eccdb::TechLayerId{layers.bottom.entity()}, *bottom));
  via.set_top_routing_shape(toRoutingShape(physical, eccdb::TechLayerId{layers.top.entity()}, *top));
  routing.get_routing_via_list().push_back(std::move(via));
}

void appendRouteShape(NetRoutingData& routing, const eccdb::PhysicalDesignService& physical, const eccdb::DesignRouteShape& shape,
                      const eccdb::DesignRouteShapeAttributes* attributes)
{
  std::visit(
      [&](const auto& geometry) {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
          const int32_t width = attributes != nullptr && (shape.flags & eccdb::DesignWirePathFlag::kHasWidth) != 0u
                                    ? attributes->width
                                    : physical.routingLayer(shape.layer).width;
          routing.get_wire_routing_shape_list().push_back(toRoutingShape(physical, eccdb::TechLayerId{shape.layer.entity()},
                                                                           segmentBounds(geometry.begin.position, geometry.end.position, width)));
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRoutePatch>) {
          routing.get_wire_routing_shape_list().push_back(
              toRoutingShape(physical, eccdb::TechLayerId{shape.layer.entity()}, geometry.rectangle));
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
          appendVia(routing, physical,
                    {.tech_via = geometry.via.tech_via, .design_via = geometry.via.design_via, .origin = geometry.origin.position,
                     .orientation = geometry.via.orientation});
        }
      },
      shape.geometry);
}

void appendPinGeometry(std::vector<RoutingShape>& target, const eccdb::PhysicalDesignService& physical,
                       const std::function<void(const eccdb::PhysicalGeometryVisitor&)>& visit)
{
  visit({.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
           if (physical.layerKind(rectangle.layer) == eccdb::GeometryLayerKind::kRouting) {
             target.push_back(toRoutingShape(physical, rectangle.layer, rectangle.rectangle));
           }
         },
         .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
           if (physical.layerKind(polygon.layer) == eccdb::GeometryLayerKind::kRouting) {
             target.push_back(toRoutingShape(physical, polygon.layer, polygonBounds(polygon.points)));
           }
         }});
}

void appendNetPins(DefData& def_data, const eccdb::PhysicalDesignService& physical, const eccdb::DesignNetId net_id,
                   const std::string& net_name, const bool power, const bool ground)
{
  auto& routing = def_data.get_def_routing_data().get_net_routing_data_map()[net_name];
  std::optional<std::string> input_io_driver;
  physical.forEachNetPin(
      net_id,
      {.instance_pin = [&](const auto pin_id, const auto& instance_pin, const auto& instance, const auto& term) {
         const std::string terminal_name = instanceTerminalName(instance, term);
         const ConnectType type = power ? ConnectType::kPower : (ground ? ConnectType::kGround : ConnectType::kNone);
         def_data.get_terminal_connect_type_map()[terminal_name] = type;
         if (power) {
           def_data.get_def_routing_data().get_power_instance_pin_net_map()[terminal_name] = net_name;
         } else if (ground) {
           def_data.get_def_routing_data().get_ground_instance_pin_net_map()[terminal_name] = net_name;
         }
         if (isDriver(term.direction) && routing.get_driver_terminal_name().empty()) {
           routing.set_driver_terminal_name(terminal_name);
         }
         if (!physical.isPlaced(instance_pin.instance)) {
           return;
         }
         std::vector<RoutingShape> shapes;
         appendPinGeometry(shapes, physical, [&](const eccdb::PhysicalGeometryVisitor& visitor) {
           physical.forEachPlacedInstancePinGeometry(pin_id, visitor);
         });
         if (!shapes.empty()) {
           routing.get_terminal_routing_shape_map()[terminal_name] = std::move(shapes);
         }
       },
       .io_pin = [&](const auto pin_id, const auto& pin) {
         const std::string terminal_name = ioTerminalName(pin.name);
         const ConnectType type = power ? ConnectType::kPower : (ground ? ConnectType::kGround : ConnectType::kNone);
         def_data.get_terminal_connect_type_map()[terminal_name] = type;
         // A top-level input is the source when a regular net has no cell output.
         // Defer assignment until all instance terms have had priority.
         if (!power && !ground && pin.direction == eccdb::DesignIoPinDirection::kInput && !input_io_driver.has_value()) {
           input_io_driver = terminal_name;
         }
         std::vector<RoutingShape> shapes;
         appendPinGeometry(shapes, physical, [&](const eccdb::PhysicalGeometryVisitor& visitor) { physical.forEachIoPinGeometry(pin_id, visitor); });
         if (!shapes.empty()) {
           routing.get_terminal_routing_shape_map()[terminal_name] = std::move(shapes);
         }
       }});
  if (routing.get_driver_terminal_name().empty() && input_io_driver.has_value()) {
    routing.set_driver_terminal_name(*input_io_driver);
  }
}

void appendNetRouting(DefData& def_data, const eccdb::PhysicalDesignService& physical, const eccdb::DesignNetId net_id,
                      const std::string& net_name)
{
  auto& routing = def_data.get_def_routing_data().get_net_routing_data_map()[net_name];
  physical.forEachNetWireShape(net_id, [&](const auto, const auto&, const auto, const auto& shape, const auto* attributes) {
    appendRouteShape(routing, physical, shape, attributes);
  });
  physical.forEachNetGeometry(
      net_id,
      {.rectangle = [&](const eccdb::DesignGeometryRectangle& rectangle) {
         routing.get_wire_routing_shape_list().push_back(toRoutingShape(physical, eccdb::TechLayerId{rectangle.layer.entity()}, rectangle.rectangle));
       },
       .polygon = [&](const eccdb::DesignGeometryPolygon& polygon) {
         routing.get_wire_routing_shape_list().push_back(
             toRoutingShape(physical, eccdb::TechLayerId{polygon.layer.entity()}, polygonBounds(polygon.points)));
       },
       .via = [&](const eccdb::DesignGeometryNetVia& via) {
         for (const auto origin : via.origins) {
           appendVia(routing, physical,
                     {.tech_via = via.tech_via, .design_via = via.design_via, .origin = origin, .orientation = via.orientation});
         }
       }});
}

}  // namespace

void LVSInterface::wrapDatabaseFromEccdb()
{
  if (_database_context == nullptr || _database_context->technology() == nullptr || _database_context->library() == nullptr) {
    LVSLOG.error(Loc::current(), "Native iLVS requires EccDB Design, Tech, and Library stores.");
    return;
  }
  if (_reference_netlist == nullptr) {
    LVSLOG.error(Loc::current(), "Native iLVS requires an external reference netlist; it will not fall back to iDB.");
    return;
  }

  const eccdb::PhysicalDesignService physical{*_database_context};
  const auto& design = _database_context->design();
  const auto& netlist = design.netlistStorage();
  DefData def_data;
  if (physical.hasDesignInfo()) {
    def_data.set_design_name(design.globalStorage().info().name);
  }
  if (physical.hasDieArea()) {
    const auto die = physical.dieBounds();
    def_data.get_die().set_real_ll(die.ll_x, die.ll_y);
    def_data.get_die().set_real_ur(die.ur_x, die.ur_y);
  }

  netlist.forEachInstance([&](const auto, const auto& instance, const auto) { def_data.get_instance_name_set().insert(instance.name); });
  netlist.forEachIoPin([&](const auto, const auto& pin) {
    const std::string terminal_name = ioTerminalName(pin.name);
    def_data.get_io_terminal_name_list().push_back(terminal_name);
    def_data.get_terminal_connect_type_map()[terminal_name] = ConnectType::kNone;
  });

  std::unordered_map<std::string, std::string> physical_net_names;
  physical.forEachRegularNet([&](const auto net_id, const auto& net) {
    const auto net_name = lvsNetName(net.name);
    registerLvsNetName(physical_net_names, net.name, net_name);
    Net target;
    physical.forEachNetPin(
        net_id,
        {.instance_pin = [&](const auto, const auto&, const auto& instance, const auto& term) {
           target.get_terminal_name_list().push_back(instanceTerminalName(instance, term));
         },
         .io_pin = [&](const auto, const auto& pin) { target.get_terminal_name_list().push_back(ioTerminalName(pin.name)); }});
    target.get_terminal_name_list() = LVSUTIL.getSortedUniqueList(std::move(target.get_terminal_name_list()));
    def_data.get_net_map()[net_name] = std::move(target);
    appendNetPins(def_data, physical, net_id, net_name, false, false);
    appendNetRouting(def_data, physical, net_id, net_name);
  });

  physical.forEachSpecialNet([&](const auto net_id, const auto& net) {
    const bool power = isPower(net.use);
    const bool ground = isGround(net.use);
    if (power) {
      def_data.get_def_routing_data().get_power_net_name_set().insert(net.name);
    } else if (ground) {
      def_data.get_def_routing_data().get_ground_net_name_set().insert(net.name);
    }
    appendNetPins(def_data, physical, net_id, net.name, power, ground);
    appendNetRouting(def_data, physical, net_id, net.name);
  });

  def_data.normalize();
  NetlistData reference = *_reference_netlist;
  if (!reference.get_design_name().empty() && !def_data.get_design_name().empty() && reference.get_design_name() != def_data.get_design_name()) {
    LVSLOG.error(Loc::current(), "Native iLVS reference netlist design differs from EccDB: netlist='", reference.get_design_name(), "' design='",
                 def_data.get_design_name(), "'.");
  }
  auto& database = LVSDM.getDatabase();
  database.set_netlist_data(std::move(reference));
  database.set_def_data(std::move(def_data));
  LVSLOG.info(Loc::current(), "Wrapped native EccDB physical design: def_nets=", database.get_def_data().get_net_map().size(),
              " routing_nets=", database.get_def_data().get_def_routing_data().get_net_routing_data_map().size(), ".");
}

}  // namespace ilvs
