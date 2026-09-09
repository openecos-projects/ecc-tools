#pragma once

#include <entt/entt.hpp>

#include "tech/common/TechEntity.h"

namespace eccdb {

// The technology object domain. Layer and rule storage facades share this
// registry so every complete Tech layer/rule receives an ID from one space.
class TechRegistry
{
 public:
  using entity_type = TechEntity;
  using registry_type = entt::basic_registry<entity_type>;

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }

 private:
  registry_type _registry;
};

}  // namespace eccdb
