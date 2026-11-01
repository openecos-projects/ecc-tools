// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "FPInterfaceEccdb.h"

#include "FPInterface.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
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
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/via/component/ViaComponents.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "service/connectivity/ConnectivityService.h"
#include "service/floorplan/FloorplanService.h"
#include "service/geometry/PhysicalDesignService.h"
#include "service/instance/InstanceService.h"
#include "service/routing/RoutingService.h"
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

eccdb::TechRoutingLayerId requireRoutingLayer(const eccdb::PhysicalDesignService& physical, std::string_view name)
{
  const auto generic = physical.findLayer(name);
  const auto layer = eccdb::TechRoutingLayerId{generic.entity()};
  if (!generic || physical.layerKind(generic) != eccdb::GeometryLayerKind::kRouting) {
    throw std::invalid_argument("iFP native adapter requires a known routing layer: " + std::string{name});
  }
  return layer;
}

eccdb::DesignInstanceId requireInstance(const eccdb::PhysicalDesignService& physical, std::string_view name)
{
  const auto id = physical.findInstance(name);
  if (!id) {
    throw std::invalid_argument("iFP native adapter cannot find instance: " + std::string{name});
  }
  return id;
}

bool isIpfPhysicalInstanceName(std::string_view name) noexcept
{
  return name.starts_with("BNDRY_CAP_") || name.starts_with("BNDRY_CAP_TAP_") || name.starts_with("WELLTAP_");
}

std::string allocateIpfRerunName(const eccdb::PhysicalDesignService& physical,
                                 std::unordered_set<std::string>& reserved,
                                 std::string_view base)
{
  for (uint64_t suffix = 1; suffix != 0; ++suffix) {
    auto candidate = std::string{base} + "__IFP_RERUN_" + std::to_string(suffix);
    if (reserved.contains(candidate) || physical.findInstance(candidate)) {
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

// iFP needs a representative location, not a Pin Access candidate. For RECT
// pins, divide the sum of master-local endpoints once before placement, as
// iDB does. Polygon vertices supply a native fallback without changing the
// stored pin geometry or pretending that the anchor is a legal access point.
struct PinAnchor
{
  int64_t sum_x = 0;
  int64_t sum_y = 0;
  int64_t count = 0;

  void add(eccdb::Point point)
  {
    sum_x += point.x;
    sum_y += point.y;
    ++count;
  }

  eccdb::PhysicalGeometryVisitor visitor()
  {
    return {.rectangle = [&](const eccdb::PhysicalLayerRect& shape) {
              add({shape.rectangle.ll_x, shape.rectangle.ll_y});
              add({shape.rectangle.ur_x, shape.rectangle.ur_y});
            },
            .polygon = [&](const eccdb::PhysicalLayerPolygon& shape) {
              for (const auto point : shape.points) {
                add(point);
              }
            }};
  }

  eccdb::Point location(eccdb::Point fallback = {}) const
  {
    return count == 0 ? fallback : eccdb::Point{static_cast<int32_t>(sum_x / count), static_cast<int32_t>(sum_y / count)};
  }
};

// iFP keeps an auxiliary instance name on a top-level IOPin.  This is not a
// persistent DEF/OA relation: the legacy iDB implementation derives it from
// the IO pin's net by finding the instance-side pin on the same net.  Keep the
// same compatibility lookup at the adaptor boundary instead of adding an
// artificial DesignBlockPin -> DesignInstance ownership relation.
std::optional<std::string> findIoCellName(const eccdb::PhysicalDesignService& physical,
                                          eccdb::DesignBlockTermId block_term_id)
{
  const auto& netlist = physical.design().netlistStorage();
  const auto& block_term = netlist.blockTerm(block_term_id);
  const auto find_on_net = [&](eccdb::DesignNetId net) -> std::optional<std::string> {
    if (!net) {
      return std::nullopt;
    }
    std::optional<std::string> result;
    physical.forEachNetPin(net, {
        .instance_pin = [&](eccdb::DesignInstanceTermId,
                            const eccdb::DesignInstanceTerm&,
                            const eccdb::DesignInstance& instance,
                            const eccdb::LibraryMasterTerm& master_term) {
          // Match DataManager::getIoCellByIoPin(): the instance-side pin with
          // a different pin name is the IO-cell endpoint for this top-level
          // IO pin.  GCD/standard IO cases have one such endpoint.
          if (!result && master_term.name != block_term.name) {
            result = instance.name;
          }
        },
        .io_pin = {}});
    return result;
  };

  if (auto result = find_on_net(block_term.net); result) {
    return result;
  }
  return find_on_net(block_term.special_net);
}

void appendNetPins(const eccdb::PhysicalDesignService& physical, eccdb::DesignNetId net_id, Net& target)
{
  physical.forEachNetPin(net_id, {
      .instance_pin = [&](eccdb::DesignInstanceTermId, const eccdb::DesignInstanceTerm& term,
                           const eccdb::DesignInstance& instance, const eccdb::LibraryMasterTerm& master_term) {
        PinAnchor anchor;
        const auto visitor = anchor.visitor();
        for (const auto pin : physical.masterPins(term.master_term)) {
          physical.forEachMasterPinGeometry(pin, visitor);
        }
        const auto offset = anchor.location();
        NetPin pin;
        pin.set_instance_name(instance.name);
        pin.set_pin_name(master_term.name);
        pin.set_offset_x(offset.x);
        pin.set_offset_y(offset.y);
        const bool placed = instance.placement_status != eccdb::DesignPlacementStatus::kUnplaced;
        pin.set_placed(placed);
        if (placed) {
          const auto position = physical.placeInstanceMasterPoint(term.instance, offset);
          pin.set_coord(position.x, position.y);
        }
        target.get_net_pin_list().push_back(std::move(pin));
      },
      .io_pin = [&](eccdb::DesignBlockTermId term_id, const eccdb::DesignBlockTerm& term) {
        NetPin pin;
        pin.set_pin_name(term.name);
        pin.set_io(true);
        PinAnchor anchor;
        eccdb::Point fallback{};
        bool placed = false;
        physical.forEachBlockPin(term_id, [&](eccdb::DesignBlockPinId pin_id, const eccdb::DesignBlockPin& port) {
          if (port.placement_status == eccdb::DesignPlacementStatus::kUnplaced) {
            return;
          }
          if (!placed) {
            fallback = port.origin;
          }
          placed = true;
          physical.forEachBlockPinGeometry(pin_id, anchor.visitor());
        });
        pin.set_placed(placed);
        if (placed) {
          const auto position = anchor.location(fallback);
          pin.set_coord(position.x, position.y);
        }
        target.get_net_pin_list().push_back(std::move(pin));
      }});
  target.set_pin_num(static_cast<int32_t>(target.get_net_pin_list().size()));
}

void appendMacroPinRectangle(Instance& target, std::string_view pin_name, const eccdb::PhysicalLayerRect& rectangle,
                             const eccdb::PhysicalDesignService& physical)
{
  // iFP's PG model accepts only routing-layer rectangles.  PhysicalDesignService
  // also visits cut geometry and VIA expansion, so filter it here rather than
  // accidentally treating a cut as an iFP routing layer.
  if (physical.layerKind(rectangle.layer) != eccdb::GeometryLayerKind::kRouting) {
    return;
  }
  InstancePinShape shape;
  shape.set_pin_name(std::string{pin_name});
  shape.set_layer_name(physical.layerInfo(rectangle.layer).name);
  shape.set_rect(rectangle.rectangle.ll_x, rectangle.rectangle.ll_y, rectangle.rectangle.ur_x, rectangle.rectangle.ur_y);
  target.get_pin_shape_list().push_back(std::move(shape));
}

void appendMacroPinShapes(const eccdb::PhysicalDesignService& physical, const eccdb::DesignInstance& source,
                          std::span<const eccdb::DesignInstanceTermId> terms, Instance& target)
{
  const auto append_via = [&](std::string_view pin_name, const eccdb::PhysicalViaPlacement& placement) {
    const auto append_rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
      appendMacroPinRectangle(target, pin_name, rectangle, physical);
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
    const auto& instance_term = physical.instanceTerm(term_id);
    const auto& master_term = physical.masterTerm(instance_term.master_term);
    const auto append_geometry = [&](const eccdb::PhysicalLayerRect& rectangle) {
      appendMacroPinRectangle(target, master_term.name, rectangle, physical);
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

std::optional<eccdb::TechRoutingLayerId> firstRoutingLayer(const eccdb::PhysicalDesignService& physical)
{
  std::optional<eccdb::TechRoutingLayerId> first;
  physical.forEachRoutingLayer([&](auto id, auto, auto, const auto&, const auto&) {
    if (!first) first = id;
  });
  return first;
}

std::optional<eccdb::TechRoutingLayerId> macroTopRoutingLayer(const eccdb::PhysicalDesignService& physical,
                                                               eccdb::DesignInstanceId instance)
{
  std::optional<eccdb::TechRoutingLayerId> top;
  const auto consider_rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
    const auto routing = eccdb::TechRoutingLayerId{rectangle.layer.entity()};
    if (physical.layerKind(rectangle.layer) != eccdb::GeometryLayerKind::kRouting) {
      return;
    }
    const auto candidate_position = physical.layerPosition(rectangle.layer).value_or(0u);
    const auto top_position = top ? physical.layerPosition(eccdb::TechLayerId{top->entity()}).value_or(0u) : 0u;
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
  for (const auto term : physical.instanceTerms(instance)) {
    const auto& instance_term = physical.instanceTerm(term);
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
  const eccdb::PhysicalDesignService physical{context};
  eccdb::FloorplanService floorplan{context};

  for (auto& pin : database.get_io_pin_list()) {
    if (!pin.is_updated()) {
      continue;
    }
    auto term = physical.findBlockTerm(pin.get_name());
    if (!term) {
      // iFP updates existing terminals; IOPin does not carry the Net/USE/
      // direction metadata required to construct a new logical terminal.
      throw std::invalid_argument("iFP output references an unknown IO terminal: " + pin.get_name());
    }

    const auto status = placementStatus(pin);
    const auto origin = eccdb::Point{pin.get_x(), pin.get_y()};
    const auto orientation = toDesignOrientation(pin.get_orient());
    // iFP's port_exist tracks explicit DEF +PORT syntax, not whether any
    // physical geometry exists. Preserve it unless the tool updates it.
    const auto previous_ports = physical.blockPins(term);
    const bool previously_explicit = previous_ports.empty() ? pin.get_port_exist()
        : (physical.blockPin(previous_ports.front()).flags & eccdb::DesignIoPinPortFlag::kExplicit) != 0u;
    const bool explicit_ports = pin.is_port_exist_updated() ? pin.get_port_exist() : previously_explicit;
    const auto build_new_ports = [&]() {
      std::vector<eccdb::DesignBlockPinInput> inputs;
      inputs.reserve(pin.get_new_port_list().size());
      for (const auto& port : pin.get_new_port_list()) {
        const auto layer = requireRoutingLayer(physical, port.get_layer_name());
        if (explicit_ports || inputs.empty()) {
          // Explicit PORTs may have independent anchors. Implicit geometry
          // shares the PIN anchor, including IOPlacer's direct-location form.
          const auto port_origin = explicit_ports && port.get_placed() ? eccdb::Point{port.get_x(), port.get_y()} : origin;
          inputs.push_back(eccdb::DesignBlockPinInput{
              .flags = eccdb::DesignIoPinPortFlag::kHasPlacement | (explicit_ports ? eccdb::DesignIoPinPortFlag::kExplicit : 0u),
              .placement_status = status,
              .origin = port_origin,
              .orientation = orientation});
        }
        // Upstream emits all implicit geometry under one PIN, rather than
        // treating each new rectangle as a separate explicit PORT group.
        inputs.back().rectangles.push_back(eccdb::DesignPinRectangle{.layer = eccdb::TechLayerId{layer.entity()},
                                                                      .rectangle = toRect(port)});
      }
      return inputs;
    };

    // Match iFP's upstream writer: updated carries the complete new PORT
    // list, including an empty list that clears the old physical geometry.
    // Keep the logical terminal and its connectivity while replacing ports.
    static_cast<void>(floorplan.replaceAllBlockPinPorts(term, build_new_ports()));
    pin.get_new_port_list().clear();
    pin.set_updated(false);
  }
}

void writeInstances(eccdb::DatabaseContext& context, Database& database)
{
  const eccdb::PhysicalDesignService physical{context};
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
    auto id = physical.findInstance(instance.get_name());
    const bool generated = instance.is_new_instance();
    bool place_generated_instance = false;
    if (generated) {
      const auto master = physical.findMaster(instance.get_master_name());
      if (!master) {
        throw std::invalid_argument("iFP native adapter cannot find master: " + instance.get_master_name());
      }
      const auto& master_value = physical.master(master);
      if (id) {
        const auto& stored = physical.instance(id);
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
            instance.set_name(allocateIpfRerunName(physical, reserved_names, old_name));
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
      const auto& stored = physical.instance(id);
      const auto& master = physical.master(stored.master);
      instances.place(id, {instance.get_x(), instance.get_y()}, toDesignOrientation(instance.get_orient()), placementStatus(instance, isMacro(master.type)));
      instance.set_placement_updated(false);
    }
  }

  for (const auto& instance : database.get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }
    const auto id = requireInstance(physical, instance.get_name());
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

  const auto bottom_layer = firstRoutingLayer(physical);
  if (!bottom_layer) {
    return;
  }
  for (const auto& instance : database.get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }
    const auto id = requireInstance(physical, instance.get_name());
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
    const auto top_layer = macroTopRoutingLayer(physical, id);
    if (!top_layer) {
      continue;
    }
    floorplan.setInstanceRouteHalo(id, {.distance = left, .min_layer = *bottom_layer, .max_layer = *top_layer});
  }
}

void writePgNets(eccdb::DatabaseContext& context, Database& database)
{
  const eccdb::PhysicalDesignService physical{context};
  eccdb::ConnectivityService connectivity{context};
  // PGNet stores master pin names, rather than one entry per instance.
  // Build their occurrence index once for all PG nets.
  std::unordered_map<std::string_view, std::vector<eccdb::DesignInstanceTermId>> terms_by_name;
  physical.forEachInstanceTerm([&](eccdb::DesignInstanceTermId term_id, const eccdb::DesignInstanceTerm& term) {
    const auto& name = physical.masterTerm(term.master_term).name;
    terms_by_name[name].push_back(term_id);
  });
  for (const auto& pg_net : database.get_pg_net_list()) {
    auto id = physical.findSpecialNet(pg_net.get_name());
    if (!id) {
      id = connectivity.createNet(eccdb::DesignNet{.name = pg_net.get_name(), .use = toSignalUse(pg_net.get_type()),
                                                    .source = eccdb::DesignNetSource::kDist},
                                  true);
    }
    for (const auto& [pin_name, direction] : pg_net.get_io_pin_name_to_direction_map()) {
      auto term = physical.findBlockTerm(pin_name);
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

struct PgViaEdge
{
  eccdb::TechRoutingLayerId entry;
  eccdb::TechRoutingLayerId exit;
  eccdb::TechCutLayerId cut;
};

struct PgViaTechnology
{
  struct RoutingLayer
  {
    eccdb::TechRoutingLayerId id;
    uint32_t position;
  };
  struct CutLayer
  {
    eccdb::TechCutLayerId id;
    uint32_t position;
  };

  std::vector<RoutingLayer> routing_layers;
  std::vector<CutLayer> cut_layers;
  std::unordered_map<std::string, eccdb::TechViaMasterId> tech_vias;
  std::unordered_map<uint64_t, std::vector<eccdb::TechViaRuleGenerateId>> rules_by_cut;

  explicit PgViaTechnology(const eccdb::PhysicalDesignService& physical)
  {
    physical.forEachRoutingLayer([&](auto id, uint32_t position, uint32_t, const auto&, const auto&) {
      routing_layers.push_back({id, position});
    });
    physical.forEachCutLayer([&](auto id, uint32_t position, const auto&, const auto&) {
      cut_layers.push_back({id, position});
    });
    physical.forEachTechVia([&](auto id, const auto& via, const auto&) { tech_vias.try_emplace(via.name, id); });
    physical.forEachTechViaRuleGenerate([&](auto id, const auto&, const auto&, const auto& cut, const auto&) {
      rules_by_cut[cut.layer.packed()].push_back(id);
    });
    for (auto& [cut, rules] : rules_by_cut) {
      static_cast<void>(cut);
      std::sort(rules.begin(), rules.end(), [&](auto lhs, auto rhs) {
        const auto& left = physical.techViaRuleGenerate(lhs);
        const auto& right = physical.techViaRuleGenerate(rhs);
        if (left.isDefault() != right.isDefault()) {
          return left.isDefault();
        }
        return left.name < right.name;
      });
    }
  }
};

std::vector<PgViaEdge> pgViaEdges(const eccdb::PhysicalDesignService& physical, const PgViaTechnology& technology,
                                  const PGSegment& segment)
{
  const auto entry = requireRoutingLayer(physical, segment.get_bottom_layer_name());
  const auto exit = requireRoutingLayer(physical, segment.get_top_layer_name());
  if (entry == exit) {
    throw std::invalid_argument("iFP native PG VIA requires distinct routing layers");
  }

  const auto find_index = [&](eccdb::TechRoutingLayerId layer) {
    const auto found = std::find_if(technology.routing_layers.begin(), technology.routing_layers.end(),
                                    [layer](const auto& candidate) { return candidate.id == layer; });
    if (found == technology.routing_layers.end()) {
      throw std::invalid_argument("iFP native PG VIA routing layer is absent from the technology stack");
    }
    return static_cast<std::size_t>(std::distance(technology.routing_layers.begin(), found));
  };
  const auto entry_index = find_index(entry);
  const auto exit_index = find_index(exit);

  eccdb::TechCutLayerId requested_cut;
  if (!segment.get_cut_layer_name().empty()) {
    const auto generic = physical.findLayer(segment.get_cut_layer_name());
    if (!generic || physical.layerKind(generic) != eccdb::GeometryLayerKind::kCut) {
      throw std::invalid_argument("iFP native PG VIA references an unknown cut layer: " + segment.get_cut_layer_name());
    }
    requested_cut = eccdb::TechCutLayerId{generic.entity()};
  }

  std::vector<PgViaEdge> edges;
  const auto first = std::min(entry_index, exit_index);
  const auto last = std::max(entry_index, exit_index);
  edges.reserve(last - first);
  bool matched_requested_cut = !requested_cut;
  for (auto index = first; index < last; ++index) {
    const auto& lower = technology.routing_layers[index];
    const auto& upper = technology.routing_layers[index + 1];
    std::vector<eccdb::TechCutLayerId> cuts;
    for (const auto& cut : technology.cut_layers) {
      if (cut.position > lower.position && cut.position < upper.position) {
        cuts.push_back(cut.id);
      }
    }
    eccdb::TechCutLayerId selected_cut;
    if (requested_cut && std::find(cuts.begin(), cuts.end(), requested_cut) != cuts.end()) {
      selected_cut = requested_cut;
      matched_requested_cut = true;
    } else if (cuts.size() == 1u) {
      selected_cut = cuts.front();
    } else {
      throw std::invalid_argument("iFP native PG VIA cannot resolve a unique cut layer between "
                                  + physical.layerInfo(eccdb::TechLayerId{lower.id.entity()}).name + " and "
                                  + physical.layerInfo(eccdb::TechLayerId{upper.id.entity()}).name);
    }
    edges.push_back({lower.id, upper.id, selected_cut});
  }
  if (!matched_requested_cut) {
    throw std::invalid_argument("iFP native PG VIA cut layer is outside the requested routing stack: "
                                + segment.get_cut_layer_name());
  }
  if (entry_index > exit_index) {
    std::reverse(edges.begin(), edges.end());
    for (auto& edge : edges) {
      std::swap(edge.entry, edge.exit);
    }
  }
  return edges;
}

std::optional<GeneratedPgViaSpec> generatedPgViaSpec(const eccdb::PhysicalDesignService& physical,
                                                    const PgViaTechnology& technology, const PgViaEdge& edge,
                                                    const PGSegment& segment)
{
  const auto candidates = technology.rules_by_cut.find(edge.cut.packed());
  if (candidates == technology.rules_by_cut.end()) {
    return std::nullopt;
  }

  int64_t unconditional_spacing = 0;
  int64_t center_spacing = 0;
  physical.forEachCutSpacingRule(edge.cut, [&](eccdb::TechCutSpacingRuleId, const eccdb::TechCutSpacingRule& spacing) {
    if (spacing.flags == 0u) {
      unconditional_spacing = std::max(unconditional_spacing, static_cast<int64_t>(spacing.spacing));
    } else if (spacing.flags == eccdb::TechCutSpacingRuleFlag::kCenterToCenter) {
      center_spacing = std::max(center_spacing, static_cast<int64_t>(spacing.spacing));
    }
  });

  for (const auto selected : candidates->second) {
    const auto& bottom = physical.techViaRuleGenerateBottomLayer(selected);
    const auto& cut = physical.techViaRuleGenerateCutLayer(selected);
    const auto& top = physical.techViaRuleGenerateTopLayer(selected);
    if (bottom.layer.kind != eccdb::TechConductorLayerKind::kRouting
        || top.layer.kind != eccdb::TechConductorLayerKind::kRouting) {
      continue;
    }
    const auto bottom_layer = eccdb::TechRoutingLayerId{bottom.layer.entity};
    const auto top_layer = eccdb::TechRoutingLayerId{top.layer.entity};
    if (!((bottom_layer == edge.entry && top_layer == edge.exit) || (bottom_layer == edge.exit && top_layer == edge.entry))) {
      continue;
    }

    int64_t cut_size_x = static_cast<int64_t>(cut.cut_rect.ur_x) - cut.cut_rect.ll_x;
    int64_t cut_size_y = static_cast<int64_t>(cut.cut_rect.ur_y) - cut.cut_rect.ll_y;
    if (cut_size_x <= 0 || cut_size_y <= 0) {
      const auto& cut_layer = physical.cutLayer(cut.layer);
      cut_size_x = cut_layer.width;
      cut_size_y = cut_layer.width;
    }
    if (cut_size_x <= 0 || cut_size_y <= 0 || cut_size_x > std::numeric_limits<int32_t>::max()
        || cut_size_y > std::numeric_limits<int32_t>::max()) {
      continue;
    }
    // GENERATE SPACING is pitch; generated-VIA CUTSPACING is the gap.
    const int64_t pitch_x = std::max({static_cast<int64_t>(cut.spacing_x), cut_size_x + unconditional_spacing, center_spacing});
    const int64_t pitch_y = std::max({static_cast<int64_t>(cut.spacing_y), cut_size_y + unconditional_spacing, center_spacing});
    const int64_t cut_spacing_x = pitch_x - cut_size_x;
    const int64_t cut_spacing_y = pitch_y - cut_size_y;
    if (cut_spacing_x > std::numeric_limits<int32_t>::max() || cut_spacing_y > std::numeric_limits<int32_t>::max()) {
      continue;
    }
    const int64_t requested_width = segment.get_via_width() > 0 ? segment.get_via_width() : cut_size_x;
    const int64_t requested_height = segment.get_via_height() > 0 ? segment.get_via_height() : cut_size_y;

    struct Choice
    {
      uint32_t rows = 0;
      uint32_t columns = 0;
      uint64_t cuts = 0;
    };
    Choice best;
    for (int bottom_swap = 0; bottom_swap < 2; ++bottom_swap) {
      const int64_t bottom_x = std::max(bottom_swap == 0 ? bottom.enclosure_overhang1 : bottom.enclosure_overhang2, 0);
      const int64_t bottom_y = std::max(bottom_swap == 0 ? bottom.enclosure_overhang2 : bottom.enclosure_overhang1, 0);
      for (int top_swap = 0; top_swap < 2; ++top_swap) {
        const int64_t top_x = std::max(top_swap == 0 ? top.enclosure_overhang1 : top.enclosure_overhang2, 0);
        const int64_t top_y = std::max(top_swap == 0 ? top.enclosure_overhang2 : top.enclosure_overhang1, 0);
        const int64_t minimum_width = cut_size_x + 2 * std::max(bottom_x, top_x);
        const int64_t minimum_height = cut_size_y + 2 * std::max(bottom_y, top_y);
        if (requested_width < minimum_width || requested_height < minimum_height) {
          continue;
        }
        const auto columns = static_cast<uint32_t>(1 + (requested_width - minimum_width) / pitch_x);
        const auto rows = static_cast<uint32_t>(1 + (requested_height - minimum_height) / pitch_y);
        const auto cuts = static_cast<uint64_t>(rows) * columns;
        if (cuts > best.cuts) {
          best = Choice{.rows = rows, .columns = columns, .cuts = cuts};
        }
      }
    }
    if (best.cuts == 0) {
      continue;
    }
    const int64_t array_width = cut_size_x + static_cast<int64_t>(best.columns - 1u) * pitch_x;
    const int64_t array_height = cut_size_y + static_cast<int64_t>(best.rows - 1u) * pitch_y;
    const auto enclosure_x = static_cast<int32_t>((requested_width - array_width) / 2);
    const auto enclosure_y = static_cast<int32_t>((requested_height - array_height) / 2);
    return GeneratedPgViaSpec{.rule = selected,
                              .bottom_layer = bottom_layer,
                              .cut_layer = cut.layer,
                              .top_layer = top_layer,
                              .entry_layer = edge.entry,
                              .exit_layer = edge.exit,
                              .cut_size_x = static_cast<int32_t>(cut_size_x),
                              .cut_size_y = static_cast<int32_t>(cut_size_y),
                              .cut_spacing_x = static_cast<int32_t>(cut_spacing_x),
                              .cut_spacing_y = static_cast<int32_t>(cut_spacing_y),
                              .bottom_enclosure_x = enclosure_x,
                              .bottom_enclosure_y = enclosure_y,
                              .top_enclosure_x = enclosure_x,
                              .top_enclosure_y = enclosure_y,
                              .rows = best.rows,
                              .columns = best.columns};
  }
  return std::nullopt;
}

bool sameGeneratedPgVia(const eccdb::DesignVia& via, const GeneratedPgViaSpec& spec) noexcept
{
  if (via.flags != eccdb::DesignViaFlag::kGenerated) {
    return false;
  }
  const auto& generated = via.generated;
  return generated.flags == eccdb::DesignGeneratedViaFlag::kHasRowCol && generated.via_rule == spec.rule
         && generated.bottom_layer == spec.bottom_layer && generated.cut_layer == spec.cut_layer
         && generated.top_layer == spec.top_layer && generated.cut_size_x == spec.cut_size_x && generated.cut_size_y == spec.cut_size_y
         && generated.cut_spacing_x == spec.cut_spacing_x && generated.cut_spacing_y == spec.cut_spacing_y
         && generated.bottom_enclosure_x == spec.bottom_enclosure_x && generated.bottom_enclosure_y == spec.bottom_enclosure_y
         && generated.top_enclosure_x == spec.top_enclosure_x && generated.top_enclosure_y == spec.top_enclosure_y
         && generated.row_count == spec.rows && generated.column_count == spec.columns;
}

eccdb::DesignViaId getOrCreateGeneratedPgVia(eccdb::RoutingService& routing, const PgViaTechnology& technology,
                                             const GeneratedPgViaSpec& spec, std::string_view preferred_name,
                                             std::unordered_map<std::string, eccdb::DesignViaId>& cache)
{
  // iDB names generated PG vias after the cut layer and requested enclosure
  // dimensions (for example VIA1_1000x160).  Keep that name as the canonical
  // design-VIA name; the fallback suffix is only for a real name collision.
  const auto key = std::string{preferred_name} + ":" + std::to_string(spec.rule.packed()) + ":"
                   + std::to_string(spec.cut_size_x) + "x"
                   + std::to_string(spec.cut_size_y) + ":" + std::to_string(spec.rows) + "x" + std::to_string(spec.columns)
                   + ":" + std::to_string(spec.cut_spacing_x) + "," + std::to_string(spec.cut_spacing_y)
                   + ":" + std::to_string(spec.bottom_enclosure_x) + "," + std::to_string(spec.bottom_enclosure_y)
                   + ":" + std::to_string(spec.top_enclosure_x) + "," + std::to_string(spec.top_enclosure_y);
  if (const auto found = cache.find(key); found != cache.end()) {
    return found->second;
  }

  const auto base_name = std::string{preferred_name};
  auto name = base_name;
  eccdb::DesignViaId existing;
  for (uint32_t suffix = 1;; ++suffix) {
    existing = routing.findVia(name);
    if (!technology.tech_vias.contains(name) && (!existing || sameGeneratedPgVia(routing.via(existing), spec))) {
      break;
    }
    name = base_name + "_" + std::to_string(suffix);
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

bool compatiblePgVia(const eccdb::PhysicalDesignService& physical, const PgViaEdge& edge, const eccdb::DesignWireVia& via)
{
  try {
    const auto layers = physical.routingViaLayers(via.tech_via, via.design_via);
    if (!((layers.bottom == edge.entry && layers.top == edge.exit) || (layers.bottom == edge.exit && layers.top == edge.entry))) {
      return false;
    }

    bool entry_geometry = false;
    bool exit_geometry = false;
    bool cut_geometry = false;
    bool other_cut_geometry = false;
    const auto consider = [&](eccdb::TechLayerId layer, bool has_area) {
      if (!has_area) {
        return;
      }
      if (layer == eccdb::TechLayerId{edge.entry.entity()}) {
        entry_geometry = true;
      } else if (layer == eccdb::TechLayerId{edge.exit.entity()}) {
        exit_geometry = true;
      } else if (physical.layerKind(layer) == eccdb::GeometryLayerKind::kCut) {
        if (layer == eccdb::TechLayerId{edge.cut.entity()}) {
          cut_geometry = true;
        } else {
          other_cut_geometry = true;
        }
      }
    };
    const eccdb::PhysicalGeometryVisitor visitor{
        .rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
          consider(rectangle.layer, rectangle.rectangle.ll_x < rectangle.rectangle.ur_x
                                        && rectangle.rectangle.ll_y < rectangle.rectangle.ur_y);
        },
        .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
          if (polygon.points.size() < 3u) {
            return;
          }
          int32_t min_x = polygon.points.front().x;
          int32_t min_y = polygon.points.front().y;
          int32_t max_x = min_x;
          int32_t max_y = min_y;
          for (const auto point : polygon.points) {
            min_x = std::min(min_x, point.x);
            min_y = std::min(min_y, point.y);
            max_x = std::max(max_x, point.x);
            max_y = std::max(max_y, point.y);
          }
          consider(polygon.layer, min_x < max_x && min_y < max_y);
        }};
    if (via.design_via) {
      physical.forEachDesignViaGeometry(via.design_via, visitor);
    } else {
      physical.forEachTechViaGeometry(via.tech_via, visitor);
    }
    return entry_geometry && exit_geometry && cut_geometry && !other_cut_geometry;
  } catch (const std::logic_error&) {
    return false;
  } catch (const std::overflow_error&) {
    return false;
  }
}

eccdb::DesignWireVia resolvePgVia(const eccdb::PhysicalDesignService& physical, eccdb::RoutingService& routing,
                                  const PgViaTechnology& technology, const PgViaEdge& edge, const PGSegment& segment,
                                  std::unordered_map<std::string, eccdb::DesignWireVia>& resolved,
                                  std::unordered_map<std::string, eccdb::DesignViaId>& generated)
{
  const auto& cut_name = physical.layerInfo(eccdb::TechLayerId{edge.cut.entity()}).name;
  const auto name = cut_name + "_" + std::to_string(segment.get_via_width()) + "x" + std::to_string(segment.get_via_height());
  if (const auto found = resolved.find(name); found != resolved.end()) {
    return found->second;
  }

  const auto design_via = routing.findVia(name);
  const auto tech = technology.tech_vias.find(name);
  const bool has_tech_via = tech != technology.tech_vias.end();
  // A DEF reference cannot disambiguate Design/Tech VIAs with the same name.
  if (static_cast<bool>(design_via) != has_tech_via) {
    const auto candidate = design_via ? eccdb::DesignWireVia{.design_via = design_via}
                                      : eccdb::DesignWireVia{.tech_via = tech->second};
    if (compatiblePgVia(physical, edge, candidate)) {
      resolved.emplace(name, candidate);
      return candidate;
    }
  }

  const auto spec = generatedPgViaSpec(physical, technology, edge, segment);
  if (!spec) {
    throw std::invalid_argument("iFP native PG VIA has no compatible existing VIA or satisfiable VIARULE GENERATE for "
                                + physical.layerInfo(eccdb::TechLayerId{edge.entry.entity()}).name + " to "
                                + physical.layerInfo(eccdb::TechLayerId{edge.exit.entity()}).name + " using " + cut_name
                                + " (requested " + std::to_string(segment.get_via_width()) + "x"
                                + std::to_string(segment.get_via_height()) + ")");
  }
  const eccdb::DesignWireVia result{.design_via = getOrCreateGeneratedPgVia(routing, technology, *spec, name, generated)};
  resolved.emplace(name, result);
  return result;
}

template <typename AdjustStripe>
void writePgSegments(eccdb::DatabaseContext& context, Database& database, AdjustStripe&& adjust_stripe)
{
  const eccdb::PhysicalDesignService physical{context};
  std::optional<PgViaTechnology> via_technology;
  eccdb::RoutingService routing{context};
  std::map<std::string, eccdb::DesignWireId> wires;
  std::unordered_map<std::string, eccdb::DesignViaId> generated_vias;
  std::unordered_map<std::string, eccdb::DesignWireVia> resolved_vias;
  std::map<std::string, std::map<int32_t, std::vector<PGSegment*>>> stripes;
  for (auto& segment : database.get_pg_segment_list()) {
    if (segment.get_generated() && segment.get_type() == PGSegmentType::kStripe) {
      const auto coordinate = segment.is_vertical() ? segment.get_start_x() : segment.get_start_y();
      stripes[segment.get_net_name() + "|" + segment.get_layer_name()][coordinate].push_back(&segment);
    }
  }

  struct PreparedVia
  {
    eccdb::DesignWireVia via;
    eccdb::TechRoutingLayerId entry_layer;
    eccdb::TechRoutingLayerId exit_layer;
  };
  std::unordered_map<const PGSegment*, std::vector<PreparedVia>> prepared_vias;
  for (auto& segment : database.get_pg_segment_list()) {
    if (!segment.get_generated() || segment.get_type() != PGSegmentType::kVia) {
      continue;
    }
    if (!via_technology) {
      via_technology.emplace(physical);
    }
    const auto edges = pgViaEdges(physical, *via_technology, segment);
    auto& prepared = prepared_vias[&segment];
    prepared.reserve(edges.size());
    for (const auto& edge : edges) {
      const auto via = resolvePgVia(physical, routing, *via_technology, edge, segment, resolved_vias, generated_vias);
      prepared.push_back({via, edge.entry, edge.exit});
      // Reuse iFP's pure stripe-coverage algorithm. EccDB supplies concrete
      // enclosure geometry through its Service, including every stacked edge.
      const eccdb::PhysicalGeometryVisitor visitor{
          .rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
            if (physical.layerKind(rectangle.layer) == eccdb::GeometryLayerKind::kRouting) {
              adjust_stripe(stripes, segment, physical.layerInfo(rectangle.layer).name, rectangle.rectangle);
            }
          },
          .polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
            if (polygon.points.empty() || physical.layerKind(polygon.layer) != eccdb::GeometryLayerKind::kRouting) {
              return;
            }
            eccdb::Rect bounds{polygon.points.front().x, polygon.points.front().y, polygon.points.front().x,
                               polygon.points.front().y};
            for (const auto point : polygon.points) {
              bounds.ll_x = std::min(bounds.ll_x, point.x);
              bounds.ll_y = std::min(bounds.ll_y, point.y);
              bounds.ur_x = std::max(bounds.ur_x, point.x);
              bounds.ur_y = std::max(bounds.ur_y, point.y);
            }
            adjust_stripe(stripes, segment, physical.layerInfo(polygon.layer).name, bounds);
          }};
      const eccdb::Point origin{segment.get_start_x(), segment.get_start_y()};
      if (via.design_via) {
        physical.forEachDesignViaGeometry(via.design_via, origin, eccdb::DesignOrientation::kN, visitor);
      } else {
        physical.forEachTechViaGeometry(via.tech_via, origin, eccdb::DesignOrientation::kN, visitor);
      }
    }
  }

  for (auto& segment : database.get_pg_segment_list()) {
    if (!segment.get_generated()) {
      continue;
    }
    const auto net = physical.findSpecialNet(segment.get_net_name());
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
      for (const auto& prepared : prepared_vias.at(&segment)) {
        const eccdb::DesignRouteShape shape{
            .wire = wire_it->second,
            .layer = prepared.entry_layer,
            .flags = eccdb::DesignWirePathFlag::kHasWidth | eccdb::DesignWirePathFlag::kHasShape,
            .geometry = eccdb::DesignRouteVia{.origin = {.position = {segment.get_start_x(), segment.get_start_y()}},
                                               .via = prepared.via,
                                               .exit_layer = prepared.exit_layer}};
        static_cast<void>(routing.createShape(wire_it->second, shape,
                                              eccdb::DesignRouteShapeAttributes{.width = 0, .shape = "STRIPE"}));
      }
    } else {
      if (!segment.is_line()) {
        throw std::runtime_error("iFP native PG output encountered an unsupported segment type");
      }
      const auto layer = requireRoutingLayer(physical, segment.get_layer_name());
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

class EccdbFpSource final : public FPDesignSource
{
 public:
  EccdbFpSource(FPInterface& interface, eccdb::DatabaseContext* context)
      : _interface(&interface), _database_context(context)
  {
  }

  void wrapDatabase(Database& database) override;
  void output(Database& database) override;

 private:
  FPInterface* _interface;
  eccdb::DatabaseContext* _database_context;
};

void EccdbFpSource::wrapDatabase(Database& database)
{
  if (_database_context == nullptr || _database_context->technology() == nullptr || _database_context->library() == nullptr) {
    throw std::logic_error("iFP native adapter requires a complete DatabaseContext");
  }
  auto& context = *_database_context;
  const eccdb::PhysicalDesignService physical{context};
  clearNativeInput(database);

  if (!physical.hasDesignInfo()) {
    throw std::invalid_argument("iFP native adapter requires DesignInfo");
  }
  const auto& info = physical.designInfo();
  if (info.database_units_per_micron <= 0) {
    throw std::invalid_argument("iFP native adapter requires positive design DBU");
  }
  database.set_design_name(info.name);
  database.set_micron_dbu(info.database_units_per_micron);
  database.set_manufacture_grid(physical.manufacturingGrid().value_or(1));

  physical.forEachSite([&](eccdb::LibrarySiteId, const eccdb::LibrarySite& source) {
    Site target;
    target.set_name(source.name);
    target.set_width(source.width);
    target.set_height(source.height);
    database.get_site_map().insert_or_assign(source.name, std::move(target));
  });
  physical.forEachMaster([&](eccdb::LibraryCellMasterId, const eccdb::LibraryCellMaster& source) {
    CellMaster target;
    target.set_name(source.name);
    target.set_width(static_cast<int32_t>(source.width));
    target.set_height(static_cast<int32_t>(source.height));
    target.set_pad(isPad(source.type));
    target.set_pad_filler(source.pad_filler || isPadFiller(source.type));
    target.set_spacer(source.core_filler || isSpacer(source.type));
    target.set_corner(source.site.has_value() && physical.site(*source.site).site_class == eccdb::LibrarySiteClass::kCorner);
    database.get_cell_master_map().insert_or_assign(source.name, std::move(target));
  });

  physical.forEachRoutingLayer([&](eccdb::TechRoutingLayerId layer, uint32_t sequence_index, uint32_t routing_level,
                                   const eccdb::TechLayerInfo& layer_info, const eccdb::TechRoutingLayer& source) {
    RoutingLayer target;
    target.set_name(layer_info.name);
    // Like iDB's routing-layer ID, this is a routing-only ordinal; order
    // includes cut/other layers. Neither field is an EccDB entity ID.
    target.set_layer_idx(static_cast<int32_t>(routing_level - 1));
    target.set_order(static_cast<int32_t>(sequence_index));
    target.set_pitch_x(source.pitch_x);
    target.set_pitch_y(source.pitch_y);
    target.set_min_width(source.min_width > 0 ? source.min_width : source.width);
    target.set_min_area(std::max<int64_t>(source.area, 0));
    target.set_spacing(physical.routingSpacing(layer, 0));
    target.set_prefer_track_offset(source.direction == eccdb::TechRoutingDirection::kHorizontal ? source.offset_y : source.offset_x);
    target.set_prefer_track_pitch(source.direction == eccdb::TechRoutingDirection::kHorizontal ? source.pitch_y : source.pitch_x);
    target.set_nonprefer_track_pitch(source.direction == eccdb::TechRoutingDirection::kHorizontal ? source.pitch_x : source.pitch_y);
    target.set_prefer_direction(source.direction == eccdb::TechRoutingDirection::kHorizontal
                                    ? Direction::kHorizontal
                                    : (source.direction == eccdb::TechRoutingDirection::kVertical ? Direction::kVertical : Direction::kNone));
    database.get_routing_layer_list().push_back(std::move(target));
  });

  if (physical.hasDieArea()) {
    const auto bounds = physical.dieBounds();
    database.get_die().set_rect(bounds.ll_x, bounds.ll_y, bounds.ur_x, bounds.ur_y);
  }
  if (physical.hasCoreArea()) {
    const auto bounds = physical.coreBounds();
    database.get_core().set_rect(bounds.ll_x, bounds.ll_y, bounds.ur_x, bounds.ur_y);
  }
  physical.forEachRow([&](eccdb::DesignRowId row_id, const eccdb::DesignRow& source) {
    const auto& site = physical.site(source.site);
    const auto bounds = physical.rowBounds(row_id);
    Row target;
    target.set_name(source.name);
    target.set_site_name(site.name);
    target.set_site_origin_x(source.origin.x);
    target.set_y(source.origin.y);
    target.set_orient(toFpOrientation(source.orientation));
    target.set_rect(bounds.ll_x, bounds.ll_y, bounds.ur_x, bounds.ur_y);
    database.get_row_list().push_back(std::move(target));
  });

  double cell_area = 0.0;
  physical.forEachInstance([&](eccdb::DesignInstanceId, const eccdb::DesignInstance& source,
                              std::span<const eccdb::DesignInstanceTermId> terms) {
    const auto& master = physical.master(source.master);
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
      appendMacroPinShapes(physical, source, terms, target);
    }
    cell_area += static_cast<double>(master.width) * static_cast<double>(master.height)
                 / (static_cast<double>(info.database_units_per_micron) * info.database_units_per_micron);
    database.get_instance_list().push_back(std::move(target));
  });
  database.set_cell_area(cell_area);

  const auto add_net = [&](eccdb::DesignNetId net_id, const eccdb::DesignNet& source) {
    Net target;
    target.set_name(source.name);
    target.set_clock(source.use == eccdb::DesignSignalUse::kClock);
    target.set_pdn(false);
    target.set_power(source.use == eccdb::DesignSignalUse::kPower);
    target.set_ground(source.use == eccdb::DesignSignalUse::kGround);
    appendNetPins(physical, net_id, target);
    database.get_net_list().push_back(std::move(target));
  };
  physical.forEachRegularNet([&](eccdb::DesignNetId id, const eccdb::DesignNet& net) { add_net(id, net); });
  physical.forEachSpecialNet([&](eccdb::DesignNetId id, const eccdb::DesignNet& net) {
    PGNet target;
    target.set_name(net.name);
    target.set_type(toPgNetType(net.use));
    std::unordered_set<std::string_view> pin_names;
    physical.forEachNetPin(id, {.instance_pin = [&](eccdb::DesignInstanceTermId, const eccdb::DesignInstanceTerm&,
                                                   const eccdb::DesignInstance&, const eccdb::LibraryMasterTerm& term) {
                                 if (pin_names.insert(term.name).second) {
                                   target.add_instance_pin(term.name);
                                 }
                               },
                               .io_pin = [&](eccdb::DesignBlockTermId, const eccdb::DesignBlockTerm& term) {
                                 target.add_io_pin(term.name, toFpIoDirection(term.direction));
                               }});
    database.get_pg_net_list().push_back(std::move(target));
  });

  physical.forEachBlockTerm([&](eccdb::DesignBlockTermId id, const eccdb::DesignBlockTerm& source) {
    IOPin target;
    target.set_name(source.name);
    if (const auto instance_name = findIoCellName(physical, id); instance_name) {
      target.set_instance_name(*instance_name);
    }
    target.set_special_net(static_cast<bool>(source.special_net));
    const auto ports = physical.blockPins(id);
    target.set_port_exist(!ports.empty()
                          && (physical.blockPin(ports.front()).flags & eccdb::DesignIoPinPortFlag::kExplicit) != 0u);
    if (!ports.empty()) {
      const auto& first = physical.blockPin(ports.front());
      target.set_coord(first.origin.x, first.origin.y);
      target.set_orient(toFpOrientation(first.orientation));
      target.set_placed(first.placement_status != eccdb::DesignPlacementStatus::kUnplaced);
      target.set_fixed(first.placement_status == eccdb::DesignPlacementStatus::kFixed);
      for (const auto port_id : ports) {
        const auto& port = physical.blockPin(port_id);
        for (const auto rectangle : port.rectangles) {
          const auto& shape = physical.blockPinRectangle(rectangle);
          IOPort wrapped;
          wrapped.set_layer_name(physical.layerInfo(shape.layer).name);
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

void EccdbFpSource::output(Database& database)
{
  if (_database_context == nullptr || _database_context->technology() == nullptr || _database_context->library() == nullptr) {
    throw std::logic_error("iFP native adapter requires a complete DatabaseContext");
  }
  auto& context = *_database_context;
  const eccdb::PhysicalDesignService physical{context};
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
      const auto site = physical.findSite(row.get_site_name());
      if (!site) {
        throw std::invalid_argument("iFP native adapter cannot find row site: " + row.get_site_name());
      }
      const auto& site_value = physical.site(site);
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
    if (!physical.hasDieArea()) {
      throw std::invalid_argument("iFP native adapter cannot output tracks without a die area");
    }
    const auto die = physical.dieBounds();
    std::vector<eccdb::DesignTrackGrid> grids;
    grids.reserve(database.get_new_track_list().size() * 2u);
    for (const auto& track : database.get_new_track_list()) {
      const auto layer = requireRoutingLayer(physical, track.get_layer_name());
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
  writePgSegments(context, database, [&](auto& stripes, PGSegment& via, const std::string& layer, eccdb::Rect enclosure) {
    static_cast<void>(_interface->adjustStripeSegmentListByViaEnclosure(stripes, via, layer, enclosure.ll_x, enclosure.ll_y,
                                                                         enclosure.ur_x, enclosure.ur_y));
  });
}

}  // namespace ifp

namespace eccdb::adapters {

void bindFpSource(ifp::FPInterface& interface, DatabaseContext* context)
{
  if (context == nullptr) {
    interface.setDesignSource(std::shared_ptr<ifp::FPDesignSource>{});
    return;
  }
  interface.setDesignSource(std::make_shared<ifp::EccdbFpSource>(interface, context));
}

}  // namespace eccdb::adapters
