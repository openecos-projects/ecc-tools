#pragma once

#include <cstdint>
#include <vector>

#include "design/DesignRegistry.h"
#include "design/fill/model/FillComponents.h"
#include "geometry/DomainGeometryComponents.h"
#include "geometry/GeometryRegistry.h"

namespace eccdb {

class TechRegistry;

class DesignFillStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;

  DesignFillStorage(DesignRegistry& design_registry, const TechRegistry& tech_registry)
      : _design_registry(design_registry), _registry(design_registry.registry()), _tech_registry(tech_registry)
  {
  }

  [[nodiscard]] DesignFillId createFill(DesignFillInput fill);
  void updateFill(DesignFillId id, DesignFillInput fill);
  [[nodiscard]] bool destroyFill(DesignFillId id);
  [[nodiscard]] bool contains(DesignFillId id) const;
  [[nodiscard]] const DesignFill& fill(DesignFillId id) const;
  [[nodiscard]] GeometryRegistry& geometryRegistry() noexcept { return _design_registry.geometryRegistry(); }
  [[nodiscard]] const GeometryRegistry& geometryRegistry() const noexcept { return _design_registry.geometryRegistry(); }
  [[nodiscard]] Rect rectangle(GeometryShapeId id) const noexcept
  {
    return geometryRegistry().get<const GeometryRectangle>(id).rectangle;
  }
  [[nodiscard]] std::vector<DesignFillId> fills() const;
  [[nodiscard]] uint32_t fillCount() const;

 private:
  void validateFill(const DesignFillInput& fill) const;
  [[nodiscard]] DesignFill materializeFill(DesignFillId id, DesignFillInput fill);
  void destroyFillGeometry(const DesignFill& fill) noexcept;
  void ensureFill(DesignFillId id) const;

  DesignRegistry& _design_registry;
  registry_type& _registry;
  const TechRegistry& _tech_registry;
};

}  // namespace eccdb
