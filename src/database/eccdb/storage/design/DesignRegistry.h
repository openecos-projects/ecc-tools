// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <entt/entt.hpp>

#include "design/common/DesignTypes.h"
#include "geometry/GeometryRegistry.h"

namespace eccdb {

class DesignRegistry
{
 public:
  using entity_type = DesignEntity;
  using registry_type = entt::basic_registry<entity_type>;

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }
  [[nodiscard]] GeometryRegistry& geometryRegistry() noexcept { return _geometry; }
  [[nodiscard]] const GeometryRegistry& geometryRegistry() const noexcept { return _geometry; }

  // Geometry also depends on Design metadata, such as a Net's NDR width.
  // Keep one runtime revision for both routing and netlist edits.
  [[nodiscard]] uint64_t geometryEpoch() const noexcept { return _geometry_epoch; }
  void markGeometryChanged() noexcept { ++_geometry_epoch; }

 private:
  registry_type _registry;
  GeometryRegistry _geometry;
  uint64_t _geometry_epoch = 1;
};

}  // namespace eccdb
