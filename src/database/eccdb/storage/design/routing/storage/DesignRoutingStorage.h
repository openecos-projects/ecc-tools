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

  DesignRoutingStorage(DesignRegistry& design_registry, const TechRegistry& tech_registry)
      : _registry(design_registry.registry()), _tech_registry(tech_registry)
  {
  }

  [[nodiscard]] DesignViaId createVia(DesignVia via);
  void updateVia(DesignViaId id, DesignVia via);
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
  [[nodiscard]] DesignRouteShapeId createShape(DesignWireId wire, DesignRouteShape shape,
                                              std::optional<DesignRouteShapeAttributes> attributes = std::nullopt);
  // Omitted attributes retain the current values; clearing value flags removes
  // them. A geometry-only edit does not discard DEF width/mask metadata.
  void updateShape(DesignRouteShapeId id, DesignRouteShape shape,
                   std::optional<DesignRouteShapeAttributes> attributes = std::nullopt);
  [[nodiscard]] bool destroyShape(DesignRouteShapeId id);
  [[nodiscard]] bool contains(DesignRouteShapeId id) const;
  // Live ID contract, like instanceTerm(): no repeated hot-path validation.
  [[nodiscard]] const DesignRouteShape& shape(DesignRouteShapeId id) const
  {
    return _registry.get<const DesignRouteShape>(id.entity());
  }
  [[nodiscard]] const DesignRouteShapeAttributes* shapeAttributes(DesignRouteShapeId id) const
  {
    return _registry.try_get<const DesignRouteShapeAttributes>(id.entity());
  }
  [[nodiscard]] Rect shapeBoundingBox(DesignRouteShapeId id) const;
  template <typename Function>
  void forEachShape(DesignWireId id, Function&& function) const
  {
    for (const auto shape_id : _registry.get<const DesignWire>(id.entity()).shapes) {
      std::invoke(function, shape_id, shape(shape_id), shapeAttributes(shape_id));
    }
  }

  [[nodiscard]] bool destroyWire(DesignWireId id);
  [[nodiscard]] bool contains(DesignWireId id) const;
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
  [[nodiscard]] const DesignNetGeometry* netGeometry(DesignNetId net) const;

  [[nodiscard]] uint32_t viaCount() const;
  [[nodiscard]] uint32_t nonDefaultRuleCount() const;
  [[nodiscard]] uint32_t wireCount() const;

 private:
  void validateVia(const DesignVia& via, DesignViaId ignored = {}) const;
  void validateNonDefaultRule(const DesignNonDefaultRule& rule, DesignNonDefaultRuleId ignored = {}) const;
  [[nodiscard]] TechRoutingLayerId viaExitLayer(TechRoutingLayerId layer, const DesignWireVia& via) const;
  void validateNetGeometry(DesignNetId net, const DesignNetGeometry& geometry) const;
  [[nodiscard]] bool viaNameInUse(std::string_view name, DesignViaId ignored = {}) const;
  [[nodiscard]] bool nonDefaultRuleNameInUse(std::string_view name, DesignNonDefaultRuleId ignored = {}) const;
  [[nodiscard]] bool referencesVia(DesignViaId id) const;
  void ensureVia(DesignViaId id) const;
  void ensureNonDefaultRule(DesignNonDefaultRuleId id) const;
  void ensureWire(DesignWireId id) const;

  registry_type& _registry;
  const TechRegistry& _tech_registry;
  entt::dense_map<uint64_t, std::vector<DesignWireId>> _wires_by_net;
};

}  // namespace eccdb
