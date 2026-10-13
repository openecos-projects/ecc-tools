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

#include "EMIRInterface.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

#include "DataManager.hpp"
#include "Database.hpp"
#include "Logger.hpp"
#include "PowerNet.hpp"
#include "PowerNetType.hpp"
#include "PowerPin.hpp"
#include "PowerVia.hpp"
#include "PowerWireSegment.hpp"
#include "service/context/DatabaseContext.h"
#include "service/geometry/PhysicalDesignService.h"
#include "storage/design/DesignStore.h"
#include "storage/tech/TechStore.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace iemir {
namespace {

[[nodiscard]] PowerNetType toPowerNetType(const eccdb::DesignSignalUse use)
{
  if (use == eccdb::DesignSignalUse::kPower) {
    return PowerNetType::kPower;
  }
  if (use == eccdb::DesignSignalUse::kGround) {
    return PowerNetType::kGround;
  }
  return PowerNetType::kNone;
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

[[nodiscard]] int32_t layerIndex(const eccdb::TechLayerId layer)
{
  return static_cast<int32_t>(layer.packed());
}

void appendSegment(PowerNet& power_net, const eccdb::PhysicalDesignService& physical, const eccdb::TechRoutingLayerId layer,
                   const eccdb::Point begin, const eccdb::Point end, const int32_t width)
{
  if (begin.x != end.x && begin.y != end.y) {
    EMIRLOG.warn(Loc::current(), "Native EMIR skips non-Manhattan power segment on layer ", physical.layerInfo(eccdb::TechLayerId{layer.entity()}).name,
                 ".");
    return;
  }
  const auto& routing = physical.routingLayer(layer);
  PowerWireSegment target;
  target.set_layer_idx(layerIndex(eccdb::TechLayerId{layer.entity()}));
  target.set_layer_name(physical.layerInfo(eccdb::TechLayerId{layer.entity()}).name);
  target.set_first_x(begin.x);
  target.set_first_y(begin.y);
  target.set_second_x(end.x);
  target.set_second_y(end.y);
  target.set_width(width > 0 ? width : routing.width);
  target.set_resistance_per_square(routing.resistance);
  power_net.get_wire_segment_list().push_back(std::move(target));
}

void appendRectangle(PowerNet& power_net, const eccdb::PhysicalDesignService& physical, const eccdb::TechRoutingLayerId layer,
                     const eccdb::Rect rectangle)
{
  const int32_t width_x = rectangle.ur_x - rectangle.ll_x;
  const int32_t width_y = rectangle.ur_y - rectangle.ll_y;
  if (width_x >= width_y) {
    appendSegment(power_net, physical, layer, {.x = rectangle.ll_x, .y = rectangle.ll_y + width_y / 2},
                  {.x = rectangle.ur_x, .y = rectangle.ll_y + width_y / 2}, width_y);
  } else {
    appendSegment(power_net, physical, layer, {.x = rectangle.ll_x + width_x / 2, .y = rectangle.ll_y},
                  {.x = rectangle.ll_x + width_x / 2, .y = rectangle.ur_y}, width_x);
  }
}

struct ViaData
{
  std::optional<eccdb::TechLayerId> cut_layer;
  int32_t cut_count = 0;
};

[[nodiscard]] ViaData viaData(const eccdb::PhysicalDesignService& physical, const eccdb::PhysicalViaPlacement& placement)
{
  ViaData result;
  const auto layers = physical.routingViaLayers(placement.tech_via, placement.design_via);
  const auto visit = [&](const eccdb::TechLayerId layer) {
    if (layer.entity() != layers.bottom.entity() && layer.entity() != layers.top.entity()) {
      result.cut_layer = layer;
      ++result.cut_count;
    }
  };
  const auto visitor = eccdb::PhysicalGeometryVisitor{
      .rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) { visit(rectangle.layer); },
      .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) { visit(polygon.layer); }};
  if (placement.tech_via) {
    physical.forEachTechViaGeometry(placement.tech_via, placement.origin, placement.orientation, visitor);
  } else if (placement.design_via) {
    physical.forEachDesignViaGeometry(placement.design_via, placement.origin, placement.orientation, visitor);
  }
  return result;
}

[[nodiscard]] double viaResistance(const eccdb::PhysicalDesignService& physical, const eccdb::PhysicalViaPlacement& placement,
                                   const ViaData& data)
{
  if (placement.tech_via) {
    const auto& via = physical.tech().viaMasterStorage().viaMaster(placement.tech_via);
    if (via.resistance > 0.0) {
      return via.resistance;
    }
  }
  if (placement.design_via) {
    const auto& via = physical.design().routingStorage().via(placement.design_via);
    if ((via.flags & eccdb::DesignViaFlag::kGenerated) != 0u && via.generated.via_rule && data.cut_count > 0) {
      const auto& technology = physical.tech();
      const auto& registry = technology.techRegistry().registry();
      if (registry.valid(via.generated.via_rule.entity())
          && registry.all_of<eccdb::TechViaRuleGenerateCutLayer>(via.generated.via_rule.entity())) {
        const auto& cut = registry.get<eccdb::TechViaRuleGenerateCutLayer>(via.generated.via_rule.entity());
        if (cut.resistance_per_cut > 0.0) {
          return cut.resistance_per_cut / data.cut_count;
        }
      }
    }
  }
  if (data.cut_layer && data.cut_count > 0) {
    const auto cut = eccdb::TechCutLayerId{data.cut_layer->entity()};
    const auto& cut_data = physical.cutLayer(cut);
    if (cut_data.resistance_per_cut > 0.0) {
      return cut_data.resistance_per_cut / data.cut_count;
    }
  }

  // DEF generated VIAs often inherit a VIARULE without a resistance field.
  // Match the legacy iDB path: use an unambiguous, fixed DEFAULT one-cut VIA
  // on the same technology layer triplet. This reads a declared PDK VIA
  // resistance; it never substitutes a synthetic value.
  if (placement.design_via && data.cut_layer && data.cut_count > 0) {
    const auto& definition = physical.design().routingStorage().via(placement.design_via);
    if ((definition.flags & eccdb::DesignViaFlag::kGenerated) != 0u) {
      const auto layers = physical.routingViaLayers({}, placement.design_via);
      const auto& technology = physical.tech();
      double resistance_per_cut = 0.0;
      bool ambiguous = false;
      physical.forEachTechVia([&](const eccdb::TechViaMasterId candidate_id, const eccdb::TechViaMaster& candidate,
                                  const eccdb::TechViaGeometry&) {
        if (ambiguous || !technology.viaMasterStorage().hasFixedViaMaster(candidate_id)
            || (candidate.flags & eccdb::TechViaMasterFlag::kDefault) == 0u || candidate.resistance <= 0.0) {
          return;
        }
        const auto candidate_layers = physical.routingViaLayers(candidate_id, {});
        if (candidate_layers.bottom.entity() != layers.bottom.entity() || candidate_layers.top.entity() != layers.top.entity()) {
          return;
        }

        int32_t candidate_cut_count = 0;
        bool same_cut_layer = true;
        physical.forEachTechViaGeometry(candidate_id, {.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
                                                         if (rectangle.layer.entity() != layers.bottom.entity()
                                                             && rectangle.layer.entity() != layers.top.entity()) {
                                                           ++candidate_cut_count;
                                                           same_cut_layer = same_cut_layer && rectangle.layer.entity() == data.cut_layer->entity();
                                                         }
                                                       },
                                                       .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
                                                         if (polygon.layer.entity() != layers.bottom.entity()
                                                             && polygon.layer.entity() != layers.top.entity()) {
                                                           ++candidate_cut_count;
                                                           same_cut_layer = same_cut_layer && polygon.layer.entity() == data.cut_layer->entity();
                                                         }
                                                       }});
        if (!same_cut_layer || candidate_cut_count != 1) {
          return;
        }
        if (resistance_per_cut == 0.0) {
          resistance_per_cut = candidate.resistance;
        } else if (std::abs(resistance_per_cut - candidate.resistance) > 1e-9) {
          ambiguous = true;
        }
      });
      if (!ambiguous && resistance_per_cut > 0.0) {
        return resistance_per_cut / data.cut_count;
      }
    }
  }
  return 0.0;
}

void appendVia(PowerNet& power_net, const eccdb::PhysicalDesignService& physical, const eccdb::PhysicalViaPlacement& placement)
{
  const auto layers = physical.routingViaLayers(placement.tech_via, placement.design_via);
  const auto data = viaData(physical, placement);
  PowerVia target;
  target.set_bottom_layer_idx(layerIndex(eccdb::TechLayerId{layers.bottom.entity()}));
  target.set_top_layer_idx(layerIndex(eccdb::TechLayerId{layers.top.entity()}));
  target.set_x(placement.origin.x);
  target.set_y(placement.origin.y);
  target.set_cut_num(data.cut_count);
  target.set_resistance(viaResistance(physical, placement, data));
  const auto append_metal_rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
    const PowerViaMetalRect target_rectangle{.ll_x = rectangle.rectangle.ll_x,
                                              .ll_y = rectangle.rectangle.ll_y,
                                              .ur_x = rectangle.rectangle.ur_x,
                                              .ur_y = rectangle.rectangle.ur_y};
    if (rectangle.layer.entity() == layers.bottom.entity()) {
      target.add_bottom_rect(target_rectangle);
    } else if (rectangle.layer.entity() == layers.top.entity()) {
      target.add_top_rect(target_rectangle);
    }
  };
  const eccdb::PhysicalGeometryVisitor visitor{.rectangle = append_metal_rectangle};
  if (placement.tech_via) {
    physical.forEachTechViaGeometry(placement.tech_via, placement.origin, placement.orientation, visitor);
  } else if (placement.design_via) {
    physical.forEachDesignViaGeometry(placement.design_via, placement.origin, placement.orientation, visitor);
  }
  power_net.get_via_list().push_back(std::move(target));
}

void appendRouteShape(PowerNet& power_net, const eccdb::PhysicalDesignService& physical, const eccdb::DesignRouteShape& shape,
                      const eccdb::DesignRouteShapeAttributes* attributes)
{
  std::visit(
      [&](const auto& geometry) {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
          const int32_t width = attributes != nullptr && (shape.flags & eccdb::DesignWirePathFlag::kHasWidth) != 0u
                                    ? attributes->width
                                    : physical.routingLayer(shape.layer).width;
          appendSegment(power_net, physical, shape.layer, geometry.begin.position, geometry.end.position, width);
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRoutePatch>) {
          appendRectangle(power_net, physical, shape.layer, geometry.rectangle);
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
          appendVia(power_net, physical,
                    {.tech_via = geometry.via.tech_via, .design_via = geometry.via.design_via, .origin = geometry.origin.position,
                     .orientation = geometry.via.orientation});
        }
      },
      shape.geometry);
}

void appendPin(PowerNet& power_net, const uint64_t instance_id, const uint64_t port_id, const eccdb::DesignInstance& instance,
               const eccdb::LibraryMasterTerm& term, const eccdb::PhysicalLayerRect& rectangle)
{
  PowerPin target;
  target.set_instance_id(instance_id);
  target.set_pin_name(instance.name + ":" + term.name);
  target.set_layer_idx(layerIndex(rectangle.layer));
  target.set_bounds(rectangle.rectangle.ll_x, rectangle.rectangle.ll_y, rectangle.rectangle.ur_x, rectangle.rectangle.ur_y);
  target.set_port_id(port_id);
  target.set_is_source(false);
  power_net.get_pin_list().push_back(std::move(target));
}

void appendPin(PowerNet& power_net, const uint64_t instance_id, const uint64_t port_id, const eccdb::DesignInstance& instance,
               const eccdb::LibraryMasterTerm& term, const eccdb::PhysicalLayerPolygon& polygon)
{
  const auto rectangle = polygonBounds(polygon.points);
  appendPin(power_net, instance_id, port_id, instance, term, eccdb::PhysicalLayerRect{.layer = polygon.layer, .rectangle = rectangle});
}

void appendIoPin(PowerNet& power_net, const uint64_t port_id, const eccdb::DesignIoPin& pin, const eccdb::PhysicalLayerRect& rectangle)
{
  PowerPin target;
  target.set_pin_name(pin.name);
  target.set_layer_idx(layerIndex(rectangle.layer));
  target.set_bounds(rectangle.rectangle.ll_x, rectangle.rectangle.ll_y, rectangle.rectangle.ur_x, rectangle.rectangle.ur_y);
  target.set_port_id(port_id);
  target.set_is_source(true);
  power_net.get_pin_list().push_back(std::move(target));
}

void appendIoPin(PowerNet& power_net, const uint64_t port_id, const eccdb::DesignIoPin& pin, const eccdb::PhysicalLayerPolygon& polygon)
{
  const auto rectangle = polygonBounds(polygon.points);
  appendIoPin(power_net, port_id, pin, eccdb::PhysicalLayerRect{.layer = polygon.layer, .rectangle = rectangle});
}

}  // namespace

void EMIRInterface::wrapDatabaseFromEccdb()
{
  if (_database_context == nullptr || _database_context->technology() == nullptr || _database_context->library() == nullptr) {
    EMIRLOG.error(Loc::current(), "Native iEMIR requires EccDB Design, Tech, and Library stores.");
    return;
  }

  const eccdb::PhysicalDesignService physical{*_database_context};
  const auto& design = _database_context->design();
  const auto& netlist = design.netlistStorage();
  auto& database = EMIRDM.getDatabase();
  database.get_instance_id_set().clear();
  database.get_power_net_map().clear();
  database.get_instance_power_map().clear();
  database.get_power_graph_map().clear();
  if (physical.hasDesignInfo()) {
    database.set_design_name(design.globalStorage().info().name);
    database.set_micron_dbu(physical.databaseUnitsPerMicron());
  }
  netlist.forEachInstance([&](const auto id, const auto&, const auto) { database.get_instance_id_set().insert(static_cast<uint64_t>(id.packed())); });

  physical.forEachSpecialNet([&](const auto net_id, const auto& net) {
    const auto type = toPowerNetType(net.use);
    if (type == PowerNetType::kNone) {
      return;
    }
    PowerNet target;
    target.set_net_name(net.name);
    target.set_type(type);
    physical.forEachNetWireShape(net_id, [&](const auto, const auto&, const auto, const auto& shape, const auto* attributes) {
      appendRouteShape(target, physical, shape, attributes);
    });
    physical.forEachNetGeometry(
        net_id,
        {.rectangle = [&](const eccdb::DesignGeometryRectangle& rectangle) { appendRectangle(target, physical, rectangle.layer, rectangle.rectangle); },
         .polygon = [&](const eccdb::DesignGeometryPolygon& polygon) {
           EMIRLOG.warn(Loc::current(), "Native EMIR skips non-rectangular special-net polygon on ", net.name, ".");
         },
         .via = [&](const eccdb::DesignGeometryNetVia& via) {
           for (const auto origin : via.origins) {
             appendVia(target, physical, {.tech_via = via.tech_via, .design_via = via.design_via, .origin = origin, .orientation = via.orientation});
           }
         }});
    physical.forEachNetPin(
        net_id,
        {.instance_pin = [&](const auto pin_id, const auto& instance_pin, const auto& instance, const auto& term) {
           if (!physical.isPlaced(instance_pin.instance)) {
             return;
           }
           for (const auto master_pin : physical.masterPins(instance_pin.master_term)) {
             const auto port_id = static_cast<uint64_t>(master_pin.packed());
             physical.forEachPlacedInstanceMasterPinGeometry(
                 pin_id, master_pin,
                 {.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
                    if (physical.layerKind(rectangle.layer) == eccdb::GeometryLayerKind::kRouting) {
                      appendPin(target, static_cast<uint64_t>(instance_pin.instance.packed()), port_id, instance, term, rectangle);
                    }
                  },
                  .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
                    if (physical.layerKind(polygon.layer) == eccdb::GeometryLayerKind::kRouting) {
                      appendPin(target, static_cast<uint64_t>(instance_pin.instance.packed()), port_id, instance, term, polygon);
                    }
                  }});
           }
         },
         .io_pin = [&](const auto pin_id, const auto& pin) {
           for (const auto block_pin : physical.blockPins(pin_id)) {
             const auto port_id = static_cast<uint64_t>(block_pin.packed());
             physical.forEachBlockPinGeometry(
                 block_pin,
                 {.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
                    if (physical.layerKind(rectangle.layer) == eccdb::GeometryLayerKind::kRouting) appendIoPin(target, port_id, pin, rectangle);
                  },
                  .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
                    if (physical.layerKind(polygon.layer) == eccdb::GeometryLayerKind::kRouting) appendIoPin(target, port_id, pin, polygon);
                  }});
           }
         }});
    if (!target.get_wire_segment_list().empty() || !target.get_via_list().empty() || !target.get_pin_list().empty()) {
      database.get_power_net_map()[target.get_net_name()] = std::move(target);
    }
  });
  EMIRLOG.info(Loc::current(), "Wrapped native EccDB power nets: ", database.get_power_net_map().size(), ".");
}

}  // namespace iemir
