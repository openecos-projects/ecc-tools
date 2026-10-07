#pragma once

#include <span>

#include "design/DesignRegistry.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "geometry/GeometryRegistry.h"

namespace eccdb {

class DesignGlobalStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;

  DesignGlobalStorage(DesignRegistry& registry, DesignRootId root) : _design_registry(registry), _registry(registry.registry()), _root(root) {}

  [[nodiscard]] DesignRootId rootId() const noexcept { return _root; }
  [[nodiscard]] bool containsRoot() const noexcept;
  void rebindRoot(DesignRootId root) noexcept { _root = root; }

  [[nodiscard]] bool hasInfo() const noexcept;
  [[nodiscard]] const DesignInfo& info() const;
  void setInfo(DesignInfo info);

  [[nodiscard]] bool hasDieArea() const noexcept;
  [[nodiscard]] const DesignDieArea& dieArea() const;
  [[nodiscard]] std::span<const Point> dieBoundary() const;
  [[nodiscard]] Rect dieBounds() const;
  void setDieArea(DesignDieAreaInput die_area);
  [[nodiscard]] GeometryRegistry& geometryRegistry() noexcept { return _design_registry.geometryRegistry(); }
  [[nodiscard]] const GeometryRegistry& geometryRegistry() const noexcept { return _design_registry.geometryRegistry(); }

 private:
  [[nodiscard]] DesignEntity rootEntity() const;
  static void validateInfo(const DesignInfo& info);
  static Rect validateAndGetDieBounds(std::span<const Point> boundary);

  DesignRegistry& _design_registry;
  registry_type& _registry;
  DesignRootId _root;
};

}  // namespace eccdb
