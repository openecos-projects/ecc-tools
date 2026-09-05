#pragma once

#include <cstdint>
#include <vector>

#include "design/DesignRegistry.h"
#include "design/fill/model/FillComponents.h"

namespace eccdb {

class TechRegistry;

class DesignFillStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;

  DesignFillStorage(DesignRegistry& design_registry, const TechRegistry& tech_registry)
      : _registry(design_registry.registry()), _tech_registry(tech_registry)
  {
  }

  [[nodiscard]] DesignFillId createFill(DesignFill fill);
  void updateFill(DesignFillId id, DesignFill fill);
  [[nodiscard]] bool destroyFill(DesignFillId id);
  [[nodiscard]] bool contains(DesignFillId id) const;
  [[nodiscard]] const DesignFill& fill(DesignFillId id) const;
  [[nodiscard]] std::vector<DesignFillId> fills() const;
  [[nodiscard]] uint32_t fillCount() const;

 private:
  void validateFill(const DesignFill& fill) const;
  void ensureFill(DesignFillId id) const;

  registry_type& _registry;
  const TechRegistry& _tech_registry;
};

}  // namespace eccdb
