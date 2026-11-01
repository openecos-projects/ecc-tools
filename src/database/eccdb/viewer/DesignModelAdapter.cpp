// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "viewer/DesignModelAdapter.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <span>
#include <unordered_set>
#include <utility>

#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/geometry/DesignGeometrySpatialIndex.h"
#include "design/geometry/DesignGeometryStorage.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "geometry/GeometryEntity.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {
namespace {

using namespace ModelEntityKind;

struct ModelContext
{
  const DesignStore& design;
  const LibraryStore* library = nullptr;
  const DesignRegistry::registry_type& design_registry;
  const LibraryRegistry::registry_type& library_registry;
  const TechRegistry::registry_type& tech_registry;
};

[[nodiscard]] ModelObjectRef ref(std::string_view kind, uint64_t id)
{
  return ModelObjectRef{.kind = std::string(kind), .id = id};
}

template <typename Id>
[[nodiscard]] uint64_t packed(Id id)
{
  return static_cast<uint64_t>(id.packed());
}

[[nodiscard]] DesignEntity designEntity(uint64_t id)
{
  return static_cast<DesignEntity>(id);
}

[[nodiscard]] LibraryEntity libraryEntity(uint64_t id)
{
  return static_cast<LibraryEntity>(id);
}

[[nodiscard]] GeometryEntity geometryEntity(uint64_t id)
{
  return static_cast<GeometryEntity>(id);
}

[[nodiscard]] std::size_t pageSize(std::size_t requested) noexcept
{
  return std::clamp(requested == 0u ? std::size_t{256} : requested, std::size_t{1}, DesignModelAdapter::kMaxPageSize);
}

[[nodiscard]] bool placed(DesignPlacementStatus status) noexcept
{
  return status == DesignPlacementStatus::kPlaced || status == DesignPlacementStatus::kFixed
         || status == DesignPlacementStatus::kCover;
}

[[nodiscard]] std::optional<int32_t> checkedCoordinate(int64_t value) noexcept
{
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<int32_t>(value);
}

[[nodiscard]] std::optional<Point> placeMasterPoint(Point point, const DesignInstance& instance,
                                                     const LibraryCellMaster& master) noexcept
{
  const int64_t x = point.x;
  const int64_t y = point.y;
  const int64_t width = master.width;
  const int64_t height = master.height;
  int64_t transformed_x = 0;
  int64_t transformed_y = 0;
  switch (instance.orientation) {
    case DesignOrientation::kN:
      transformed_x = x;
      transformed_y = y;
      break;
    case DesignOrientation::kW:
      transformed_x = -y + height;
      transformed_y = x;
      break;
    case DesignOrientation::kS:
      transformed_x = -x + width;
      transformed_y = -y + height;
      break;
    case DesignOrientation::kE:
      transformed_x = y;
      transformed_y = -x + width;
      break;
    case DesignOrientation::kFN:
      transformed_x = -x + width;
      transformed_y = y;
      break;
    case DesignOrientation::kFS:
      transformed_x = x;
      transformed_y = -y + height;
      break;
    case DesignOrientation::kFW:
      transformed_x = y;
      transformed_y = x;
      break;
    case DesignOrientation::kFE:
      transformed_x = -y + height;
      transformed_y = -x + width;
      break;
    default: return std::nullopt;
  }
  const auto final_x = checkedCoordinate(transformed_x + instance.origin.x);
  const auto final_y = checkedCoordinate(transformed_y + instance.origin.y);
  if (!final_x || !final_y) return std::nullopt;
  return Point{.x = *final_x, .y = *final_y};
}

[[nodiscard]] std::optional<Rect> masterEnvelope(const LibraryCellMaster& master) noexcept
{
  const auto width = checkedCoordinate(master.width);
  const auto height = checkedCoordinate(master.height);
  if (!width || !height) return std::nullopt;
  return Rect{.ll_x = 0, .ll_y = 0, .ur_x = *width, .ur_y = *height};
}

[[nodiscard]] std::optional<Rect> instanceEnvelope(const ModelContext& context, const DesignInstance& instance)
{
  if (!placed(instance.placement_status)) return std::nullopt;
  const auto* master = context.library_registry.try_get<const LibraryCellMaster>(instance.master.entity());
  if (master == nullptr) return std::nullopt;
  const auto master_bounds = masterEnvelope(*master);
  if (!master_bounds) return std::nullopt;

  const std::array<Point, 4> local{{{master_bounds->ll_x, master_bounds->ll_y},
                                    {master_bounds->ur_x, master_bounds->ll_y},
                                    {master_bounds->ll_x, master_bounds->ur_y},
                                    {master_bounds->ur_x, master_bounds->ur_y}}};
  std::optional<Rect> result;
  for (const auto point : local) {
    const auto transformed = placeMasterPoint(point, instance, *master);
    if (!transformed) return std::nullopt;
    const Rect singleton{.ll_x = transformed->x, .ll_y = transformed->y, .ur_x = transformed->x, .ur_y = transformed->y};
    result = result ? result->united(singleton) : singleton;
  }
  return result;
}

template <typename IdRange>
[[nodiscard]] std::optional<Rect> boundsForShapes(const ModelContext& context, const IdRange& ids)
{
  std::optional<Rect> result;
  const auto& geometry = context.design.geometryStorage();
  for (const auto id : ids) {
    if (!geometry.contains(id)) continue;
    const auto bounds = geometry.boundsUncached(id);
    result = result ? result->united(bounds) : bounds;
  }
  return result;
}

[[nodiscard]] std::optional<ModelObjectRef> netRef(DesignNetId id)
{
  if (!id) return std::nullopt;
  return ref(kNet, packed(id));
}

[[nodiscard]] std::optional<ModelObjectRef> ownerRef(const GeometryOwner& owner)
{
  switch (owner.domain) {
    case GeometryOwnerDomain::kDesignWire: return ref(kWire, owner.value);
    case GeometryOwnerDomain::kDesignNet: return ref(kNet, owner.value);
    case GeometryOwnerDomain::kDesignBlockPin: return ref(kBlockPin, owner.value);
    default: return std::nullopt;
  }
}

[[nodiscard]] std::optional<ModelObjectRef> shapeNetRef(const ModelContext& context, const GeometryOwner& owner)
{
  switch (owner.domain) {
    case GeometryOwnerDomain::kDesignWire: {
      const auto& wire = context.design.routingStorage().wire(DesignWireId{designEntity(owner.value)});
      return netRef(wire.net);
    }
    case GeometryOwnerDomain::kDesignNet: return ref(kNet, owner.value);
    case GeometryOwnerDomain::kDesignBlockPin: {
      const auto& pin = context.design.netlistStorage().blockPin(DesignBlockPinId{designEntity(owner.value)});
      const auto& term = context.design.netlistStorage().blockTerm(pin.term);
      return netRef(term.net ? term.net : term.special_net);
    }
    default: return std::nullopt;
  }
}

[[nodiscard]] ModelEntity modelInstance(const ModelContext& context, DesignInstanceId id, bool with_relations)
{
  const auto& instance = context.design.netlistStorage().instance(id);
  ModelEntity result{
      .object = ref(kInstance, packed(id)),
      .name = instance.name,
      .master = ref(kMaster, packed(instance.master)),
      .bounds = instanceEnvelope(context, instance),
      .status = static_cast<uint8_t>(instance.placement_status),
      .orientation = static_cast<uint8_t>(instance.orientation),
      .has_origin = true,
      .origin = instance.origin,
      .flags = instance.flags,
  };
  if (with_relations) {
    result.relations.push_back(*result.master);
    for (const auto term : context.design.netlistStorage().instanceTerms(id)) {
      result.relations.push_back(ref(kInstanceTerm, packed(term)));
    }
  }
  return result;
}

[[nodiscard]] std::optional<ModelEntity> modelLayer(const ModelContext& context, TechLayerId id)
{
  const auto entity = id.entity();
  const auto* info = context.tech_registry.try_get<const TechLayerInfo>(entity);
  if (info == nullptr) return std::nullopt;

  ModelEntity result{
      .object = ref(kLayer, packed(id)),
      .name = info->name,
      .layer = packed(id),
      .flags = info->flags,
  };
  if (const auto* routing = context.tech_registry.try_get<const TechRoutingLayer>(entity); routing != nullptr) {
    result.geometry_kind = static_cast<uint8_t>(GeometryLayerKind::kRouting);
    result.status = static_cast<uint8_t>(routing->direction);
  } else if (context.tech_registry.all_of<const TechCutLayer>(entity)) {
    result.geometry_kind = static_cast<uint8_t>(GeometryLayerKind::kCut);
  } else {
    result.geometry_kind = static_cast<uint8_t>(GeometryLayerKind::kOther);
  }
  return result;
}

[[nodiscard]] ModelEntity modelMaster(const ModelContext& context, LibraryCellMasterId id, bool with_relations)
{
  const auto& master = context.library_registry.get<const LibraryCellMaster>(id.entity());
  ModelEntity result{
      .object = ref(kMaster, packed(id)),
      .name = master.name,
      .bounds = masterEnvelope(master),
      .status = static_cast<uint8_t>(master.type),
  };
  const auto origin_x = checkedCoordinate(master.origin_x);
  const auto origin_y = checkedCoordinate(master.origin_y);
  if (origin_x && origin_y) {
    result.has_origin = true;
    result.origin = {.x = *origin_x, .y = *origin_y};
  }
  if (with_relations) {
    result.relations.reserve(master.terms.size());
    for (const auto term : master.terms) result.relations.push_back(ref(kMasterTerm, packed(term)));
  }
  return result;
}

[[nodiscard]] ModelEntity modelMasterTerm(const ModelContext& context, LibraryMasterTermId id, bool with_relations)
{
  const auto& term = context.library_registry.get<const LibraryMasterTerm>(id.entity());
  ModelEntity result{
      .object = ref(kMasterTerm, packed(id)),
      .name = term.name,
      .parent = ref(kMaster, packed(term.master)),
      .master = ref(kMaster, packed(term.master)),
      .status = static_cast<uint8_t>(term.direction),
      .flags = static_cast<uint32_t>(term.use),
  };
  if (with_relations) {
    result.relations.reserve(term.ports.size() + 1u);
    result.relations.push_back(*result.parent);
    for (const auto pin : term.ports) result.relations.push_back(ref(kMasterPin, packed(pin)));
  }
  return result;
}

[[nodiscard]] ModelEntity modelMasterPin(const ModelContext& context, LibraryMasterPinId id, bool with_relations)
{
  const auto& pin = context.library_registry.get<const LibraryMasterPin>(id.entity());
  ModelEntity result{
      .object = ref(kMasterPin, packed(id)),
      .parent = ref(kMasterTerm, packed(pin.term)),
      .status = static_cast<uint8_t>(pin.port_class),
  };
  if (context.library != nullptr) {
    result.bounds = context.library->masterPortStorage().boundingBox(id);
  }
  if (with_relations) result.relations.push_back(*result.parent);
  return result;
}

[[nodiscard]] ModelEntity modelInstanceTerm(const ModelContext& context, DesignInstanceTermId id, bool with_relations)
{
  const auto& term = context.design.netlistStorage().instanceTerm(id);
  const auto& master_term = context.library_registry.get<const LibraryMasterTerm>(term.master_term.entity());
  ModelEntity result{
      .object = ref(kInstanceTerm, packed(id)),
      .name = master_term.name,
      .parent = ref(kInstance, packed(term.instance)),
      .master = ref(kMasterTerm, packed(term.master_term)),
      .net = netRef(term.net ? term.net : term.special_net),
      .status = static_cast<uint8_t>(master_term.direction),
      .flags = static_cast<uint32_t>(master_term.use),
  };
  if (with_relations) {
    result.relations.push_back(*result.parent);
    result.relations.push_back(*result.master);
    if (result.net) result.relations.push_back(*result.net);
  }
  return result;
}

[[nodiscard]] ModelEntity modelBlockTerm(const ModelContext& context, DesignBlockTermId id, bool with_relations)
{
  const auto& term = context.design.netlistStorage().blockTerm(id);
  ModelEntity result{
      .object = ref(kBlockTerm, packed(id)),
      .name = term.name,
      .net = netRef(term.net ? term.net : term.special_net),
      .status = static_cast<uint8_t>(term.direction),
      .flags = term.flags,
  };
  if (with_relations) {
    result.relations.reserve(term.pins.size() + (result.net ? 1u : 0u));
    if (result.net) result.relations.push_back(*result.net);
    for (const auto pin : term.pins) result.relations.push_back(ref(kBlockPin, packed(pin)));
  }
  return result;
}

[[nodiscard]] ModelEntity modelBlockPin(const ModelContext& context, DesignBlockPinId id, bool with_relations)
{
  const auto& pin = context.design.netlistStorage().blockPin(id);
  const std::array ranges{std::span<const GeometryShapeId>{pin.rectangles}, std::span<const GeometryShapeId>{pin.polygons},
                          std::span<const GeometryShapeId>{pin.vias}};
  std::optional<Rect> bounds;
  for (const auto range : ranges) {
    const auto current = boundsForShapes(context, range);
    if (current) {
      if (bounds) {
        *bounds = bounds->united(*current);
      } else {
        bounds = current;
      }
    }
  }
  ModelEntity result{
      .object = ref(kBlockPin, packed(id)),
      .parent = ref(kBlockTerm, packed(pin.term)),
      .bounds = bounds,
      .status = static_cast<uint8_t>(pin.placement_status),
      .orientation = static_cast<uint8_t>(pin.orientation),
      .has_origin = true,
      .origin = pin.origin,
      .flags = pin.flags,
  };
  if (with_relations) {
    result.relations.push_back(*result.parent);
    for (const auto range : ranges) {
      for (const auto shape : range) result.relations.push_back(ref(kShape, packed(shape)));
    }
  }
  return result;
}

[[nodiscard]] ModelEntity modelNet(const ModelContext& context, DesignNetId id, bool with_relations)
{
  const auto& net = context.design.netlistStorage().net(id);
  ModelEntity result{
      .object = ref(kNet, packed(id)),
      .name = net.name,
      .status = context.design.netlistStorage().isSpecialNet(id) ? uint8_t{1} : uint8_t{0},
      .flags = net.flags,
  };
  if (with_relations) {
    for (const auto term : context.design.netlistStorage().instanceTerms(id)) {
      result.relations.push_back(ref(kInstanceTerm, packed(term)));
    }
    for (const auto term : context.design.netlistStorage().blockTerms(id)) {
      result.relations.push_back(ref(kBlockTerm, packed(term)));
    }
    for (const auto wire : context.design.routingStorage().wireIds(id)) {
      result.relations.push_back(ref(kWire, packed(wire)));
    }
    for (const auto shape : context.design.geometryStorage().netShapeIds(id)) {
      result.relations.push_back(ref(kShape, packed(shape)));
    }
  }
  return result;
}

[[nodiscard]] ModelEntity modelWire(const ModelContext& context, DesignWireId id, bool with_relations)
{
  const auto& wire = context.design.routingStorage().wire(id);
  ModelEntity result{
      .object = ref(kWire, packed(id)),
      .parent = ref(kNet, packed(wire.net)),
      .net = ref(kNet, packed(wire.net)),
      .bounds = boundsForShapes(context, wire.shapes),
      .status = static_cast<uint8_t>(wire.status),
  };
  if (with_relations) {
    result.relations.reserve(wire.shapes.size() + 1u);
    result.relations.push_back(*result.net);
    for (const auto shape : wire.shapes) result.relations.push_back(ref(kShape, packed(shape)));
  }
  return result;
}

[[nodiscard]] ModelEntity modelShape(const ModelContext& context, GeometryShapeId id, bool with_relations)
{
  const auto& geometry = context.design.geometryStorage();
  const auto view = geometry.shape(id);
  const auto& metadata = *view.metadata;
  ModelEntity result{
      .object = ref(kShape, packed(id)),
      .parent = ownerRef(metadata.owner),
      .net = shapeNetRef(context, metadata.owner),
      .bounds = geometry.boundsUncached(id),
      .geometry_kind = static_cast<uint8_t>(metadata.kind),
      .flags = metadata.flags,
  };
  const auto layer = geometry.effectiveLayer(id);
  if (layer) result.layer = packed(layer);
  if (with_relations) {
    if (result.parent) result.relations.push_back(*result.parent);
    if (result.net && (!result.parent || *result.net != *result.parent)) result.relations.push_back(*result.net);
  }
  return result;
}

[[nodiscard]] std::optional<ModelEntity> describe(const ModelContext& context, std::string_view kind, uint64_t id, bool with_relations)
{
  const auto& netlist = context.design.netlistStorage();
  const auto& routing = context.design.routingStorage();
  const auto& geometry = context.design.geometryStorage();
  if (kind == kLayer) return modelLayer(context, TechLayerId{static_cast<TechEntity>(id)});
  if (kind == kInstance) {
    const auto typed = DesignInstanceId{designEntity(id)};
    return netlist.contains(typed) ? std::optional<ModelEntity>{modelInstance(context, typed, with_relations)} : std::nullopt;
  }
  if (kind == kMaster) {
    const auto entity = libraryEntity(id);
    return context.library_registry.valid(entity) && context.library_registry.all_of<LibraryCellMaster>(entity)
               ? std::optional<ModelEntity>{modelMaster(context, LibraryCellMasterId{entity}, with_relations)}
               : std::nullopt;
  }
  if (kind == kMasterTerm) {
    const auto entity = libraryEntity(id);
    return context.library_registry.valid(entity) && context.library_registry.all_of<LibraryMasterTerm>(entity)
               ? std::optional<ModelEntity>{modelMasterTerm(context, LibraryMasterTermId{entity}, with_relations)}
               : std::nullopt;
  }
  if (kind == kMasterPin) {
    const auto entity = libraryEntity(id);
    return context.library_registry.valid(entity) && context.library_registry.all_of<LibraryMasterPin>(entity)
               ? std::optional<ModelEntity>{modelMasterPin(context, LibraryMasterPinId{entity}, with_relations)}
               : std::nullopt;
  }
  if (kind == kInstanceTerm) {
    const auto typed = DesignInstanceTermId{designEntity(id)};
    return netlist.contains(typed) ? std::optional<ModelEntity>{modelInstanceTerm(context, typed, with_relations)} : std::nullopt;
  }
  if (kind == kBlockTerm) {
    const auto typed = DesignBlockTermId{designEntity(id)};
    return netlist.contains(typed) ? std::optional<ModelEntity>{modelBlockTerm(context, typed, with_relations)} : std::nullopt;
  }
  if (kind == kBlockPin) {
    const auto typed = DesignBlockPinId{designEntity(id)};
    return netlist.contains(typed) ? std::optional<ModelEntity>{modelBlockPin(context, typed, with_relations)} : std::nullopt;
  }
  if (kind == kNet) {
    const auto typed = DesignNetId{designEntity(id)};
    return netlist.contains(typed) ? std::optional<ModelEntity>{modelNet(context, typed, with_relations)} : std::nullopt;
  }
  if (kind == kWire) {
    const auto typed = DesignWireId{designEntity(id)};
    return routing.contains(typed) ? std::optional<ModelEntity>{modelWire(context, typed, with_relations)} : std::nullopt;
  }
  if (kind == kShape) {
    const auto typed = GeometryShapeId{geometryEntity(id)};
    return geometry.contains(typed) ? std::optional<ModelEntity>{modelShape(context, typed, with_relations)} : std::nullopt;
  }
  return std::nullopt;
}

template <typename Range, typename Id, typename Build>
[[nodiscard]] ModelEntityPage pageFrom(const Range& values, uint64_t offset, std::size_t limit, Build&& build)
{
  ModelEntityPage result;
  const auto maximum = pageSize(limit);
  uint64_t ordinal = 0;
  for (const auto value : values) {
    if (ordinal++ < offset) continue;
    if (result.records.size() == maximum) {
      result.has_more = true;
      break;
    }
    result.records.push_back(std::invoke(build, Id{value}));
  }
  result.next_offset = offset + result.records.size();
  return result;
}

template <typename Range, typename Id>
[[nodiscard]] std::vector<ModelObjectRef> idPageFrom(const Range& values, std::string_view kind, uint64_t offset,
                                                      std::size_t limit)
{
  std::vector<ModelObjectRef> result;
  const auto maximum = pageSize(limit);
  result.reserve(maximum);
  uint64_t ordinal = 0;
  for (const auto value : values) {
    if (ordinal++ < offset) continue;
    if (result.size() == maximum) break;
    result.push_back(ref(kind, packed(Id{value})));
  }
  return result;
}

[[nodiscard]] bool validParent(const std::optional<ModelObjectRef>& parent, std::string_view expected_kind) noexcept
{
  return parent && parent->kind == expected_kind && parent->valid();
}

[[nodiscard]] ModelContext contextFor(const DatabaseContext& state)
{
  const auto& design = state.design();
  return ModelContext{.design = design,
                      .library = state.library(),
                      .design_registry = design.designRegistry().registry(),
                      .library_registry = design.libraryRegistry().registry(),
                      .tech_registry = design.techRegistry().registry()};
}

}  // namespace

ModelEntityPage DesignModelAdapter::list(std::string_view kind, uint64_t offset, std::size_t limit,
                                         std::optional<ModelObjectRef> parent) const
{
  if (_context == nullptr) return {};
  const auto context = std::optional<ModelContext>{contextFor(*_context)};
  const auto& design = context->design;
  const auto& netlist = design.netlistStorage();
  const auto& routing = design.routingStorage();

  if (kind == kLayer) {
    const auto roots = context->tech_registry.view<const TechLayerSequence>();
    if (roots.begin() == roots.end()) return {};
    const auto& sequence = roots.get<const TechLayerSequence>(*roots.begin()).layers;
    ModelEntityPage result;
    const auto maximum = pageSize(limit);
    uint64_t ordinal = 0;
    for (const auto id : sequence) {
      if (ordinal++ < offset) continue;
      if (result.records.size() == maximum) {
        result.has_more = true;
        break;
      }
      const auto descriptor = modelLayer(*context, id);
      if (descriptor) result.records.push_back(*descriptor);
    }
    result.next_offset = offset + result.records.size();
    return result;
  }
  if (kind == kInstance) {
    const auto view = context->design_registry.view<const DesignInstance>();
    return pageFrom<decltype(view), DesignInstanceId>(view, offset, limit,
                                                       [&context](DesignInstanceId id) { return modelInstance(*context, id, false); });
  }
  if (kind == kMaster) {
    const auto view = context->library_registry.view<const LibraryCellMaster>();
    return pageFrom<decltype(view), LibraryCellMasterId>(view, offset, limit,
                                                          [&context](LibraryCellMasterId id) { return modelMaster(*context, id, false); });
  }
  if (kind == kMasterTerm) {
    if (validParent(parent, kMaster)) {
      const auto master = LibraryCellMasterId{libraryEntity(parent->id)};
      if (!context->library_registry.valid(master.entity()) || !context->library_registry.all_of<LibraryCellMaster>(master.entity())) return {};
      return pageFrom<decltype(context->library_registry.get<const LibraryCellMaster>(master.entity()).terms), LibraryMasterTermId>(
          context->library_registry.get<const LibraryCellMaster>(master.entity()).terms, offset, limit,
          [&context](LibraryMasterTermId id) { return modelMasterTerm(*context, id, false); });
    }
    const auto view = context->library_registry.view<const LibraryMasterTerm>();
    return pageFrom<decltype(view), LibraryMasterTermId>(view, offset, limit,
                                                          [&context](LibraryMasterTermId id) { return modelMasterTerm(*context, id, false); });
  }
  if (kind == kMasterPin) {
    if (validParent(parent, kMasterTerm)) {
      const auto term = LibraryMasterTermId{libraryEntity(parent->id)};
      if (!context->library_registry.valid(term.entity()) || !context->library_registry.all_of<LibraryMasterTerm>(term.entity())) return {};
      return pageFrom<decltype(context->library_registry.get<const LibraryMasterTerm>(term.entity()).ports), LibraryMasterPinId>(
          context->library_registry.get<const LibraryMasterTerm>(term.entity()).ports, offset, limit,
          [&context](LibraryMasterPinId id) { return modelMasterPin(*context, id, false); });
    }
    const auto view = context->library_registry.view<const LibraryMasterPin>();
    return pageFrom<decltype(view), LibraryMasterPinId>(view, offset, limit,
                                                         [&context](LibraryMasterPinId id) { return modelMasterPin(*context, id, false); });
  }
  if (kind == kInstanceTerm) {
    if (validParent(parent, kInstance)) {
      const auto instance = DesignInstanceId{designEntity(parent->id)};
      if (!netlist.contains(instance)) return {};
      return pageFrom<decltype(netlist.instanceTerms(instance)), DesignInstanceTermId>(
          netlist.instanceTerms(instance), offset, limit,
          [&context](DesignInstanceTermId id) { return modelInstanceTerm(*context, id, false); });
    }
    if (validParent(parent, kNet)) {
      const auto net = DesignNetId{designEntity(parent->id)};
      if (!netlist.contains(net)) return {};
      return pageFrom<decltype(netlist.instanceTerms(net)), DesignInstanceTermId>(
          netlist.instanceTerms(net), offset, limit,
          [&context](DesignInstanceTermId id) { return modelInstanceTerm(*context, id, false); });
    }
    const auto view = context->design_registry.view<const DesignInstanceTerm>();
    return pageFrom<decltype(view), DesignInstanceTermId>(view, offset, limit,
                                                         [&context](DesignInstanceTermId id) { return modelInstanceTerm(*context, id, false); });
  }
  if (kind == kBlockTerm) {
    if (validParent(parent, kNet)) {
      const auto net = DesignNetId{designEntity(parent->id)};
      if (!netlist.contains(net)) return {};
      return pageFrom<decltype(netlist.blockTerms(net)), DesignBlockTermId>(
          netlist.blockTerms(net), offset, limit,
          [&context](DesignBlockTermId id) { return modelBlockTerm(*context, id, false); });
    }
    const auto view = context->design_registry.view<const DesignBlockTerm>();
    return pageFrom<decltype(view), DesignBlockTermId>(view, offset, limit,
                                                        [&context](DesignBlockTermId id) { return modelBlockTerm(*context, id, false); });
  }
  if (kind == kBlockPin) {
    if (validParent(parent, kBlockTerm)) {
      const auto term = DesignBlockTermId{designEntity(parent->id)};
      if (!netlist.contains(term)) return {};
      return pageFrom<decltype(netlist.blockPins(term)), DesignBlockPinId>(
          netlist.blockPins(term), offset, limit,
          [&context](DesignBlockPinId id) { return modelBlockPin(*context, id, false); });
    }
    const auto view = context->design_registry.view<const DesignBlockPin>();
    return pageFrom<decltype(view), DesignBlockPinId>(view, offset, limit,
                                                       [&context](DesignBlockPinId id) { return modelBlockPin(*context, id, false); });
  }
  if (kind == kNet) {
    const auto view = context->design_registry.view<const DesignNet>();
    return pageFrom<decltype(view), DesignNetId>(view, offset, limit,
                                                  [&context](DesignNetId id) { return modelNet(*context, id, false); });
  }
  if (kind == kWire) {
    if (validParent(parent, kNet)) {
      const auto net = DesignNetId{designEntity(parent->id)};
      if (!netlist.contains(net)) return {};
      return pageFrom<decltype(routing.wireIds(net)), DesignWireId>(
          routing.wireIds(net), offset, limit, [&context](DesignWireId id) { return modelWire(*context, id, false); });
    }
    const auto view = context->design_registry.view<const DesignWire>();
    return pageFrom<decltype(view), DesignWireId>(view, offset, limit,
                                                   [&context](DesignWireId id) { return modelWire(*context, id, false); });
  }
  if (kind == kShape) {
    const auto& geometry = design.geometryStorage();
    if (validParent(parent, kWire)) {
      const auto wire = DesignWireId{designEntity(parent->id)};
      if (!routing.contains(wire)) return {};
      return pageFrom<decltype(geometry.wireShapeIds(wire)), GeometryShapeId>(
          geometry.wireShapeIds(wire), offset, limit, [&context](GeometryShapeId id) { return modelShape(*context, id, false); });
    }
    if (validParent(parent, kNet)) {
      const auto net = DesignNetId{designEntity(parent->id)};
      if (!netlist.contains(net)) return {};
      return pageFrom<decltype(geometry.netShapeIds(net)), GeometryShapeId>(
          geometry.netShapeIds(net), offset, limit, [&context](GeometryShapeId id) { return modelShape(*context, id, false); });
    }
    if (validParent(parent, kBlockPin)) {
      const auto pin = DesignBlockPinId{designEntity(parent->id)};
      if (!netlist.contains(pin)) return {};
      std::vector<GeometryShapeId> ids;
      const auto& record = netlist.blockPin(pin);
      ids.reserve(record.rectangles.size() + record.polygons.size() + record.vias.size());
      ids.insert(ids.end(), record.rectangles.begin(), record.rectangles.end());
      ids.insert(ids.end(), record.polygons.begin(), record.polygons.end());
      ids.insert(ids.end(), record.vias.begin(), record.vias.end());
      return pageFrom<decltype(ids), GeometryShapeId>(ids, offset, limit,
                                                       [&context](GeometryShapeId id) { return modelShape(*context, id, false); });
    }
    const auto view = geometry.registry().registry().view<const GeometryShape>();
    return pageFrom<decltype(view), GeometryShapeId>(view, offset, limit,
                                                      [&context](GeometryShapeId id) { return modelShape(*context, id, false); });
  }
  return {};
}

std::vector<ModelObjectRef> DesignModelAdapter::listIds(std::string_view kind, uint64_t offset, std::size_t limit,
                                                         std::optional<ModelObjectRef> parent) const
{
  if (_context == nullptr) return {};
  const auto context = std::optional<ModelContext>{contextFor(*_context)};
  const auto& design = context->design;
  const auto& netlist = design.netlistStorage();
  const auto& routing = design.routingStorage();

  if (kind == kLayer) {
    const auto roots = context->tech_registry.view<const TechLayerSequence>();
    if (roots.begin() == roots.end()) return {};
    const auto& sequence = roots.get<const TechLayerSequence>(*roots.begin()).layers;
    return idPageFrom<decltype(sequence), TechLayerId>(sequence, kind, offset, limit);
  }
  if (kind == kInstance) {
    const auto view = context->design_registry.view<const DesignInstance>();
    return idPageFrom<decltype(view), DesignInstanceId>(view, kind, offset, limit);
  }
  if (kind == kMaster) {
    const auto view = context->library_registry.view<const LibraryCellMaster>();
    return idPageFrom<decltype(view), LibraryCellMasterId>(view, kind, offset, limit);
  }
  if (kind == kMasterTerm) {
    if (validParent(parent, kMaster)) {
      const auto master = LibraryCellMasterId{libraryEntity(parent->id)};
      if (!context->library_registry.valid(master.entity()) || !context->library_registry.all_of<LibraryCellMaster>(master.entity())) return {};
      const auto& terms = context->library_registry.get<const LibraryCellMaster>(master.entity()).terms;
      return idPageFrom<decltype(terms), LibraryMasterTermId>(terms, kind, offset, limit);
    }
    const auto view = context->library_registry.view<const LibraryMasterTerm>();
    return idPageFrom<decltype(view), LibraryMasterTermId>(view, kind, offset, limit);
  }
  if (kind == kMasterPin) {
    if (validParent(parent, kMasterTerm)) {
      const auto term = LibraryMasterTermId{libraryEntity(parent->id)};
      if (!context->library_registry.valid(term.entity()) || !context->library_registry.all_of<LibraryMasterTerm>(term.entity())) return {};
      const auto& ports = context->library_registry.get<const LibraryMasterTerm>(term.entity()).ports;
      return idPageFrom<decltype(ports), LibraryMasterPinId>(ports, kind, offset, limit);
    }
    const auto view = context->library_registry.view<const LibraryMasterPin>();
    return idPageFrom<decltype(view), LibraryMasterPinId>(view, kind, offset, limit);
  }
  if (kind == kInstanceTerm) {
    if (validParent(parent, kInstance)) {
      const auto instance = DesignInstanceId{designEntity(parent->id)};
      if (!netlist.contains(instance)) return {};
      const auto terms = netlist.instanceTerms(instance);
      return idPageFrom<decltype(terms), DesignInstanceTermId>(terms, kind, offset, limit);
    }
    if (validParent(parent, kNet)) {
      const auto net = DesignNetId{designEntity(parent->id)};
      if (!netlist.contains(net)) return {};
      const auto terms = netlist.instanceTerms(net);
      return idPageFrom<decltype(terms), DesignInstanceTermId>(terms, kind, offset, limit);
    }
    const auto view = context->design_registry.view<const DesignInstanceTerm>();
    return idPageFrom<decltype(view), DesignInstanceTermId>(view, kind, offset, limit);
  }
  if (kind == kBlockTerm) {
    if (validParent(parent, kNet)) {
      const auto net = DesignNetId{designEntity(parent->id)};
      if (!netlist.contains(net)) return {};
      const auto terms = netlist.blockTerms(net);
      return idPageFrom<decltype(terms), DesignBlockTermId>(terms, kind, offset, limit);
    }
    const auto view = context->design_registry.view<const DesignBlockTerm>();
    return idPageFrom<decltype(view), DesignBlockTermId>(view, kind, offset, limit);
  }
  if (kind == kBlockPin) {
    if (validParent(parent, kBlockTerm)) {
      const auto term = DesignBlockTermId{designEntity(parent->id)};
      if (!netlist.contains(term)) return {};
      const auto pins = netlist.blockPins(term);
      return idPageFrom<decltype(pins), DesignBlockPinId>(pins, kind, offset, limit);
    }
    const auto view = context->design_registry.view<const DesignBlockPin>();
    return idPageFrom<decltype(view), DesignBlockPinId>(view, kind, offset, limit);
  }
  if (kind == kNet) {
    const auto view = context->design_registry.view<const DesignNet>();
    return idPageFrom<decltype(view), DesignNetId>(view, kind, offset, limit);
  }
  if (kind == kWire) {
    if (validParent(parent, kNet)) {
      const auto net = DesignNetId{designEntity(parent->id)};
      if (!netlist.contains(net)) return {};
      const auto wires = routing.wireIds(net);
      return idPageFrom<decltype(wires), DesignWireId>(wires, kind, offset, limit);
    }
    const auto view = context->design_registry.view<const DesignWire>();
    return idPageFrom<decltype(view), DesignWireId>(view, kind, offset, limit);
  }
  if (kind == kShape) {
    const auto& geometry = design.geometryStorage();
    if (validParent(parent, kWire)) {
      const auto wire = DesignWireId{designEntity(parent->id)};
      if (!routing.contains(wire)) return {};
      const auto ids = geometry.wireShapeIds(wire);
      return idPageFrom<decltype(ids), GeometryShapeId>(ids, kind, offset, limit);
    }
    if (validParent(parent, kNet)) {
      const auto net = DesignNetId{designEntity(parent->id)};
      if (!netlist.contains(net)) return {};
      const auto ids = geometry.netShapeIds(net);
      return idPageFrom<decltype(ids), GeometryShapeId>(ids, kind, offset, limit);
    }
    if (validParent(parent, kBlockPin)) {
      const auto pin = DesignBlockPinId{designEntity(parent->id)};
      if (!netlist.contains(pin)) return {};
      const auto& record = netlist.blockPin(pin);
      std::vector<GeometryShapeId> ids;
      ids.reserve(record.rectangles.size() + record.polygons.size() + record.vias.size());
      ids.insert(ids.end(), record.rectangles.begin(), record.rectangles.end());
      ids.insert(ids.end(), record.polygons.begin(), record.polygons.end());
      ids.insert(ids.end(), record.vias.begin(), record.vias.end());
      return idPageFrom<decltype(ids), GeometryShapeId>(ids, kind, offset, limit);
    }
    const auto view = geometry.registry().registry().view<const GeometryShape>();
    return idPageFrom<decltype(view), GeometryShapeId>(view, kind, offset, limit);
  }
  return {};
}

std::optional<ModelEntity> DesignModelAdapter::describe(std::string_view kind, uint64_t id) const
{
  if (_context == nullptr) return std::nullopt;
  return ::eccdb::describe(contextFor(*_context), kind, id, true);
}

std::optional<ModelObjectRef> DesignModelAdapter::find(std::string_view kind, std::string_view name,
                                                        std::optional<ModelObjectRef> parent) const
{
  if (_context == nullptr) return std::nullopt;
  const auto context = std::optional<ModelContext>{contextFor(*_context)};
  const auto& netlist = context->design.netlistStorage();
  if (kind == kLayer) {
    const auto roots = context->tech_registry.view<const TechLayerSequence>();
    if (roots.begin() == roots.end()) return std::nullopt;
    const auto& sequence = roots.get<const TechLayerSequence>(*roots.begin()).layers;
    for (const auto id : sequence) {
      const auto* info = context->tech_registry.try_get<const TechLayerInfo>(id.entity());
      if (info != nullptr && info->name == name) return ref(kLayer, packed(id));
    }
    return std::nullopt;
  }
  if (kind == kInstance) {
    const auto id = netlist.findInstance(name);
    return id ? std::optional<ModelObjectRef>{ref(kInstance, packed(id))} : std::nullopt;
  }
  if (kind == kMaster) {
    if (context->library != nullptr) {
      const auto id = context->library->cellMasterStorage().findCellMaster(name);
      return id ? std::optional<ModelObjectRef>{ref(kMaster, packed(id))} : std::nullopt;
    }
    const auto view = context->library_registry.view<const LibraryCellMaster>();
    for (const auto entity : view) {
      if (view.get<const LibraryCellMaster>(entity).name == name) return ref(kMaster, packed(LibraryCellMasterId{entity}));
    }
    return std::nullopt;
  }
  if (kind == kMasterTerm && validParent(parent, kMaster)) {
    const auto master = LibraryCellMasterId{libraryEntity(parent->id)};
    if (!context->library_registry.valid(master.entity()) || !context->library_registry.all_of<LibraryCellMaster>(master.entity())) return std::nullopt;
    for (const auto id : context->library_registry.get<const LibraryCellMaster>(master.entity()).terms) {
      if (context->library_registry.get<const LibraryMasterTerm>(id.entity()).name == name) return ref(kMasterTerm, packed(id));
    }
    return std::nullopt;
  }
  if (kind == kInstanceTerm && validParent(parent, kInstance)) {
    const auto instance = DesignInstanceId{designEntity(parent->id)};
    if (!netlist.contains(instance)) return std::nullopt;
    const auto id = netlist.findInstanceTerm(instance, name);
    return id ? std::optional<ModelObjectRef>{ref(kInstanceTerm, packed(id))} : std::nullopt;
  }
  if (kind == kBlockTerm) {
    const auto id = netlist.findBlockTerm(name);
    return id ? std::optional<ModelObjectRef>{ref(kBlockTerm, packed(id))} : std::nullopt;
  }
  if (kind == kNet) {
    auto id = netlist.findRegularNet(name);
    if (!id) id = netlist.findSpecialNet(name);
    return id ? std::optional<ModelObjectRef>{ref(kNet, packed(id))} : std::nullopt;
  }
  return std::nullopt;
}

std::vector<ModelLayerRecord> DesignModelAdapter::layers() const
{
  if (_context == nullptr) return {};
  const auto context = std::optional<ModelContext>{contextFor(*_context)};
  const auto roots = context->tech_registry.view<const TechLayerSequence>();
  if (roots.begin() == roots.end()) return {};
  const auto& sequence = roots.get<const TechLayerSequence>(*roots.begin()).layers;
  std::vector<ModelLayerRecord> result;
  result.reserve(sequence.size());
  uint32_t routing_level = 0;
  for (uint32_t index = 0; index < sequence.size(); ++index) {
    const auto layer = sequence[index];
    const auto entity = layer.entity();
    const auto* info = context->tech_registry.try_get<const TechLayerInfo>(entity);
    if (info == nullptr) continue;
    if (const auto* routing = context->tech_registry.try_get<const TechRoutingLayer>(entity); routing != nullptr) {
      ++routing_level;
      result.push_back(ModelLayerRecord{.id = packed(layer),
                                        .name = info->name,
                                        .sequence_index = index,
                                        .routing_level = routing_level,
                                        .mask_count = info->mask_count,
                                        .kind = static_cast<uint8_t>(GeometryLayerKind::kRouting),
                                        .direction = static_cast<uint8_t>(routing->direction),
                                        .flags = routing->flags,
                                        .width = routing->width,
                                        .pitch_x = routing->pitch_x,
                                        .pitch_y = routing->pitch_y,
                                        .offset_x = routing->offset_x,
                                        .offset_y = routing->offset_y});
    } else if (const auto* cut = context->tech_registry.try_get<const TechCutLayer>(entity); cut != nullptr) {
      result.push_back(ModelLayerRecord{.id = packed(layer),
                                        .name = info->name,
                                        .sequence_index = index,
                                        .mask_count = info->mask_count,
                                        .kind = static_cast<uint8_t>(GeometryLayerKind::kCut),
                                        .flags = cut->flags,
                                        .width = cut->width});
    }
  }
  return result;
}

std::vector<ModelTrackGridRecord> DesignModelAdapter::trackGrids(std::optional<uint64_t> layer) const
{
  if (_context == nullptr) return {};
  const auto context = std::optional<ModelContext>{contextFor(*_context)};
  std::vector<ModelTrackGridRecord> result;
  for (const auto id : context->design.floorplanStorage().trackGrids()) {
    const auto& grid = context->design.floorplanStorage().trackGrid(id);
    for (const auto target_layer : grid.layers) {
      if (layer && packed(target_layer) != *layer) continue;
      result.push_back(ModelTrackGridRecord{.source_id = packed(id),
                                             .layer = packed(target_layer),
                                             .axis = static_cast<uint8_t>(grid.axis),
                                             .start = grid.start,
                                             .track_count = grid.track_count,
                                             .step = grid.step,
                                             .flags = grid.flags,
                                             .mask = grid.mask});
    }
  }
  return result;
}

ModelEntityPage DesignModelAdapter::listPlacedInstances(Rect viewport, uint64_t offset, std::size_t limit) const
{
  if (_context == nullptr) return {};
  const auto context = std::optional<ModelContext>{contextFor(*_context)};
  ModelEntityPage result;
  const auto maximum = pageSize(limit);
  const auto requested_offset = offset;

  // DesignInstance::region_bounds are canonical geometry entries owned by the
  // same registry as route/pin shapes.  Reuse that already-maintained R-tree
  // instead of scanning every placed instance for every viewport refresh.
  // The index is derived and rebuilt lazily, just like route queries; no
  // second component geometry store is introduced here.
  GeometryQueryFilter filter;
  filter.owner_domain = GeometryOwnerDomain::kDesignInstance;
  filter.kind_mask = uint32_t{1} << static_cast<uint32_t>(GeometryShapeKind::kRectangle);
  const auto& geometry = context->design.geometryStorage();
  // A legacy binary can contain instances without the derived region_bounds
  // relation. Detect that once per call before choosing the indexed path; an
  // empty viewport must still remain a cheap indexed query for modern DBs.
  bool has_instance_bounds = false;
  const auto instance_view = context->design_registry.view<const DesignInstance>();
  for (const auto entity : instance_view) {
    if (!instance_view.get<const DesignInstance>(entity).region_bounds.empty()) {
      has_instance_bounds = true;
      break;
    }
  }
  if (has_instance_bounds) {
    const auto instance_shapes = context->design.geometryIndex().query(viewport, filter);
    // The index returns one row per region-bound shape, while this API is
    // occurrence-based. Deduplicate before pagination so a multi-rectangle
    // instance cannot consume page slots or make the GUI show a different
    // component after a viewport change.
    std::vector<uint64_t> instance_ids;
    instance_ids.reserve(instance_shapes.size());
    std::unordered_set<uint64_t> seen_instances;
    seen_instances.reserve(instance_shapes.size());
    for (const auto shape_id : instance_shapes) {
      const auto owner = geometry.owner(shape_id);
      if (owner.domain != GeometryOwnerDomain::kDesignInstance) continue;
      const auto id = DesignInstanceId{designEntity(owner.value)};
      const auto& instance = context->design.netlistStorage().instance(id);
      if (!placed(instance.placement_status)) continue;
      const auto packed_id = packed(id);
      if (seen_instances.insert(packed_id).second) instance_ids.push_back(packed_id);
    }
    std::sort(instance_ids.begin(), instance_ids.end());
    for (const auto packed_id : instance_ids) {
      const auto id = DesignInstanceId{designEntity(packed_id)};
      if (offset != 0) {
        --offset;
        continue;
      }
      if (result.records.size() == maximum) {
        result.has_more = true;
        break;
      }
      result.records.push_back(modelInstance(*context, id, false));
    }
    result.next_offset = requested_offset + result.records.size();
    return result;
  }

  // Older/partial databases may not persist instance region bounds. Preserve
  // the previous behavior for that case instead of making components vanish.
  uint64_t ordinal = 0;
  const auto view = context->design_registry.view<const DesignInstance>();
  for (const auto entity : view) {
    const auto id = DesignInstanceId{entity};
    const auto& instance = view.get<const DesignInstance>(entity);
    const auto bounds = instanceEnvelope(*context, instance);
    if (!bounds || !bounds->intersects(viewport)) continue;
    if (ordinal++ < offset) continue;
    if (result.records.size() == maximum) {
      result.has_more = true;
      break;
    }
    result.records.push_back(modelInstance(*context, id, false));
  }
  result.next_offset = offset + result.records.size();
  return result;
}

}  // namespace eccdb
