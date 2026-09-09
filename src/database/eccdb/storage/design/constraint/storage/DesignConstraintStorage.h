#pragma once

#include <string_view>
#include <vector>

#include "design/DesignRegistry.h"
#include "design/constraint/model/ConstraintComponents.h"

namespace eccdb {

class TechRegistry;

class DesignConstraintStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;

  DesignConstraintStorage(DesignRegistry& design_registry, const TechRegistry& tech_registry)
      : _registry(design_registry.registry()), _tech_registry(tech_registry)
  {
  }

  [[nodiscard]] DesignRegionId createRegion(DesignRegion region);
  void updateRegion(DesignRegionId id, DesignRegion region);
  [[nodiscard]] bool destroyRegion(DesignRegionId id);
  [[nodiscard]] bool contains(DesignRegionId id) const;
  [[nodiscard]] DesignRegionId findRegion(std::string_view name) const;
  [[nodiscard]] const DesignRegion& region(DesignRegionId id) const;
  [[nodiscard]] std::vector<DesignRegionId> regions() const;

  [[nodiscard]] DesignGroupId createGroup(DesignGroup group);
  void updateGroup(DesignGroupId id, DesignGroup group);
  [[nodiscard]] bool destroyGroup(DesignGroupId id);
  [[nodiscard]] bool contains(DesignGroupId id) const;
  [[nodiscard]] DesignGroupId findGroup(std::string_view name) const;
  [[nodiscard]] const DesignGroup& group(DesignGroupId id) const;
  [[nodiscard]] std::vector<DesignGroupId> groups() const;

  [[nodiscard]] DesignBlockageId createBlockage(DesignBlockage blockage);
  void updateBlockage(DesignBlockageId id, DesignBlockage blockage);
  [[nodiscard]] bool destroyBlockage(DesignBlockageId id);
  [[nodiscard]] bool contains(DesignBlockageId id) const;
  [[nodiscard]] const DesignBlockage& blockage(DesignBlockageId id) const;
  [[nodiscard]] std::vector<DesignBlockageId> blockages() const;

  [[nodiscard]] bool referencesInstance(DesignInstanceId id) const;
  [[nodiscard]] uint32_t regionCount() const;
  [[nodiscard]] uint32_t groupCount() const;
  [[nodiscard]] uint32_t blockageCount() const;

 private:
  void validateRegion(const DesignRegion& region, DesignRegionId ignored = {}) const;
  void validateGroup(const DesignGroup& group, DesignGroupId ignored = {}) const;
  void validateBlockage(const DesignBlockage& blockage) const;
  [[nodiscard]] bool regionNameInUse(std::string_view name, DesignRegionId ignored = {}) const;
  [[nodiscard]] bool groupNameInUse(std::string_view name, DesignGroupId ignored = {}) const;
  [[nodiscard]] bool regionIsReferenced(DesignRegionId id) const;
  void ensureRegion(DesignRegionId id) const;
  void ensureGroup(DesignGroupId id) const;
  void ensureBlockage(DesignBlockageId id) const;

  registry_type& _registry;
  const TechRegistry& _tech_registry;
};

}  // namespace eccdb
