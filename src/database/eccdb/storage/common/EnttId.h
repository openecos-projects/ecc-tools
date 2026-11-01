// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <entt/entt.hpp>

namespace eccdb {

// Strong id exposed by the eccdb storage layer. The underlying value is an
// EnTT entity identifier owned by exactly one registry domain.
template <typename Entity, typename Component>
class EnttId
{
 public:
  EnttId() noexcept = default;
  explicit EnttId(Entity entity) noexcept : _entity(entity) {}

  [[nodiscard]] Entity entity() const noexcept { return _entity; }
  explicit operator bool() const noexcept { return _entity != entt::null; }

  [[nodiscard]] auto packed() const noexcept { return entt::to_integral(_entity); }

  friend bool operator==(const EnttId&, const EnttId&) = default;

 private:
  Entity _entity = entt::null;
};

}  // namespace eccdb
