// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
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

#include "FPInterface.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "DataManager.hpp"
#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/via/component/ViaComponents.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "service/connectivity/ConnectivityService.h"
#include "service/floorplan/FloorplanService.h"
#include "service/geometry/PhysicalDesignService.h"
#include "service/instance/InstanceService.h"
#include "service/routing/RoutingService.h"
#include "tech/TechStore.h"
#include "tech/cut_layer/model/CutRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace ifp {
namespace {

PlacementOrientation toFpOrientation(eccdb::DesignOrientation orientation) noexcept
{
  switch (orientation) {
    case eccdb::DesignOrientation::kN:
      return PlacementOrientation::kN;
    case eccdb::DesignOrientation::kS:
      return PlacementOrientation::kS;
    case eccdb::DesignOrientation::kE:
      return PlacementOrientation::kE;
    case eccdb::DesignOrientation::kW:
      return PlacementOrientation::kW;
    case eccdb::DesignOrientation::kFN:
      return PlacementOrientation::kFN;
    case eccdb::DesignOrientation::kFS:
      return PlacementOrientation::kFS;
    case eccdb::DesignOrientation::kFE:
      return PlacementOrientation::kFE;
    case eccdb::DesignOrientation::kFW:
      return PlacementOrientation::kFW;
  }
  return PlacementOrientation::kNone;
}

eccdb::DesignOrientation toDesignOrientation(PlacementOrientation orientation)
{
  switch (orientation) {
    case PlacementOrientation::kN:
      return eccdb::DesignOrientation::kN;
    case PlacementOrientation::kS:
      return eccdb::DesignOrientation::kS;
    case PlacementOrientation::kE:
      return eccdb::DesignOrientation::kE;
    case PlacementOrientation::kW:
      return eccdb::DesignOrientation::kW;
    case PlacementOrientation::kFN:
      return eccdb::DesignOrientation::kFN;
    case PlacementOrientation::kFS:
      return eccdb::DesignOrientation::kFS;
    case PlacementOrientation::kFE:
      return eccdb::DesignOrientation::kFE;
    case PlacementOrientation::kFW:
      return eccdb::DesignOrientation::kFW;
    case PlacementOrientation::kNone:
      return eccdb::DesignOrientation::kN;
  }
  return eccdb::DesignOrientation::kN;
}

bool isMacro(eccdb::LibraryCellMasterType type) noexcept
{
  using Type = eccdb::LibraryCellMasterType;
  return type == Type::kBlock || type == Type::kBlockBlackbox || type == Type::kBlockSoft;
}

bool isPad(eccdb::LibraryCellMasterType type) noexcept
{
  using Type = eccdb::LibraryCellMasterType;
  return type == Type::kPad || type == Type::kPadInput || type == Type::kPadOutput || type == Type::kPadInOut
         || type == Type::kPadPower;
}

bool isPadFiller(eccdb::LibraryCellMasterType type) noexcept
{
  return type == eccdb::LibraryCellMasterType::kPadSpacer;
}

bool isSpacer(eccdb::LibraryCellMasterType type) noexcept
{
  return type == eccdb::LibraryCellMasterType::kCoreSpacer;
}

bool hasArea(const PlanarRect& rect) noexcept
{
  return rect.get_ur_x() > rect.get_ll_x() && rect.get_ur_y() > rect.get_ll_y();
}

eccdb::Rect toRect(const PlanarRect& rect)
{
  return {.ll_x = rect.get_ll_x(), .ll_y = rect.get_ll_y(), .ur_x = rect.get_ur_x(), .ur_y = rect.get_ur_y()};
}

std::vector<eccdb::Point> rectangleBoundary(const PlanarRect& rect)
{
  if (!hasArea(rect)) {
    throw std::invalid_argument("iFP native floorplan output requires an area rectangle");
  }
  return {{rect.get_ll_x(), rect.get_ll_y()}, {rect.get_ur_x(), rect.get_ll_y()},
          {rect.get_ur_x(), rect.get_ur_y()}, {rect.get_ll_x(), rect.get_ur_y()}};
}

eccdb::DesignPlacementStatus placementStatus(const Instance& instance, bool macro)
{
  if (instance.get_cover()) {
    return eccdb::DesignPlacementStatus::kCover;
  }
  if (instance.get_fixed() || macro) {
    return eccdb::DesignPlacementStatus::kFixed;
  }
  return instance.get_placed() ? eccdb::DesignPlacementStatus::kPlaced : eccdb::DesignPlacementStatus::kUnplaced;
}

eccdb::DesignPlacementStatus placementStatus(const IOPin& pin)
{
  if (pin.get_fixed()) {
    return eccdb::DesignPlacementStatus::kFixed;
  }
  return pin.get_placed() ? eccdb::DesignPlacementStatus::kPlaced : eccdb::DesignPlacementStatus::kUnplaced;
}

eccdb::DesignSignalUse toSignalUse(PGNetType type) noexcept
{
  switch (type) {
    case PGNetType::kPower:
      return eccdb::DesignSignalUse::kPower;
    case PGNetType::kGround:
      return eccdb::DesignSignalUse::kGround;
    default:
      return eccdb::DesignSignalUse::kNone;
  }
}

eccdb::DesignIoPinDirection toIoDirection(IOPinDirection direction) noexcept
{
  switch (direction) {
    case IOPinDirection::kInput:
      return eccdb::DesignIoPinDirection::kInput;
    case IOPinDirection::kOutput:
    case IOPinDirection::kOutputTriState:
      return eccdb::DesignIoPinDirection::kOutput;
    case IOPinDirection::kInOut:
      return eccdb::DesignIoPinDirection::kInOut;
    case IOPinDirection::kFeedThru:
      return eccdb::DesignIoPinDirection::kFeedThru;
    default:
      return eccdb::DesignIoPinDirection::kNone;
  }
}

IOPinDirection toFpIoDirection(eccdb::DesignIoPinDirection direction) noexcept
{
  switch (direction) {
    case eccdb::DesignIoPinDirection::kInput:
      return IOPinDirection::kInput;
    case eccdb::DesignIoPinDirection::kOutput:
      return IOPinDirection::kOutput;
    case eccdb::DesignIoPinDirection::kInOut:
      return IOPinDirection::kInOut;
    case eccdb::DesignIoPinDirection::kFeedThru:
      return IOPinDirection::kFeedThru;
    case eccdb::DesignIoPinDirection::kNone:
      return IOPinDirection::kNone;
  }
  return IOPinDirection::kNone;
}

PGNetType toPgNetType(eccdb::DesignSignalUse use) noexcept
{
  switch (use) {
    case eccdb::DesignSignalUse::kPower:
      return PGNetType::kPower;
    case eccdb::DesignSignalUse::kGround:
      return PGNetType::kGround;
    default:
      return PGNetType::kNone;
  }
}

eccdb::TechRoutingLayerId requireRoutingLayer(const eccdb::TechStore& technology, std::string_view name)
{
  const auto generic = technology.findLayer(name);
  const auto layer = eccdb::TechRoutingLayerId{generic.entity()};
  if (!generic || !technology.routingLayerStorage().contains(layer)) {
    throw std::invalid_argument("iFP native adapter requires a known routing layer: " + std::string{name});
  }
  return layer;
}

eccdb::DesignInstanceId requireInstance(const eccdb::DesignNetlistStorage& netlist, std::string_view name)
{
  const auto id = netlist.findInstance(name);
  if (!id) {
    throw std::invalid_argument("iFP native adapter cannot find instance: " + std::string{name});
  }
  return id;
}

bool isIpfPhysicalInstanceName(std::string_view name) noexcept
{
  return name.starts_with("BNDRY_CAP_") || name.starts_with("BNDRY_CAP_TAP_") || name.starts_with("WELLTAP_");
}

std::string allocateIpfRerunName(const eccdb::DesignNetlistStorage& netlist,
                                 std::unordered_set<std::string>& reserved,
                                 std::string_view base)
{
  for (uint64_t suffix = 1; suffix != 0; ++suffix) {
    auto candidate = std::string{base} + "__IFP_RERUN_" + std::to_string(suffix);
    if (reserved.contains(candidate) || netlist.findInstance(candidate)) {
      continue;
    }
    reserved.insert(candidate);
    return candidate;
  }
  throw std::overflow_error("iFP native output exhausted rerun instance names");
}

eccdb::Rect instanceBounds(const eccdb::DesignInstance& instance, const eccdb::LibraryCellMaster& master)
{
  const auto width = static_cast<int32_t>(master.width);
  const auto height = static_cast<int32_t>(master.height);
  if (width <= 0 || height <= 0) {
    return {instance.origin.x, instance.origin.y, instance.origin.x, instance.origin.y};
  }
  const bool quarter_turn = eccdb::isQuarterTurnOrientation(instance.orientation);
  return {instance.origin.x, instance.origin.y, instance.origin.x + (quarter_turn ? height : width),
          instance.origin.y + (quarter_turn ? width : height)};
}

void appendMacroPinRectangle(Instance& target, std::string_view pin_name, const eccdb::PhysicalLayerRect& rectangle,
                             const eccdb::TechStore& technology)
{
  const auto layer = eccdb::TechRoutingLayerId{rectangle.layer.entity()};
  // iFP's PG model accepts only routing-layer rectangles.  PhysicalDesignService
  // also visits cut geometry and VIA expansion, so filter it here rather than
  // accidentally treating a cut as an iFP routing layer.
  if (!technology.routingLayerStorage().contains(layer)) {
    return;
  }
  InstancePinShape shape;
  shape.set_pin_name(std::string{pin_name});
  shape.set_layer_name(technology.layerInfo(rectangle.layer).name);
  shape.set_rect(rectangle.rectangle.ll_x, rectangle.rectangle.ll_y, rectangle.rectangle.ur_x, rectangle.rectangle.ur_y);
  target.get_pin_shape_list().push_back(std::move(shape));
}

void appendMacroPinShapes(eccdb::DatabaseContext& context, const eccdb::DesignInstance& source,
                          std::span<const eccdb::DesignInstanceTermId> terms, Instance& target)
{
  auto* technology = context.technology();
  auto* library = context.library();
  if (technology == nullptr || library == nullptr) {
    throw std::logic_error("iFP native macro pin projection requires complete technology and library stores");
  }
  const auto& netlist = context.design().netlistStorage();
  const eccdb::PhysicalDesignService physical{context};
  const auto append_via = [&](std::string_view pin_name, const eccdb::PhysicalViaPlacement& placement) {
    const auto append_rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
      appendMacroPinRectangle(target, pin_name, rectangle, *technology);
    };
    if (placement.tech_via) {
      physical.forEachTechViaGeometry(placement.tech_via, placement.origin, placement.orientation,
                                      eccdb::PhysicalGeometryVisitor{.rectangle = append_rectangle});
    } else if (placement.design_via) {
      physical.forEachDesignViaGeometry(placement.design_via, placement.origin, placement.orientation,
                                        eccdb::PhysicalGeometryVisitor{.rectangle = append_rectangle});
    }
  };

  for (const auto term_id : terms) {
    const auto& instance_term = netlist.instanceTerm(term_id);
    const auto& master_term = library->masterTermStorage().masterTerm(instance_term.master_term);
    const auto append_geometry = [&](const eccdb::PhysicalLayerRect& rectangle) {
      appendMacroPinRectangle(target, master_term.name, rectangle, *technology);
    };
    const auto visitor = eccdb::PhysicalGeometryVisitor{
        .rectangle = append_geometry,
        .via = [&](const eccdb::PhysicalViaPlacement& placement) { append_via(master_term.name, placement); }};
    for (const auto master_pin : physical.masterPins(instance_term.master_term)) {
      if (source.placement_status == eccdb::DesignPlacementStatus::kUnplaced) {
        // Legacy iFP exposes unplaced macro PORT rectangles in master-local
        // coordinates.  Keep that contract while the placed case is fully
        // transformed by PhysicalDesignService.
        physical.forEachMasterPinGeometry(master_pin, visitor);
      } else {
        physical.forEachPlacedInstanceMasterPinGeometry(term_id, master_pin, visitor);
      }
    }
  }
}

std::optional<eccdb::TechRoutingLayerId> firstRoutingLayer(const eccdb::TechStore& technology)
{
  for (const auto generic : technology.layerSequence()) {
    const auto routing = eccdb::TechRoutingLayerId{generic.entity()};
    if (technology.routingLayerStorage().contains(routing)) {
      return routing;
    }
  }
  return std::nullopt;
}

std::optional<eccdb::TechRoutingLayerId> macroTopRoutingLayer(const eccdb::DatabaseContext& context,
                                                               eccdb::DesignInstanceId instance)
{
  auto* technology = context.technology();
  if (technology == nullptr) {
    throw std::logic_error("iFP native macro route-halo projection requires a TechStore");
  }
  const auto& netlist = context.design().netlistStorage();
  const eccdb::PhysicalDesignService physical{context};
  std::optional<eccdb::TechRoutingLayerId> top;
  const auto consider_rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
    const auto routing = eccdb::TechRoutingLayerId{rectangle.layer.entity()};
    if (!technology->routingLayerStorage().contains(routing)) {
      return;
    }
    const auto candidate_position = technology->layerPosition(rectangle.layer).value_or(0u);
    const auto top_position = top ? technology->layerPosition(eccdb::TechLayerId{top->entity()}).value_or(0u) : 0u;
    if (!top || candidate_position > top_position) {
      top = routing;
    }
  };
  const auto consider_via = [&](const eccdb::PhysicalViaPlacement& placement) {
    const auto visitor = eccdb::PhysicalGeometryVisitor{.rectangle = consider_rectangle};
    if (placement.tech_via) {
      physical.forEachTechViaGeometry(placement.tech_via, placement.origin, placement.orientation, visitor);
    } else if (placement.design_via) {
      physical.forEachDesignViaGeometry(placement.design_via, placement.origin, placement.orientation, visitor);
    }
  };
  const auto visitor = eccdb::PhysicalGeometryVisitor{.rectangle = consider_rectangle, .via = consider_via};
  for (const auto term : netlist.instanceTerms(instance)) {
    const auto& instance_term = netlist.instanceTerm(term);
    for (const auto pin : physical.masterPins(instance_term.master_term)) {
      physical.forEachPlacedInstanceMasterPinGeometry(term, pin, visitor);
    }
  }
  physical.forEachPlacedMasterObstructionGeometry(instance, visitor);
  return top;
}

void clearNativeInput(Database& database)
{
  database.get_site_map().clear();
  database.get_cell_master_map().clear();
  database.get_routing_layer_list().clear();
  database.get_instance_list().clear();
  database.get_net_list().clear();
  database.get_io_pin_list().clear();
  database.get_pg_net_list().clear();
  database.get_pg_segment_list().clear();
  database.get_row_list().clear();
  database.get_new_row_list().clear();
  database.get_new_track_list().clear();
  database.get_instance_name_to_idx_map().clear();
  database.get_routing_layer_name_to_idx_map().clear();
  database.get_io_pin_name_to_idx_map().clear();
  database.get_pg_net_name_to_idx_map().clear();
  database.set_die_updated(false);
  database.set_core_updated(false);
  database.set_track_updated(false);
}

void rebuildNativeMaps(Database& database)
{
  auto& instances = database.get_instance_name_to_idx_map();
  for (int32_t index = 0; index < static_cast<int32_t>(database.get_instance_list().size()); ++index) {
    instances[database.get_instance_list()[index].get_name()] = index;
  }
  auto& layers = database.get_routing_layer_name_to_idx_map();
  for (int32_t index = 0; index < static_cast<int32_t>(database.get_routing_layer_list().size()); ++index) {
    layers[database.get_routing_layer_list()[index].get_name()] = index;
  }
  auto& pins = database.get_io_pin_name_to_idx_map();
  for (int32_t index = 0; index < static_cast<int32_t>(database.get_io_pin_list().size()); ++index) {
    pins[database.get_io_pin_list()[index].get_name()] = index;
  }
  auto& pg_nets = database.get_pg_net_name_to_idx_map();
  for (int32_t index = 0; index < static_cast<int32_t>(database.get_pg_net_list().size()); ++index) {
    pg_nets[database.get_pg_net_list()[index].get_name()] = index;
  }
}

void writeIoPins(eccdb::DatabaseContext& context, Database& database)
{
  auto& design = context.design();
  auto& netlist = design.netlistStorage();
  auto* technology = context.technology();
  if (technology == nullptr) {
    throw std::logic_error("iFP native IO output requires a TechStore");
  }
  eccdb::ConnectivityService connectivity{context};
  eccdb::FloorplanService floorplan{context};

  for (auto& pin : database.get_io_pin_list()) {
    if (!pin.is_updated()) {
      continue;
    }
    auto term = netlist.findBlockTerm(pin.get_name());
    if (!term) {
      term = connectivity.createBlockTerm(eccdb::DesignBlockTerm{.name = pin.get_name(), .direction = eccdb::DesignIoPinDirection::kNone});
    }

    const auto status = placementStatus(pin);
    const auto origin = eccdb::Point{pin.get_x(), pin.get_y()};
    const auto orientation = toDesignOrientation(pin.get_orient());
    const auto ports = netlist.blockPins(term);
    bool has_implicit_port = false;
    for (const auto port : ports) {
      has_implicit_port |= (netlist.blockPin(port).flags & eccdb::DesignIoPinPortFlag::kExplicit) == 0u;
    }

    const auto build_new_ports = [&]() {
      std::vector<eccdb::DesignBlockPinInput> inputs;
      inputs.reserve(pin.get_new_port_list().size());
      for (const auto& port : pin.get_new_port_list()) {
        const auto layer = requireRoutingLayer(*technology, port.get_layer_name());
        eccdb::DesignBlockPinInput input{.flags = eccdb::DesignIoPinPortFlag::kExplicit | eccdb::DesignIoPinPortFlag::kHasPlacement,
                                         .placement_status = status,
                                         .origin = {port.get_x(), port.get_y()},
                                         .orientation = orientation};
        input.rectangles.push_back(eccdb::DesignPinRectangle{.layer = eccdb::TechLayerId{layer.entity()},
                                                             // Block-pin payloads are local to their placement.
                                                             // The exporter/view applies origin and orientation.
                                                             .rectangle = toRect(port)});
        inputs.push_back(std::move(input));
      }
      return inputs;
    };

    if (has_implicit_port && !pin.get_new_port_list().empty()) {
      // A legacy DEF PIN without +PORT is represented by one implicit
      // block-pin.  Convert that group atomically before adding iFP's new
      // explicit geometry; storage intentionally rejects mixed forms.
      static_cast<void>(floorplan.replaceAllBlockPinPorts(term, build_new_ports()));
    } else {
      for (const auto port : ports) {
        // An added PORT has its own placement. The IO algorithm does not
        // request relocation of pre-existing explicit PORTs when appending
        // it; moving them all would also move the original metal access.
        if (pin.get_new_port_list().empty()
            || (netlist.blockPin(port).flags & eccdb::DesignIoPinPortFlag::kExplicit) == 0u) {
          floorplan.placeBlockPin(port, origin, orientation, status);
        }
      }
      for (auto input : build_new_ports()) {
        static_cast<void>(floorplan.createBlockPin(term, std::move(input)));
      }
    }
    pin.get_new_port_list().clear();
    pin.set_updated(false);
  }
}

void writeInstances(eccdb::DatabaseContext& context, Database& database)
{
  auto& design = context.design();
  auto& netlist = design.netlistStorage();
  auto* library = context.library();
  auto* technology = context.technology();
  if (library == nullptr || technology == nullptr) {
    throw std::logic_error("iFP native instance output requires complete technology and library stores");
  }
  eccdb::InstanceService instances{context};
  eccdb::FloorplanService floorplan{context};
  auto& database_instances = database.get_instance_list();
  std::unordered_map<std::string, std::size_t> generated_indices;
  generated_indices.reserve(database_instances.size());
  std::unordered_set<std::string> reserved_names;
  reserved_names.reserve(database_instances.size());
  for (std::size_t index = 0; index < database_instances.size(); ++index) {
    const auto& instance = database_instances[index];
    if (instance.get_name().empty()) {
      throw std::invalid_argument("iFP native output generated an instance with an empty name");
    }
    reserved_names.insert(instance.get_name());
    if (instance.is_new_instance()) {
      const auto [unused, inserted] = generated_indices.emplace(instance.get_name(), index);
      if (!inserted) {
        throw std::invalid_argument("iFP native output generated a duplicate instance name: " + instance.get_name());
      }
    }
  }

  for (std::size_t index = 0; index < database_instances.size(); ++index) {
    auto& instance = database_instances[index];
    // A native wrap contains the previous DIST physical cell followed by the
    // newly generated replacement with the same algorithm-owned name.  The
    // new record is authoritative; the old projection is only retained for
    // the later halo pass.
    if (!instance.is_new_instance() && generated_indices.contains(instance.get_name())) {
      continue;
    }
    auto id = netlist.findInstance(instance.get_name());
    const bool generated = instance.is_new_instance();
    bool place_generated_instance = false;
    if (generated) {
      const auto master = library->cellMasterStorage().findCellMaster(instance.get_master_name());
      if (!master) {
        throw std::invalid_argument("iFP native adapter cannot find master: " + instance.get_master_name());
      }
      const auto& master_value = library->cellMasterStorage().cellMaster(master);
      if (id) {
        const auto& stored = netlist.instance(id);
        const bool previous_ifp_instance = isIpfPhysicalInstanceName(instance.get_name())
                                            && stored.source == eccdb::DesignInstanceSource::kDist;
        if (!previous_ifp_instance) {
          throw std::invalid_argument("iFP native output refuses to overwrite an existing non-distribution instance: " + instance.get_name());
        }
        if (stored.master != master) {
          try {
            instances.swapMasterCompatible(id, master);
          } catch (const std::invalid_argument&) {
            // A prior physical cell may carry a terminal set that is not
            // compatible with this run's chosen filler/endcap master. Keep it
            // intact and allocate a fresh algorithm-owned name instead.
            const auto old_name = instance.get_name();
            instance.set_name(allocateIpfRerunName(netlist, reserved_names, old_name));
            id = {};
          }
        }
        if (id) {
          // Physical-cell placement is idempotent across reruns: reuse a
          // prior DIST object, preserving its connectivity and Term IDs.
          place_generated_instance = true;
        }
      } else {
        id = instances.create(eccdb::DesignInstance{.name = instance.get_name(),
                                                     .master = master,
                                                     .origin = {instance.get_x(), instance.get_y()},
                                                     .orientation = toDesignOrientation(instance.get_orient()),
                                                     .placement_status = placementStatus(instance, isMacro(master_value.type)),
                                                     .source = eccdb::DesignInstanceSource::kDist});
      }
      if (!id) {
        id = instances.create(eccdb::DesignInstance{.name = instance.get_name(),
                                                     .master = master,
                                                     .origin = {instance.get_x(), instance.get_y()},
                                                     .orientation = toDesignOrientation(instance.get_orient()),
                                                     .placement_status = placementStatus(instance, isMacro(master_value.type)),
                                                     .source = eccdb::DesignInstanceSource::kDist});
      }
      instance.set_new_instance(false);
    }
    if (!id) {
      throw std::invalid_argument("iFP native adapter cannot find instance: " + instance.get_name());
    }
    if (instance.is_placement_updated() || place_generated_instance) {
      const auto& stored = netlist.instance(id);
      const auto& master = library->cellMasterStorage().cellMaster(stored.master);
      instances.place(id, {instance.get_x(), instance.get_y()}, toDesignOrientation(instance.get_orient()), placementStatus(instance, isMacro(master.type)));
      instance.set_placement_updated(false);
    }
  }

  for (const auto& instance : database.get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }
    const auto id = requireInstance(netlist, instance.get_name());
    const auto& macro = instance.get_bounding_rect();
    const auto& placement_halo = instance.get_placement_halo_rect();
    if (hasArea(macro) && hasArea(placement_halo) && placement_halo.get_ll_x() <= macro.get_ll_x()
        && placement_halo.get_ll_y() <= macro.get_ll_y() && placement_halo.get_ur_x() >= macro.get_ur_x()
        && placement_halo.get_ur_y() >= macro.get_ur_y()) {
      floorplan.setInstanceHalo(id,
                                {.left = macro.get_ll_x() - placement_halo.get_ll_x(),
                                 .bottom = macro.get_ll_y() - placement_halo.get_ll_y(),
                                 .right = placement_halo.get_ur_x() - macro.get_ur_x(),
                                 .top = placement_halo.get_ur_y() - macro.get_ur_y()},
                                false);
    }
  }

  const auto bottom_layer = firstRoutingLayer(*technology);
  if (!bottom_layer) {
    return;
  }
  for (const auto& instance : database.get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }
    const auto id = requireInstance(netlist, instance.get_name());
    const auto& macro = instance.get_bounding_rect();
    const auto& route_halo = instance.get_routing_halo_rect();
    if (!hasArea(macro) || !hasArea(route_halo) || route_halo.get_ll_x() > macro.get_ll_x()
        || route_halo.get_ll_y() > macro.get_ll_y() || route_halo.get_ur_x() < macro.get_ur_x()
        || route_halo.get_ur_y() < macro.get_ur_y()) {
      continue;
    }
    const int32_t left = macro.get_ll_x() - route_halo.get_ll_x();
    const int32_t bottom = macro.get_ll_y() - route_halo.get_ll_y();
    const int32_t right = route_halo.get_ur_x() - macro.get_ur_x();
    const int32_t top = route_halo.get_ur_y() - macro.get_ur_y();
    if (left < 0 || left != bottom || left != right || left != top) {
      continue;
    }
    const auto top_layer = macroTopRoutingLayer(context, id);
    if (!top_layer) {
      continue;
    }
    auto replacement = netlist.instance(id);
    replacement.flags |= eccdb::DesignInstanceFlag::kHasRouteHalo;
    replacement.route_halo = {.distance = left, .min_layer = *bottom_layer, .max_layer = *top_layer};
    instances.update(id, std::move(replacement));
  }
}

void writePgNets(eccdb::DatabaseContext& context, Database& database)
{
  auto& netlist = context.design().netlistStorage();
  auto* library = context.library();
  if (library == nullptr) {
    throw std::logic_error("iFP native PG output requires a LibraryStore");
  }
  eccdb::ConnectivityService connectivity{context};
  // PGNet stores master pin names, rather than one entry per instance.
  // Build their occurrence index once for all PG nets.
  std::unordered_map<std::string_view, std::vector<eccdb::DesignInstanceTermId>> terms_by_name;
  netlist.forEachInstanceTerm([&](eccdb::DesignInstanceTermId term_id, const eccdb::DesignInstanceTerm& term) {
    const auto& name = library->masterTermStorage().masterTerm(term.master_term).name;
    terms_by_name[name].push_back(term_id);
  });
  for (const auto& pg_net : database.get_pg_net_list()) {
    auto id = netlist.findSpecialNet(pg_net.get_name());
    if (!id) {
      id = connectivity.createNet(eccdb::DesignNet{.name = pg_net.get_name(), .use = toSignalUse(pg_net.get_type()),
                                                    .source = eccdb::DesignNetSource::kDist},
                                  true);
    }
    for (const auto& [pin_name, direction] : pg_net.get_io_pin_name_to_direction_map()) {
      auto term = netlist.findBlockTerm(pin_name);
      if (!term) {
        term = connectivity.createBlockTerm(eccdb::DesignBlockTerm{.name = pin_name, .direction = toIoDirection(direction),
                                                                      .use = toSignalUse(pg_net.get_type())});
      }
      connectivity.connect(term, id);
    }
    std::unordered_set<std::string_view> connected_names;
    for (const auto& term_name : pg_net.get_instance_pin_name_list()) {
      if (!connected_names.insert(term_name).second) {
        continue;
      }
      if (const auto found = terms_by_name.find(term_name); found != terms_by_name.end()) {
        for (const auto term_id : found->second) {
          connectivity.connect(term_id, id);
        }
      }
    }
  }
}

struct GeneratedPgViaSpec
{
  eccdb::TechViaRuleGenerateId rule;
  eccdb::TechRoutingLayerId bottom_layer;
  eccdb::TechCutLayerId cut_layer;
  eccdb::TechRoutingLayerId top_layer;
  eccdb::TechRoutingLayerId entry_layer;
  eccdb::TechRoutingLayerId exit_layer;
  int32_t cut_size_x = 0;
  int32_t cut_size_y = 0;
  int32_t cut_spacing_x = 0;
  int32_t cut_spacing_y = 0;
  int32_t bottom_enclosure_x = 0;
  int32_t bottom_enclosure_y = 0;
  int32_t top_enclosure_x = 0;
  int32_t top_enclosure_y = 0;
  uint32_t rows = 1;
  uint32_t columns = 1;
};

std::optional<GeneratedPgViaSpec> generatedPgViaSpec(const eccdb::TechStore& technology, const PGSegment& segment)
{
  const auto bottom_layer = requireRoutingLayer(technology, segment.get_bottom_layer_name());
  const auto top_layer = requireRoutingLayer(technology, segment.get_top_layer_name());
  if (bottom_layer == top_layer) {
    throw std::invalid_argument("iFP native PG VIA requires distinct bottom and top routing layers");
  }

  std::optional<eccdb::TechCutLayerId> requested_cut;
  if (!segment.get_cut_layer_name().empty()) {
    const auto generic = technology.findLayer(segment.get_cut_layer_name());
    const auto cut = eccdb::TechCutLayerId{generic.entity()};
    if (!generic || !technology.cutLayerStorage().contains(cut)) {
      throw std::invalid_argument("iFP native PG VIA references an unknown cut layer: " + segment.get_cut_layer_name());
    }
    requested_cut = cut;
  }

  const auto& rules = technology.viaRuleGenerateStorage();
  std::vector<eccdb::TechViaRuleGenerateId> candidates;
  if (requested_cut.has_value()) {
    candidates = rules.viaRuleGeneratesForCutLayer(*requested_cut);
  } else {
    candidates = rules.viaRuleGeneratesForLayer(eccdb::TechLayerId{bottom_layer.entity()});
  }

  eccdb::TechViaRuleGenerateId selected;
  for (const auto rule : candidates) {
    const auto& bottom = rules.bottomLayer(rule);
    const auto& cut = rules.cutLayer(rule);
    const auto& top = rules.topLayer(rule);
    const bool canonical = bottom.layer.layer() == eccdb::TechLayerId{bottom_layer.entity()}
                           && top.layer.layer() == eccdb::TechLayerId{top_layer.entity()};
    const bool opposite = bottom.layer.layer() == eccdb::TechLayerId{top_layer.entity()}
                          && top.layer.layer() == eccdb::TechLayerId{bottom_layer.entity()};
    if ((!canonical && !opposite) || (requested_cut.has_value() && cut.layer != *requested_cut)) {
      continue;
    }
    if (!selected || rules.viaRuleGenerate(rule).isDefault()) {
      selected = rule;
    }
  }
  if (!selected) {
    return std::nullopt;
  }

  const auto& cut = rules.cutLayer(selected);
  int32_t cut_size_x = cut.cut_rect.ur_x - cut.cut_rect.ll_x;
  int32_t cut_size_y = cut.cut_rect.ur_y - cut.cut_rect.ll_y;
  if (cut_size_x <= 0 || cut_size_y <= 0) {
    const auto& cut_layer = technology.cutLayerStorage().cutLayer(cut.layer);
    cut_size_x = cut_layer.width;
    cut_size_y = cut_layer.width;
  }
  if (cut_size_x <= 0 || cut_size_y <= 0) {
    throw std::invalid_argument("iFP native PG VIA rule has no positive cut size");
  }
  // VIARULE GENERATE SPACING is center-to-center pitch. Design VIA
  // CUTSPACING is the edge-to-edge gap, so subtract the cut size once.
  int32_t pitch_x = std::max(cut.spacing_x, cut_size_x);
  int32_t pitch_y = std::max(cut.spacing_y, cut_size_y);
  for (const auto spacing_id : technology.cutLayerStorage().spacingRules(cut.layer)) {
    const auto& spacing = technology.cutLayerStorage().spacingRule(spacing_id);
    if (spacing.flags == 0u) {
      pitch_x = std::max(pitch_x, cut_size_x + spacing.spacing);
      pitch_y = std::max(pitch_y, cut_size_y + spacing.spacing);
    } else if (spacing.flags == eccdb::TechCutSpacingRuleFlag::kCenterToCenter) {
      pitch_x = std::max(pitch_x, spacing.spacing);
      pitch_y = std::max(pitch_y, spacing.spacing);
    }
  }
  const int32_t cut_spacing_x = pitch_x - cut_size_x;
  const int32_t cut_spacing_y = pitch_y - cut_size_y;
  const auto& bottom = rules.bottomLayer(selected);
  const auto& top = rules.topLayer(selected);
  const int32_t requested_width = segment.get_via_width() > 0 ? segment.get_via_width() : cut_size_x;
  const int32_t requested_height = segment.get_via_height() > 0 ? segment.get_via_height() : cut_size_y;

  struct Choice
  {
    int32_t bottom_x = 0;
    int32_t bottom_y = 0;
    int32_t top_x = 0;
    int32_t top_y = 0;
    uint32_t rows = 0;
    uint32_t columns = 0;
    uint64_t cuts = 0;
  };
  Choice best;
  for (int bottom_swap = 0; bottom_swap < 2; ++bottom_swap) {
    const int32_t bottom_x = bottom_swap == 0 ? bottom.enclosure_overhang1 : bottom.enclosure_overhang2;
    const int32_t bottom_y = bottom_swap == 0 ? bottom.enclosure_overhang2 : bottom.enclosure_overhang1;
    for (int top_swap = 0; top_swap < 2; ++top_swap) {
      const int32_t top_x = top_swap == 0 ? top.enclosure_overhang1 : top.enclosure_overhang2;
      const int32_t top_y = top_swap == 0 ? top.enclosure_overhang2 : top.enclosure_overhang1;
      const int32_t minimum_width = cut_size_x + 2 * std::max(bottom_x, top_x);
      const int32_t minimum_height = cut_size_y + 2 * std::max(bottom_y, top_y);
      if (requested_width < minimum_width || requested_height < minimum_height || pitch_x <= 0 || pitch_y <= 0) {
        continue;
      }
      const auto columns = static_cast<uint32_t>(1 + (requested_width - minimum_width) / pitch_x);
      const auto rows = static_cast<uint32_t>(1 + (requested_height - minimum_height) / pitch_y);
      const auto cuts = static_cast<uint64_t>(rows) * columns;
      if (cuts > best.cuts) {
        best = Choice{.bottom_x = std::max(bottom_x, 0),
                      .bottom_y = std::max(bottom_y, 0),
                      .top_x = std::max(top_x, 0),
                      .top_y = std::max(top_y, 0),
                      .rows = rows,
                      .columns = columns,
                      .cuts = cuts};
      }
    }
  }
  if (best.cuts == 0) {
    throw std::invalid_argument("iFP native PG VIA dimensions are smaller than the technology enclosure");
  }
  const auto& canonical_bottom = rules.bottomLayer(selected);
  const auto& canonical_top = rules.topLayer(selected);
  const auto formula_bottom = eccdb::TechRoutingLayerId{canonical_bottom.layer.entity};
  const auto formula_top = eccdb::TechRoutingLayerId{canonical_top.layer.entity};
  return GeneratedPgViaSpec{.rule = selected,
                            .bottom_layer = formula_bottom,
                            .cut_layer = cut.layer,
                            .top_layer = formula_top,
                            .entry_layer = bottom_layer,
                            .exit_layer = top_layer,
                            .cut_size_x = cut_size_x,
                            .cut_size_y = cut_size_y,
                            .cut_spacing_x = cut_spacing_x,
                            .cut_spacing_y = cut_spacing_y,
                            .bottom_enclosure_x = best.bottom_x,
                            .bottom_enclosure_y = best.bottom_y,
                            .top_enclosure_x = best.top_x,
                            .top_enclosure_y = best.top_y,
                            .rows = best.rows,
                            .columns = best.columns};
}

std::vector<GeneratedPgViaSpec> generatedPgViaSpecs(const eccdb::TechStore& technology, const PGSegment& segment)
{
  if (const auto direct = generatedPgViaSpec(technology, segment); direct.has_value()) {
    return {*direct};
  }

  const auto requested_bottom = requireRoutingLayer(technology, segment.get_bottom_layer_name());
  const auto requested_top = requireRoutingLayer(technology, segment.get_top_layer_name());
  std::vector<eccdb::TechRoutingLayerId> routing_layers;
  for (const auto generic : technology.layerSequence()) {
    const auto routing = eccdb::TechRoutingLayerId{generic.entity()};
    if (technology.routingLayerStorage().contains(routing)) {
      routing_layers.push_back(routing);
    }
  }
  const auto bottom_it = std::find(routing_layers.begin(), routing_layers.end(), requested_bottom);
  const auto top_it = std::find(routing_layers.begin(), routing_layers.end(), requested_top);
  if (bottom_it == routing_layers.end() || top_it == routing_layers.end() || bottom_it == top_it) {
    return {};
  }
  if (bottom_it > top_it) {
    std::reverse(routing_layers.begin(), routing_layers.end());
  }
  const auto first = std::find(routing_layers.begin(), routing_layers.end(), requested_bottom);
  const auto last = std::find(routing_layers.begin(), routing_layers.end(), requested_top);
  if (first == routing_layers.end() || last == routing_layers.end() || first >= last) {
    return {};
  }

  std::vector<GeneratedPgViaSpec> result;
  result.reserve(static_cast<std::size_t>(std::distance(first, last)));
  std::optional<eccdb::TechCutLayerId> requested_cut;
  if (!segment.get_cut_layer_name().empty()) {
    const auto generic = technology.findLayer(segment.get_cut_layer_name());
    const auto cut = eccdb::TechCutLayerId{generic.entity()};
    if (!generic || !technology.cutLayerStorage().contains(cut)) {
      return {};
    }
    requested_cut = cut;
  }
  bool matched_requested_cut = !requested_cut.has_value();
  for (auto current = first; current != last; ++current) {
    auto next = std::next(current);
    PGSegment edge = segment;
    edge.set_bottom_layer_name(technology.layerInfo(eccdb::TechLayerId{current->entity()}).name);
    edge.set_top_layer_name(technology.layerInfo(eccdb::TechLayerId{next->entity()}).name);
    // A cut named on a legacy stacked segment selects one edge; the other
    // edges still resolve their own technology cut layers.
    edge.set_cut_layer_name("");
    const auto spec = generatedPgViaSpec(technology, edge);
    if (!spec.has_value()) {
      return {};
    }
    if (requested_cut.has_value() && spec->cut_layer == *requested_cut) {
      matched_requested_cut = true;
    }
    result.push_back(*spec);
  }
  return result.size() == static_cast<std::size_t>(std::distance(first, last)) && matched_requested_cut ? result
                                                                                                           : std::vector<GeneratedPgViaSpec>{};
}

bool sameGeneratedPgVia(const eccdb::DesignVia& via, const GeneratedPgViaSpec& spec) noexcept
{
  if ((via.flags & eccdb::DesignViaFlag::kGenerated) == 0u) {
    return false;
  }
  const auto& generated = via.generated;
  return generated.via_rule == spec.rule && generated.bottom_layer == spec.bottom_layer && generated.cut_layer == spec.cut_layer
         && generated.top_layer == spec.top_layer && generated.cut_size_x == spec.cut_size_x && generated.cut_size_y == spec.cut_size_y
         && generated.cut_spacing_x == spec.cut_spacing_x && generated.cut_spacing_y == spec.cut_spacing_y
         && generated.bottom_enclosure_x == spec.bottom_enclosure_x && generated.bottom_enclosure_y == spec.bottom_enclosure_y
         && generated.top_enclosure_x == spec.top_enclosure_x && generated.top_enclosure_y == spec.top_enclosure_y
         && generated.row_count == spec.rows && generated.column_count == spec.columns;
}

eccdb::DesignViaId getOrCreateGeneratedPgVia(eccdb::RoutingService& routing, const GeneratedPgViaSpec& spec,
                                             std::unordered_map<std::string, eccdb::DesignViaId>& cache)
{
  const auto key = std::to_string(spec.rule.packed()) + ":" + std::to_string(spec.cut_size_x) + "x"
                   + std::to_string(spec.cut_size_y) + ":" + std::to_string(spec.rows) + "x" + std::to_string(spec.columns)
                   + ":" + std::to_string(spec.cut_spacing_x) + "," + std::to_string(spec.cut_spacing_y)
                   + ":" + std::to_string(spec.bottom_enclosure_x) + "," + std::to_string(spec.bottom_enclosure_y)
                   + ":" + std::to_string(spec.top_enclosure_x) + "," + std::to_string(spec.top_enclosure_y);
  if (const auto found = cache.find(key); found != cache.end()) {
    return found->second;
  }

  const auto base_name = std::string{"__IFP_PG_VIA_"} + std::to_string(spec.rule.packed()) + "_" + std::to_string(spec.cut_size_x) + "x"
                         + std::to_string(spec.cut_size_y) + "_" + std::to_string(spec.rows) + "x" + std::to_string(spec.columns);
  auto name = base_name;
  eccdb::DesignViaId existing = routing.findVia(name);
  for (uint32_t suffix = 1; existing && !sameGeneratedPgVia(routing.via(existing), spec); ++suffix) {
    name = base_name + "_" + std::to_string(suffix);
    existing = routing.findVia(name);
  }
  if (existing) {
    cache.emplace(key, existing);
    return existing;
  }

  const auto created = routing.createVia(eccdb::DesignViaInput{
      .name = name,
      .flags = eccdb::DesignViaFlag::kGenerated,
      .generated = {.via_rule = spec.rule,
                    .bottom_layer = spec.bottom_layer,
                    .cut_layer = spec.cut_layer,
                    .top_layer = spec.top_layer,
                    .flags = eccdb::DesignGeneratedViaFlag::kHasRowCol,
                    .cut_size_x = spec.cut_size_x,
                    .cut_size_y = spec.cut_size_y,
                    .cut_spacing_x = spec.cut_spacing_x,
                    .cut_spacing_y = spec.cut_spacing_y,
                    .bottom_enclosure_x = spec.bottom_enclosure_x,
                    .bottom_enclosure_y = spec.bottom_enclosure_y,
                    .top_enclosure_x = spec.top_enclosure_x,
                    .top_enclosure_y = spec.top_enclosure_y,
                    .row_count = spec.rows,
                    .column_count = spec.columns}});
  cache.emplace(key, created);
  return created;
}

void writePgSegments(eccdb::DatabaseContext& context, Database& database)
{
  auto& netlist = context.design().netlistStorage();
  auto* technology = context.technology();
  if (technology == nullptr) {
    throw std::logic_error("iFP native PG segment output requires a TechStore");
  }

  eccdb::RoutingService routing{context};
  std::map<std::string, eccdb::DesignWireId> wires;
  std::unordered_map<std::string, eccdb::DesignViaId> generated_vias;
  for (auto& segment : database.get_pg_segment_list()) {
    if (!segment.get_generated()) {
      continue;
    }
    const auto net = netlist.findSpecialNet(segment.get_net_name());
    if (!net) {
      throw std::invalid_argument("iFP native PG segment references a missing special net: " + segment.get_net_name()
                                  + " (type=" + std::to_string(static_cast<int>(segment.get_type())) + ", layer="
                                  + segment.get_layer_name() + ", bottom=" + segment.get_bottom_layer_name() + ", top="
                                  + segment.get_top_layer_name() + ", origin=" + std::to_string(segment.get_start_x()) + ","
                                  + std::to_string(segment.get_start_y()) + ")");
    }
    auto [wire_it, inserted] = wires.try_emplace(segment.get_net_name());
    if (inserted) {
      wire_it->second = routing.createWire(eccdb::DesignWire{.net = net, .status = eccdb::DesignWireStatus::kFixed});
    }
    if (segment.get_type() == PGSegmentType::kVia) {
      const auto specs = generatedPgViaSpecs(*technology, segment);
      if (specs.empty()) {
        throw std::invalid_argument("iFP native PG VIA has no matching technology VIARULE GENERATE for "
                                    + segment.get_bottom_layer_name() + " to " + segment.get_top_layer_name());
      }
      for (const auto& spec : specs) {
        const auto via = getOrCreateGeneratedPgVia(routing, spec, generated_vias);
        const eccdb::DesignRouteShape shape{
            .wire = wire_it->second,
            .layer = spec.entry_layer,
            .flags = eccdb::DesignWirePathFlag::kHasWidth | eccdb::DesignWirePathFlag::kHasShape,
            .geometry = eccdb::DesignRouteVia{.origin = {.position = {segment.get_start_x(), segment.get_start_y()}},
                                               .via = eccdb::DesignWireVia{.design_via = via},
                                               .exit_layer = spec.exit_layer}};
        static_cast<void>(routing.createShape(wire_it->second, shape,
                                              eccdb::DesignRouteShapeAttributes{.width = 0, .shape = "STRIPE"}));
      }
    } else {
      if (!segment.is_line()) {
        throw std::runtime_error("iFP native PG output encountered an unsupported segment type");
      }
      const auto layer = requireRoutingLayer(*technology, segment.get_layer_name());
      const auto width = segment.get_width();
      if (width <= 0) {
        throw std::invalid_argument("iFP native PG segment width must be positive");
      }
      const eccdb::DesignRouteShape shape{
          .wire = wire_it->second,
          .layer = layer,
          .flags = eccdb::DesignWirePathFlag::kHasWidth | eccdb::DesignWirePathFlag::kHasShape,
          .geometry = eccdb::DesignRouteSegment{.begin = {.position = {segment.get_start_x(), segment.get_start_y()}},
                                                 .end = {.position = {segment.get_end_x(), segment.get_end_y()}}}};
      const auto shape_name = segment.get_type() == PGSegmentType::kFollowPin ? "FOLLOWPIN" : "STRIPE";
      static_cast<void>(routing.createShape(wire_it->second, shape,
                                            eccdb::DesignRouteShapeAttributes{.width = width, .shape = shape_name}));
    }
    segment.set_generated(false);
  }
}

}  // namespace

void FPInterface::wrapDatabaseFromEnTT()
{
  if (_database_context == nullptr || _database_context->technology() == nullptr || _database_context->library() == nullptr) {
    throw std::logic_error("iFP native adapter requires a complete DatabaseContext");
  }
  auto& context = *_database_context;
  auto& design = context.design();
  auto& technology = *context.technology();
  auto& library = *context.library();
  auto& database = FPDM.getDatabase();
  clearNativeInput(database);

  if (!design.globalStorage().hasInfo()) {
    throw std::invalid_argument("iFP native adapter requires DesignInfo");
  }
  const auto& info = design.globalStorage().info();
  if (info.database_units_per_micron <= 0) {
    throw std::invalid_argument("iFP native adapter requires positive design DBU");
  }
  database.set_design_name(info.name);
  database.set_micron_dbu(info.database_units_per_micron);
  const auto* manufacturing_grid = technology.globalStorage().tryGetManufacturingGrid();
  database.set_manufacture_grid(manufacturing_grid == nullptr ? 1 : manufacturing_grid->value);

  for (const auto site_id : library.siteStorage().sites()) {
    const auto& source = library.siteStorage().site(site_id);
    Site target;
    target.set_name(source.name);
    target.set_width(source.width);
    target.set_height(source.height);
    database.get_site_map().insert_or_assign(source.name, std::move(target));
  }
  for (const auto master_id : library.cellMasterStorage().cellMasters()) {
    const auto& source = library.cellMasterStorage().cellMaster(master_id);
    CellMaster target;
    target.set_name(source.name);
    target.set_width(static_cast<int32_t>(source.width));
    target.set_height(static_cast<int32_t>(source.height));
    target.set_pad(isPad(source.type));
    target.set_pad_filler(source.pad_filler || isPadFiller(source.type));
    target.set_spacer(source.core_filler || isSpacer(source.type));
    target.set_corner(source.site.has_value() && library.siteStorage().site(*source.site).site_class == eccdb::LibrarySiteClass::kCorner);
    database.get_cell_master_map().insert_or_assign(source.name, std::move(target));
  }

  for (const auto generic_layer : technology.layerSequence()) {
    const auto layer = eccdb::TechRoutingLayerId{generic_layer.entity()};
    if (!technology.routingLayerStorage().contains(layer)) {
      continue;
    }
    const auto& source = technology.routingLayerStorage().routingLayer(layer);
    RoutingLayer target;
    target.set_name(technology.layerInfo(generic_layer).name);
    target.set_layer_idx(static_cast<int32_t>(layer.packed()));
    target.set_order(static_cast<int32_t>(technology.layerPosition(generic_layer).value_or(0)));
    target.set_pitch_x(source.pitch_x);
    target.set_pitch_y(source.pitch_y);
    target.set_min_width(source.min_width > 0 ? source.min_width : source.width);
    target.set_prefer_track_offset(source.direction == eccdb::TechRoutingDirection::kHorizontal ? source.offset_y : source.offset_x);
    target.set_prefer_track_pitch(source.direction == eccdb::TechRoutingDirection::kHorizontal ? source.pitch_y : source.pitch_x);
    target.set_nonprefer_track_pitch(source.direction == eccdb::TechRoutingDirection::kHorizontal ? source.pitch_x : source.pitch_y);
    target.set_prefer_direction(source.direction == eccdb::TechRoutingDirection::kHorizontal
                                    ? Direction::kHorizontal
                                    : (source.direction == eccdb::TechRoutingDirection::kVertical ? Direction::kVertical : Direction::kNone));
    database.get_routing_layer_list().push_back(std::move(target));
  }

  if (design.globalStorage().hasDieArea()) {
    const auto bounds = design.globalStorage().dieBounds();
    database.get_die().set_rect(bounds.ll_x, bounds.ll_y, bounds.ur_x, bounds.ur_y);
  }
  if (design.floorplanStorage().hasCoreArea()) {
    const auto bounds = design.floorplanStorage().coreBounds();
    database.get_core().set_rect(bounds.ll_x, bounds.ll_y, bounds.ur_x, bounds.ur_y);
  }
  for (const auto row_id : design.floorplanStorage().rows()) {
    const auto& source = design.floorplanStorage().row(row_id);
    const auto& site = library.siteStorage().site(source.site);
    const auto bounds = design.floorplanStorage().rowBounds(row_id);
    Row target;
    target.set_name(source.name);
    target.set_site_name(site.name);
    target.set_site_origin_x(source.origin.x);
    target.set_y(source.origin.y);
    target.set_orient(toFpOrientation(source.orientation));
    target.set_rect(bounds.ll_x, bounds.ll_y, bounds.ur_x, bounds.ur_y);
    database.get_row_list().push_back(std::move(target));
  }

  double cell_area = 0.0;
  const auto& netlist = design.netlistStorage();
  netlist.forEachInstance([&](eccdb::DesignInstanceId, const eccdb::DesignInstance& source,
                              std::span<const eccdb::DesignInstanceTermId> terms) {
    const auto& master = library.cellMasterStorage().cellMaster(source.master);
    Instance target;
    target.set_name(source.name);
    target.set_master_name(master.name);
    target.set_orient(toFpOrientation(source.orientation));
    target.set_width(static_cast<int32_t>(master.width));
    target.set_height(static_cast<int32_t>(master.height));
    target.set_macro(isMacro(master.type));
    target.set_fixed(source.placement_status == eccdb::DesignPlacementStatus::kFixed);
    target.set_cover(source.placement_status == eccdb::DesignPlacementStatus::kCover);
    target.set_placed(source.placement_status != eccdb::DesignPlacementStatus::kUnplaced);
    if (target.get_placed()) {
      target.set_coord(source.origin.x, source.origin.y);
      const auto bounds = instanceBounds(source, master);
      target.set_bounding_rect(bounds.ll_x, bounds.ll_y, bounds.ur_x, bounds.ur_y);
    }
    if (target.get_macro()) {
      appendMacroPinShapes(context, source, terms, target);
    }
    cell_area += static_cast<double>(master.width) * static_cast<double>(master.height)
                 / static_cast<double>(info.database_units_per_micron * info.database_units_per_micron);
    database.get_instance_list().push_back(std::move(target));
  });
  database.set_cell_area(cell_area);

  const auto add_net = [&](const eccdb::DesignNet& source) {
    Net target;
    target.set_name(source.name);
    target.set_clock(source.use == eccdb::DesignSignalUse::kClock);
    target.set_pdn(false);
    target.set_power(source.use == eccdb::DesignSignalUse::kPower);
    target.set_ground(source.use == eccdb::DesignSignalUse::kGround);
    database.get_net_list().push_back(std::move(target));
  };
  netlist.forEachRegularNet([&](eccdb::DesignNetId, const eccdb::DesignNet& net) { add_net(net); });
  netlist.forEachSpecialNet([&](eccdb::DesignNetId id, const eccdb::DesignNet& net) {
    PGNet target;
    target.set_name(net.name);
    target.set_type(toPgNetType(net.use));
    for (const auto block_term : netlist.blockTerms(id)) {
      const auto& term = netlist.blockTerm(block_term);
      target.add_io_pin(term.name, toFpIoDirection(term.direction));
    }
    std::unordered_set<std::string_view> pin_names;
    for (const auto instance_term : netlist.instanceTerms(id)) {
      const auto& term = netlist.instanceTerm(instance_term);
      const auto& name = library.masterTermStorage().masterTerm(term.master_term).name;
      if (pin_names.insert(name).second) {
        target.add_instance_pin(name);
      }
    }
    database.get_pg_net_list().push_back(std::move(target));
  });

  netlist.forEachBlockTerm([&](eccdb::DesignBlockTermId id, const eccdb::DesignBlockTerm& source) {
    IOPin target;
    target.set_name(source.name);
    target.set_special_net(static_cast<bool>(source.special_net));
    const auto ports = netlist.blockPins(id);
    target.set_port_exist(!ports.empty());
    if (!ports.empty()) {
      const auto& first = netlist.blockPin(ports.front());
      target.set_coord(first.origin.x, first.origin.y);
      target.set_orient(toFpOrientation(first.orientation));
      target.set_placed(first.placement_status != eccdb::DesignPlacementStatus::kUnplaced);
      target.set_fixed(first.placement_status == eccdb::DesignPlacementStatus::kFixed);
      for (const auto port_id : ports) {
        const auto& port = netlist.blockPin(port_id);
        for (const auto rectangle : port.rectangles) {
          const auto& shape = netlist.blockPinRectangle(rectangle);
          IOPort wrapped;
          wrapped.set_layer_name(technology.layerInfo(shape.layer).name);
          wrapped.set_rect(shape.rectangle.ll_x, shape.rectangle.ll_y, shape.rectangle.ur_x, shape.rectangle.ur_y);
          wrapped.set_coord(port.origin.x, port.origin.y);
          wrapped.set_placed(port.placement_status != eccdb::DesignPlacementStatus::kUnplaced);
          target.get_port_list().push_back(std::move(wrapped));
        }
      }
    }
    database.get_io_pin_list().push_back(std::move(target));
  });
  rebuildNativeMaps(database);
}

void FPInterface::outputToEnTT()
{
  if (_database_context == nullptr || _database_context->technology() == nullptr || _database_context->library() == nullptr) {
    throw std::logic_error("iFP native adapter requires a complete DatabaseContext");
  }
  auto& context = *_database_context;
  auto& design = context.design();
  auto& database = FPDM.getDatabase();
  eccdb::FloorplanService floorplan{context};

  if (database.is_die_updated()) {
    floorplan.setDieArea({.boundary = rectangleBoundary(database.get_die())});
    database.set_die_updated(false);
  }
  if (database.is_core_updated()) {
    floorplan.setCoreArea({.boundary = rectangleBoundary(database.get_core())});
    std::vector<eccdb::DesignRow> rows;
    rows.reserve(database.get_new_row_list().size());
    for (const auto& row : database.get_new_row_list()) {
      const auto site = _database_context->library()->siteStorage().findSite(row.get_site_name());
      if (!site) {
        throw std::invalid_argument("iFP native adapter cannot find row site: " + row.get_site_name());
      }
      const auto& site_value = _database_context->library()->siteStorage().site(site);
      const auto width = row.get_width();
      if (width <= 0 || site_value.width <= 0 || width % site_value.width != 0) {
        throw std::invalid_argument("iFP native adapter row width is not site-aligned");
      }
      rows.push_back(eccdb::DesignRow{.name = row.get_name(),
                                      .site = site,
                                      .origin = {row.get_ll_x(), row.get_ll_y()},
                                      .orientation = toDesignOrientation(row.get_orient()),
                                      .repeat_count_x = static_cast<uint32_t>(width / site_value.width),
                                      .repeat_count_y = 1u,
                                      .step_x = site_value.width,
                                      .step_y = 0,
                                      .flags = eccdb::DesignRowFlag::kHasDo | eccdb::DesignRowFlag::kHasStep});
    }
    static_cast<void>(floorplan.replaceRows(std::move(rows)));
    database.set_core_updated(false);
  }
  if (database.is_track_updated()) {
    if (!design.globalStorage().hasDieArea()) {
      throw std::invalid_argument("iFP native adapter cannot output tracks without a die area");
    }
    const auto die = design.globalStorage().dieBounds();
    std::vector<eccdb::DesignTrackGrid> grids;
    grids.reserve(database.get_new_track_list().size() * 2u);
    for (const auto& track : database.get_new_track_list()) {
      const auto layer = requireRoutingLayer(*_database_context->technology(), track.get_layer_name());
      if (track.get_x_pitch() > 0) {
        const auto grid = boundedTrackGrid(die.ll_x, die.ur_x, track.get_x_offset(), track.get_x_pitch());
        if (grid.count > 0) {
          grids.push_back(eccdb::DesignTrackGrid{.axis = eccdb::DesignAxis::kX,
                                               .start = grid.start,
                                               .track_count = static_cast<uint32_t>(grid.count),
                                               .step = track.get_x_pitch(),
                                               .layers = {layer}});
        }
      }
      if (track.get_y_pitch() > 0) {
        const auto grid = boundedTrackGrid(die.ll_y, die.ur_y, track.get_y_offset(), track.get_y_pitch());
        if (grid.count > 0) {
          grids.push_back(eccdb::DesignTrackGrid{.axis = eccdb::DesignAxis::kY,
                                               .start = grid.start,
                                               .track_count = static_cast<uint32_t>(grid.count),
                                               .step = track.get_y_pitch(),
                                               .layers = {layer}});
        }
      }
    }
    static_cast<void>(floorplan.replaceTrackGrids(std::move(grids)));
    database.set_track_updated(false);
  }

  writeInstances(context, database);
  writeIoPins(context, database);
  writePgNets(context, database);
  writePgSegments(context, database);
}

}  // namespace ifp
