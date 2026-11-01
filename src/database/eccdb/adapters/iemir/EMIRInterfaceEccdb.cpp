// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#include "EMIRInterfaceEccdb.h"

#include "EMIRInterface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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
#include "storage/library/LibraryStore.h"
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
  std::optional<eccdb::Rect> cut_bounds;
  int32_t cut_count = 0;
  double cut_area_um2 = 0.0;
};

void includeBounds(std::optional<eccdb::Rect>& bounds, const eccdb::Rect rectangle)
{
  if (!bounds) {
    bounds = rectangle;
    return;
  }
  bounds->ll_x = std::min(bounds->ll_x, rectangle.ll_x);
  bounds->ll_y = std::min(bounds->ll_y, rectangle.ll_y);
  bounds->ur_x = std::max(bounds->ur_x, rectangle.ur_x);
  bounds->ur_y = std::max(bounds->ur_y, rectangle.ur_y);
}

[[nodiscard]] double viaResistance(const eccdb::PhysicalDesignService& physical, const eccdb::PhysicalViaPlacement& placement,
                                   const ViaData& data)
{
  const auto generatedRuleResistance = [&](const eccdb::TechViaRuleGenerateId rule) {
    if (!rule || data.cut_count <= 0) {
      return 0.0;
    }
    const auto resistance = physical.techViaRuleGenerateCutLayer(rule).resistance_per_cut;
    return resistance > 0.0 ? resistance / data.cut_count : 0.0;
  };

  bool generated = false;
  if (placement.tech_via) {
    const auto& storage = physical.tech().viaMasterStorage();
    const auto& via = storage.viaMaster(placement.tech_via);
    if (via.resistance > 0.0) {
      return via.resistance;
    }
    if (storage.hasGeneratedViaMaster(placement.tech_via)) {
      generated = true;
      if (const auto resistance = generatedRuleResistance(storage.generatedViaMaster(placement.tech_via).via_rule_generate); resistance > 0.0) {
        return resistance;
      }
    }
  }
  if (placement.design_via) {
    const auto& via = physical.design().routingStorage().via(placement.design_via);
    if ((via.flags & eccdb::DesignViaFlag::kGenerated) != 0u) {
      generated = true;
      if (const auto resistance = generatedRuleResistance(via.generated.via_rule); resistance > 0.0) {
        return resistance;
      }
    }
  }

  // DEF generated VIAs often inherit a VIARULE without a resistance field.
  // Match the main iDB fallback: use an unambiguous fixed DEFAULT one-cut
  // technology VIA on the same layer triplet. No synthetic resistance is made.
  if (generated && data.cut_layer && data.cut_count > 0) {
    const auto layers = physical.routingViaLayers(placement.tech_via, placement.design_via);
    const auto& technology = physical.tech();
    const auto& storage = technology.viaMasterStorage();
    double resistance_per_cut = 0.0;
    bool ambiguous = false;
    physical.forEachTechVia([&](const eccdb::TechViaMasterId candidate_id, const eccdb::TechViaMaster& candidate,
                                const eccdb::TechViaGeometry& geometry) {
      if (ambiguous || !storage.hasFixedViaMaster(candidate_id) || (candidate.flags & eccdb::TechViaMasterFlag::kDefault) == 0u
          || candidate.resistance <= 0.0 || geometry.cut_layer.entity() != data.cut_layer->entity()
          || storage.cutRects(candidate_id).size() != 1u) {
        return;
      }
      const auto candidate_layers = physical.routingViaLayers(candidate_id, {});
      if (candidate_layers.bottom.entity() != layers.bottom.entity() || candidate_layers.top.entity() != layers.top.entity()) {
        return;
      }
      if (resistance_per_cut == 0.0) {
        resistance_per_cut = candidate.resistance;
      } else if (std::abs(resistance_per_cut - candidate.resistance) > EMIR_ERROR) {
        ambiguous = true;
      }
    });
    if (!ambiguous && resistance_per_cut > 0.0) {
      return resistance_per_cut / data.cut_count;
    }
  }
  return 0.0;
}

void appendVia(PowerNet& power_net, const eccdb::PhysicalDesignService& physical, const eccdb::PhysicalViaPlacement& placement)
{
  const auto layers = physical.routingViaLayers(placement.tech_via, placement.design_via);
  const auto bottom_layer = eccdb::TechLayerId{layers.bottom.entity()};
  const auto top_layer = eccdb::TechLayerId{layers.top.entity()};
  ViaData data;
  PowerVia target;
  target.set_via_name(std::string{physical.viaName(placement.tech_via, placement.design_via)});
  target.set_bottom_layer_name(physical.layerInfo(bottom_layer).name);
  target.set_top_layer_name(physical.layerInfo(top_layer).name);
  target.set_bottom_layer_idx(layerIndex(bottom_layer));
  target.set_top_layer_idx(layerIndex(top_layer));
  target.set_x(placement.origin.x);
  target.set_y(placement.origin.y);
  const int32_t dbu_per_micron = physical.databaseUnitsPerMicron();
  const auto append_cut_rectangle = [&](const eccdb::TechLayerId layer, const eccdb::Rect rectangle) {
    data.cut_layer = layer;
    ++data.cut_count;
    includeBounds(data.cut_bounds, rectangle);
    if (dbu_per_micron > 0) {
      data.cut_area_um2 += static_cast<double>(rectangle.ur_x - rectangle.ll_x) * static_cast<double>(rectangle.ur_y - rectangle.ll_y)
                           / static_cast<double>(dbu_per_micron) / static_cast<double>(dbu_per_micron);
    }
  };
  const auto append_metal_rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
    if (rectangle.layer.entity() != layers.bottom.entity() && rectangle.layer.entity() != layers.top.entity()) {
      append_cut_rectangle(rectangle.layer, rectangle.rectangle);
    }
  };
  const eccdb::PhysicalGeometryVisitor visitor{.rectangle = append_metal_rectangle};
  if (placement.tech_via) {
    physical.forEachTechViaGeometry(placement.tech_via, placement.origin, placement.orientation, visitor);
  } else if (placement.design_via) {
    physical.forEachDesignViaGeometry(placement.design_via, placement.origin, placement.orientation, visitor);
  }
  if (data.cut_layer) {
    target.set_cut_layer_name(physical.layerInfo(*data.cut_layer).name);
  }
  target.set_cut_num(data.cut_count);
  if (data.cut_bounds) {
    target.set_cut_low_x(data.cut_bounds->ll_x - placement.origin.x);
    target.set_cut_low_y(data.cut_bounds->ll_y - placement.origin.y);
    target.set_cut_high_x(data.cut_bounds->ur_x - placement.origin.x);
    target.set_cut_high_y(data.cut_bounds->ur_y - placement.origin.y);
  }
  target.set_cut_area_um2(data.cut_area_um2);
  target.set_resistance(viaResistance(physical, placement, data));
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

void appendPin(PowerNet& power_net, const uint64_t instance_id, const eccdb::DesignInstance& instance,
               const eccdb::LibraryMasterTerm& term, const eccdb::PhysicalLayerRect& rectangle)
{
  PowerPin target;
  target.set_instance_id(instance_id);
  target.set_pin_name(instance.name + ":" + term.name);
  target.set_layer_idx(layerIndex(rectangle.layer));
  target.set_x((rectangle.rectangle.ll_x + rectangle.rectangle.ur_x) / 2);
  target.set_y((rectangle.rectangle.ll_y + rectangle.rectangle.ur_y) / 2);
  target.set_low_x(rectangle.rectangle.ll_x);
  target.set_low_y(rectangle.rectangle.ll_y);
  target.set_high_x(rectangle.rectangle.ur_x);
  target.set_high_y(rectangle.rectangle.ur_y);
  target.set_is_source(false);
  power_net.get_pin_list().push_back(std::move(target));
}

void appendIoPin(PowerNet& power_net, const eccdb::DesignIoPin& pin, const eccdb::PhysicalLayerRect& rectangle)
{
  PowerPin target;
  target.set_pin_name(pin.name);
  target.set_layer_idx(layerIndex(rectangle.layer));
  target.set_x((rectangle.rectangle.ll_x + rectangle.rectangle.ur_x) / 2);
  target.set_y((rectangle.rectangle.ll_y + rectangle.rectangle.ur_y) / 2);
  target.set_low_x(rectangle.rectangle.ll_x);
  target.set_low_y(rectangle.rectangle.ll_y);
  target.set_high_x(rectangle.rectangle.ur_x);
  target.set_high_y(rectangle.rectangle.ur_y);
  target.set_is_source(true);
  power_net.get_pin_list().push_back(std::move(target));
}

[[nodiscard]] eccdb::Rect placedBounds(const eccdb::PhysicalDesignService& physical, const eccdb::DesignInstanceId instance,
                                       const eccdb::Rect rectangle)
{
  const std::array corners{eccdb::Point{.x = rectangle.ll_x, .y = rectangle.ll_y},
                           eccdb::Point{.x = rectangle.ll_x, .y = rectangle.ur_y},
                           eccdb::Point{.x = rectangle.ur_x, .y = rectangle.ll_y},
                           eccdb::Point{.x = rectangle.ur_x, .y = rectangle.ur_y}};
  const auto first = physical.placeInstanceMasterPoint(instance, corners.front());
  eccdb::Rect result{.ll_x = first.x, .ll_y = first.y, .ur_x = first.x, .ur_y = first.y};
  for (const auto point : corners) {
    const auto transformed = physical.placeInstanceMasterPoint(instance, point);
    result.ll_x = std::min(result.ll_x, transformed.x);
    result.ll_y = std::min(result.ll_y, transformed.y);
    result.ur_x = std::max(result.ur_x, transformed.x);
    result.ur_y = std::max(result.ur_y, transformed.y);
  }
  return result;
}

// DesignBlockPin currently preserves a DEF PORT boundary, but not repeated
// LAYER-clause ordinals within that PORT. This is therefore the closest main
// projection possible without changing EccDB storage: one bbox per layer.
struct PinLayerBox
{
  eccdb::TechLayerId layer;
  eccdb::Rect bounds;
};

void addPinLayerBox(std::vector<PinLayerBox>& boxes, const eccdb::PhysicalLayerRect& rectangle)
{
  const auto iter = std::find_if(boxes.begin(), boxes.end(), [&](const PinLayerBox& item) {
    return item.layer.entity() == rectangle.layer.entity();
  });
  if (iter == boxes.end()) {
    boxes.push_back({rectangle.layer, rectangle.rectangle});
  } else {
    iter->bounds = iter->bounds.united(rectangle.rectangle);
  }
}

}  // namespace

static void populateEmirDatabaseFromEccdb(eccdb::DatabaseContext& context)
{
  if (context.technology() == nullptr || context.library() == nullptr) {
    EMIRLOG.error(Loc::current(), "Native iEMIR requires EccDB Design, Tech, and Library stores.");
    return;
  }

  const eccdb::PhysicalDesignService physical{context};
  const auto& design = context.design();
  const auto& netlist = design.netlistStorage();
  auto& database = EMIRDM.getDatabase();
  database.get_instance_id_set().clear();
  database.get_instance_name_to_id_map().clear();
  database.get_power_net_map().clear();
  database.get_instance_power_map().clear();
  database.get_power_graph_map().clear();
  if (physical.hasDesignInfo()) {
    database.set_design_name(design.globalStorage().info().name);
    database.set_micron_dbu(physical.databaseUnitsPerMicron());
  }
  netlist.forEachInstance([&](const auto id, const auto& instance, const auto) {
    const auto instance_id = static_cast<uint64_t>(id.packed());
    database.get_instance_id_set().insert(instance_id);
    database.get_instance_name_to_id_map()[instance.name] = instance_id;
  });

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
             const auto& port = physical.library().masterPortStorage().masterPin(master_pin);
             const auto& geometry = physical.library().geometryStorage();
             for (const auto& clause : port.layer_clauses) {
               std::optional<eccdb::Rect> bounds;
               for (const auto shape : clause.geometry) {
                 if (!geometry.geometryRegistry().template has<eccdb::GeometryRectangle>(shape)) {
                   continue;
                 }
                 const auto placed = placedBounds(physical, instance_pin.instance, geometry.rectangle(shape).rectangle);
                 includeBounds(bounds, placed);
               }
               if (bounds) {
                 appendPin(target, static_cast<uint64_t>(instance_pin.instance.packed()), instance, term,
                           eccdb::PhysicalLayerRect{.layer = clause.layer, .rectangle = *bounds});
               }
             }
           }
         },
         .io_pin = [&](const auto pin_id, const auto& pin) {
           if (!EMIRDM.getConfig().add_ploc_from_top_def) {
             return;
           }
           for (const auto block_pin : physical.blockPins(pin_id)) {
             std::vector<PinLayerBox> boxes;
             physical.forEachBlockPinGeometry(
                 block_pin,
                 {.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
                    addPinLayerBox(boxes, rectangle);
                  }});
             for (const auto& box : boxes) {
               appendIoPin(target, pin, eccdb::PhysicalLayerRect{.layer = box.layer, .rectangle = box.bounds});
             }
           }
         }});
    if (!target.get_wire_segment_list().empty() || !target.get_via_list().empty() || !target.get_pin_list().empty()) {
      database.get_power_net_map()[target.get_net_name()] = std::move(target);
    }
  });
  EMIRLOG.info(Loc::current(), "Wrapped native EccDB power nets: ", database.get_power_net_map().size(), ".");
}

}  // namespace iemir

namespace eccdb::adapters {

void bindEmirSource(iemir::EMIRInterface& interface, DatabaseContext* context)
{
  if (context == nullptr) {
    interface.setDatabaseWrapper({});
    return;
  }
  interface.setDatabaseWrapper([context] { iemir::populateEmirDatabaseFromEccdb(*context); });
}

}  // namespace eccdb::adapters
