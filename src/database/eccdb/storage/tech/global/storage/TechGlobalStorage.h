#pragma once

#include <cstdint>

#include "tech/TechRegistry.h"
#include "tech/global/model/TechGlobalComponents.h"

namespace eccdb {

class TechStore;

// EnTT facade for the singleton LEF statements owned by one TechRoot entity.
// The statements are components, not separate entities: they have no identity
// or lifecycle independent of their technology database.
class TechGlobalStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  TechGlobalStorage(TechRegistry& registry, TechRootId root) : _registry(registry.registry()), _root(root) {}

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }
  [[nodiscard]] TechRootId rootId() const noexcept { return _root; }
  [[nodiscard]] bool containsRoot() const noexcept;

  [[nodiscard]] bool hasUnits() const noexcept;
  [[nodiscard]] const TechGlobalUnits* tryGetUnits() const noexcept;
  [[nodiscard]] const TechGlobalUnits& getUnits() const;
  void setUnits(TechGlobalUnits units);
  void removeUnits() noexcept;

  [[nodiscard]] bool hasManufacturingGrid() const noexcept;
  [[nodiscard]] const TechManufacturingGrid* tryGetManufacturingGrid() const noexcept;
  [[nodiscard]] const TechManufacturingGrid& getManufacturingGrid() const;
  void setManufacturingGrid(int32_t manufacturing_grid);
  void removeManufacturingGrid() noexcept;

  [[nodiscard]] bool hasMaxViaStack() const noexcept;
  [[nodiscard]] const TechMaxViaStack* tryGetMaxViaStack() const noexcept;
  [[nodiscard]] const TechMaxViaStack& getMaxViaStack() const;
  void setMaxViaStack(TechMaxViaStack max_via_stack);
  void removeMaxViaStack() noexcept;

 private:
  friend class TechStore;

  void rebindRoot(TechRootId root) noexcept { _root = root; }

  [[nodiscard]] TechEntity rootEntity() const;
  [[nodiscard]] const TechLayerSequence& layerSequence() const;
  static void validateUnits(const TechGlobalUnits& units);
  static void validateManufacturingGrid(int32_t manufacturing_grid);
  [[nodiscard]] static bool hasRange(const TechMaxViaStack& max_via_stack) noexcept;
  void validateMaxViaStack(const TechMaxViaStack& max_via_stack) const;

  registry_type& _registry;
  TechRootId _root;
};

}  // namespace eccdb
