#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/container/dense_map.hpp>

#include "design/DesignRegistry.h"
#include "design/non_default_rule/component/NonDefaultRuleComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/routing/component/WireRouting.h"
#include "design/via/component/ViaComponents.h"
#include "geometry/GeometryRegistry.h"

namespace eccdb {

class TechRegistry;

struct DesignRoutingArrayStatistics
{
  std::size_t count = 0;
  std::size_t capacity = 0;
  std::size_t element_size = 0;
  std::size_t capacity_bytes = 0;
  std::size_t allocation_usable_bytes = 0;
  std::size_t buffer_count = 0;
};

struct DesignRoutingStatistics
{
  // Canonical geometry and relation counts. Legacy fields below describe only
  // derived DEF projections and are retained for existing diagnostic clients.
  DesignRoutingArrayStatistics shapes;
  DesignRoutingArrayStatistics wire_shape_ids;
  DesignRoutingArrayStatistics paths;
  DesignRoutingArrayStatistics path_extras;
  DesignRoutingArrayStatistics points;
  DesignRoutingArrayStatistics vias;
  DesignRoutingArrayStatistics rectangles;
  std::size_t dynamic_string_capacity_bytes = 0;
};

class DesignRoutingStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;
  using geometry_registry_type = GeometryRegistry::registry_type;
  // Called synchronously while canonical VIA geometry is borrowed. The
  // callback must not mutate the design or technology geometry registries.
  using ViaLayerBoundsVisitor = std::function<void(TechLayerId, Rect)>;

  DesignRoutingStorage(DesignRegistry& design_registry, const TechRegistry& tech_registry,
                       const GeometryRegistry* tech_geometry_registry = nullptr)
      : _design_registry(design_registry),
        _registry(design_registry.registry()),
        _tech_registry(tech_registry),
        _geometry_registry(design_registry.geometryRegistry()),
        _tech_geometry_registry(tech_geometry_registry)
  {
  }

  [[nodiscard]] DesignViaId createVia(DesignViaInput via);
  void updateVia(DesignViaId id, DesignViaInput via);
  [[nodiscard]] bool destroyVia(DesignViaId id);
  [[nodiscard]] bool contains(DesignViaId id) const;
  [[nodiscard]] DesignViaId findVia(std::string_view name) const;
  [[nodiscard]] const DesignVia& via(DesignViaId id) const;
  [[nodiscard]] std::vector<DesignViaId> vias() const;

  [[nodiscard]] DesignNonDefaultRuleId createNonDefaultRule(DesignNonDefaultRule rule);
  void updateNonDefaultRule(DesignNonDefaultRuleId id, DesignNonDefaultRule rule);
  [[nodiscard]] bool destroyNonDefaultRule(DesignNonDefaultRuleId id);
  [[nodiscard]] bool contains(DesignNonDefaultRuleId id) const;
  [[nodiscard]] DesignNonDefaultRuleId findNonDefaultRule(std::string_view name) const;
  [[nodiscard]] const DesignNonDefaultRule& nonDefaultRule(DesignNonDefaultRuleId id) const;
  [[nodiscard]] std::vector<DesignNonDefaultRuleId> nonDefaultRules() const;

  // Shape-first API. A Wire may be empty while being edited; it owns only IDs.
  [[nodiscard]] DesignWireId createWire(DesignWire wire);
  [[nodiscard]] GeometryShapeId createShape(DesignWireId wire, DesignRouteShape shape,
                                            std::optional<DesignRouteShapeAttributes> attributes = std::nullopt);
  // Omitted attributes retain the current values; clearing value flags removes
  // them. A geometry-only edit does not discard DEF width/mask metadata.
  void updateShape(GeometryShapeId id, DesignRouteShape shape,
                   std::optional<DesignRouteShapeAttributes> attributes = std::nullopt);
  [[nodiscard]] bool destroyShape(GeometryShapeId id);
  [[nodiscard]] bool contains(GeometryShapeId id) const;
  // Live ID contract, like instanceTerm(): no repeated hot-path validation.
  [[nodiscard]] const DesignRouteShape& shape(GeometryShapeId id) const
  {
    return _geometry_registry.registry().get<const DesignRouteShape>(id.entity());
  }
  [[nodiscard]] const DesignRouteShapeAttributes* shapeAttributes(GeometryShapeId id) const
  {
    return _geometry_registry.registry().try_get<const DesignRouteShapeAttributes>(id.entity());
  }
  [[nodiscard]] Rect shapeBoundingBox(GeometryShapeId id) const;
  [[nodiscard]] Rect viaPlacementBounds(TechViaMasterId tech_via, DesignViaId design_via,
                                        Point origin, DesignOrientation orientation) const;
  // Expand one placed VIA shape into physical per-layer primitive bounds.
  // Returns false for non-VIA geometry or when a technology VIA's separate
  // canonical geometry registry was not bound by the owning DesignStore.
  [[nodiscard]] bool forEachViaLayerBounds(GeometryShapeId id, const ViaLayerBoundsVisitor& visitor) const;
  // Technology geometry is owned by TechStore, not copied into DesignStore.
  // Rebinding invalidates derived Design geometry views once through the
  // common epoch; the caller retains lifetime ownership of the registry.
  void setTechnologyGeometryRegistry(const GeometryRegistry* tech_geometry_registry) noexcept;
  // Derived BBox indexes use this revision to detect geometry edits. Shape IDs
  // are entity handles and remain independent of any traversal order.
  [[nodiscard]] uint64_t geometryEpoch() const noexcept { return _design_registry.geometryEpoch(); }
  void markGeometryChanged() noexcept { _design_registry.markGeometryChanged(); }
  template <typename Function>
  void forEachShape(DesignWireId id, Function&& function) const
  {
    for (const auto shape_id : _registry.get<const DesignWire>(id.entity()).shapes) {
      std::invoke(function, shape_id, shape(shape_id), shapeAttributes(shape_id));
    }
  }

  [[nodiscard]] bool destroyWire(DesignWireId id);
  [[nodiscard]] bool contains(DesignWireId id) const;
  [[nodiscard]] DesignWire& wire(DesignWireId id);
  [[nodiscard]] const DesignWire& wire(DesignWireId id) const;
  [[nodiscard]] std::vector<DesignWireId> wires() const;
  [[nodiscard]] std::vector<DesignWireId> wires(DesignNetId net) const;
  // The returned view is invalidated when this Net's Wire storage changes.
  [[nodiscard]] std::span<const DesignWireId> wireIds(DesignNetId net) const;

  // Callbacks must not mutate wire storage while it is being visited. This
  // preserves the net-local order without copying the indexed wire IDs.
  template <typename Function>
    requires std::invocable<Function&, DesignWireId, const DesignWire&>
  void forEachWire(DesignNetId net, Function&& function) const
  {
    for (const auto wire_id : wireIds(net)) {
      std::invoke(function, wire_id, _registry.get<const DesignWire>(wire_id.entity()));
    }
  }

  void rebuildWireIndex();
  void shrinkRoutingToFit();
  [[nodiscard]] DesignRoutingStatistics routingStatistics() const;
  void validateRestoredRoutingState() const;

  void setNetGeometry(DesignNetId net, DesignNetGeometry geometry);
  // Creates the empty relation used by the editable geometry facade.  The
  // relation is intentionally separate from the geometry entities themselves.
  DesignNetGeometry& ensureNetGeometry(DesignNetId net);
  // Mutable relation access for editor operations that remove or reorder a
  // standalone geometry entity. Unlike ensureNetGeometry this never creates
  // a relation as a side effect.
  [[nodiscard]] DesignNetGeometry* netGeometry(DesignNetId net);
  [[nodiscard]] const DesignNetGeometry* netGeometry(DesignNetId net) const;
  // Remove an empty standalone-geometry relation after the last shape is
  // deleted. Binary validation treats an empty relation as invalid, so the
  // editor must drop the relation component instead of retaining a tombstone.
  bool pruneEmptyNetGeometry(DesignNetId net);
  // Shared by single-Shape edits and binary restore; never run on read paths.
  void validateNetShape(const DesignNetRectangle& rectangle) const;
  void validateNetShape(const DesignNetPolygon& polygon) const;
  void validateNetShape(const DesignNetVia& via) const;

  [[nodiscard]] uint32_t viaCount() const;
  [[nodiscard]] uint32_t nonDefaultRuleCount() const;
  [[nodiscard]] uint32_t wireCount() const;

  [[nodiscard]] DesignRegistry& designRegistry() noexcept { return _design_registry; }
  [[nodiscard]] const DesignRegistry& designRegistry() const noexcept { return _design_registry; }

  // Geometry entities have a separate EnTT domain.  The registry is exposed
  // for binary persistence and GUI/index layers; route code should normally
  // use the typed create/update/destroy methods above.
  [[nodiscard]] GeometryRegistry& geometryRegistry() noexcept { return _geometry_registry; }
  [[nodiscard]] const GeometryRegistry& geometryRegistry() const noexcept { return _geometry_registry; }
  void clearGeometry() noexcept
  {
    _geometry_registry.registry().clear();
    markGeometryChanged();
  }

 private:
  void validateVia(const DesignViaInput& via, DesignViaId ignored = {}) const;
  void validateVia(const DesignVia& via, DesignViaId id) const;
  void validateNonDefaultRule(const DesignNonDefaultRule& rule, DesignNonDefaultRuleId ignored = {}) const;
  [[nodiscard]] TechRoutingLayerId viaExitLayer(TechRoutingLayerId layer, const DesignWireVia& via) const;
  void validateNetGeometry(DesignNetId net, const DesignNetGeometry& geometry) const;
  [[nodiscard]] bool viaNameInUse(std::string_view name, DesignViaId ignored = {}) const;
  [[nodiscard]] bool nonDefaultRuleNameInUse(std::string_view name, DesignNonDefaultRuleId ignored = {}) const;
  [[nodiscard]] bool referencesVia(DesignViaId id) const;
  void ensureVia(DesignViaId id) const;
  void ensureNonDefaultRule(DesignNonDefaultRuleId id) const;
  void ensureWire(DesignWireId id) const;

  DesignRegistry& _design_registry;
  registry_type& _registry;
  const TechRegistry& _tech_registry;
  GeometryRegistry& _geometry_registry;
  const GeometryRegistry* _tech_geometry_registry = nullptr;
  entt::dense_map<uint64_t, std::vector<DesignWireId>> _wires_by_net;
};

}  // namespace eccdb
