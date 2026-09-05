#pragma once

#include <entt/entt.hpp>

#include "design/common/DesignTypes.h"

namespace eccdb {

class DesignRegistry
{
 public:
  using entity_type = DesignEntity;
  using registry_type = entt::basic_registry<entity_type>;

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }

 private:
  registry_type _registry;
};

}  // namespace eccdb
