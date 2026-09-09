#pragma once

#include "design/DesignRegistry.h"
#include "design/global/model/DesignGlobalComponents.h"

namespace eccdb {

class DesignGlobalStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;

  DesignGlobalStorage(DesignRegistry& registry, DesignRootId root) : _registry(registry.registry()), _root(root) {}

  [[nodiscard]] DesignRootId rootId() const noexcept { return _root; }
  [[nodiscard]] bool containsRoot() const noexcept;
  void rebindRoot(DesignRootId root) noexcept { _root = root; }

  [[nodiscard]] bool hasInfo() const noexcept;
  [[nodiscard]] const DesignInfo& info() const;
  void setInfo(DesignInfo info);

  [[nodiscard]] bool hasDieArea() const noexcept;
  [[nodiscard]] const DesignDieArea& dieArea() const;
  [[nodiscard]] Rect dieBounds() const;
  void setDieArea(DesignDieArea die_area);

 private:
  [[nodiscard]] DesignEntity rootEntity() const;
  static void validateInfo(const DesignInfo& info);
  static Rect validateAndGetDieBounds(const DesignDieArea& die_area);

  registry_type& _registry;
  DesignRootId _root;
};

}  // namespace eccdb
