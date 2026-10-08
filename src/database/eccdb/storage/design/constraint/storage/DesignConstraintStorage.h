// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#pragma once

#include <string_view>
#include <span>
#include <vector>

#include "design/DesignRegistry.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "geometry/DomainGeometryComponents.h"
#include "geometry/GeometryRegistry.h"

namespace eccdb {

class TechRegistry;

class DesignConstraintStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;

  DesignConstraintStorage(DesignRegistry& design_registry, const TechRegistry& tech_registry)
      : _design_registry(design_registry), _registry(design_registry.registry()), _tech_registry(tech_registry)
  {
  }

  [[nodiscard]] DesignRegionId createRegion(DesignRegionInput region);
  void updateRegion(DesignRegionId id, DesignRegionInput region);
  [[nodiscard]] bool destroyRegion(DesignRegionId id);
  [[nodiscard]] bool contains(DesignRegionId id) const;
  [[nodiscard]] DesignRegionId findRegion(std::string_view name) const;
  [[nodiscard]] const DesignRegion& region(DesignRegionId id) const;
  [[nodiscard]] GeometryRegistry& geometryRegistry() noexcept { return _design_registry.geometryRegistry(); }
  [[nodiscard]] const GeometryRegistry& geometryRegistry() const noexcept { return _design_registry.geometryRegistry(); }
  [[nodiscard]] Rect regionRectangle(GeometryShapeId id) const noexcept
  {
    return geometryRegistry().get<const GeometryRectangle>(id).rectangle;
  }
  [[nodiscard]] std::vector<DesignRegionId> regions() const;

  [[nodiscard]] DesignGroupId createGroup(DesignGroup group);
  void updateGroup(DesignGroupId id, DesignGroup group);
  [[nodiscard]] bool destroyGroup(DesignGroupId id);
  [[nodiscard]] bool contains(DesignGroupId id) const;
  [[nodiscard]] DesignGroupId findGroup(std::string_view name) const;
  [[nodiscard]] const DesignGroup& group(DesignGroupId id) const;
  [[nodiscard]] std::vector<DesignGroupId> groups() const;

  [[nodiscard]] DesignBlockageId createBlockage(DesignBlockageInput blockage);
  void updateBlockage(DesignBlockageId id, DesignBlockageInput blockage);
  [[nodiscard]] bool destroyBlockage(DesignBlockageId id);
  [[nodiscard]] bool contains(DesignBlockageId id) const;
  [[nodiscard]] const DesignBlockage& blockage(DesignBlockageId id) const;
  [[nodiscard]] Rect blockageRectangle(GeometryShapeId id) const noexcept
  {
    return geometryRegistry().get<const GeometryRectangle>(id).rectangle;
  }
  [[nodiscard]] std::span<const Point> blockagePolygon(GeometryShapeId id) const noexcept
  {
    return geometryRegistry().get<const GeometryPolygon>(id).points;
  }
  [[nodiscard]] std::vector<DesignBlockageId> blockages() const;

  [[nodiscard]] bool referencesInstance(DesignInstanceId id) const;
  [[nodiscard]] uint32_t regionCount() const;
  [[nodiscard]] uint32_t groupCount() const;
  [[nodiscard]] uint32_t blockageCount() const;

 private:
  void validateRegion(const DesignRegionInput& region, DesignRegionId ignored = {}) const;
  void validateGroup(const DesignGroup& group, DesignGroupId ignored = {}) const;
  void validateBlockage(const DesignBlockageInput& blockage) const;
  [[nodiscard]] DesignRegion materializeRegion(DesignRegionId id, DesignRegionInput region);
  [[nodiscard]] DesignBlockage materializeBlockage(DesignBlockageId id, DesignBlockageInput blockage);
  void destroyRegionGeometry(const DesignRegion& region) noexcept;
  void destroyBlockageGeometry(const DesignBlockage& blockage) noexcept;
  [[nodiscard]] bool regionNameInUse(std::string_view name, DesignRegionId ignored = {}) const;
  [[nodiscard]] bool groupNameInUse(std::string_view name, DesignGroupId ignored = {}) const;
  [[nodiscard]] bool regionIsReferenced(DesignRegionId id) const;
  void ensureRegion(DesignRegionId id) const;
  void ensureGroup(DesignGroupId id) const;
  void ensureBlockage(DesignBlockageId id) const;

  DesignRegistry& _design_registry;
  registry_type& _registry;
  const TechRegistry& _tech_registry;
};

}  // namespace eccdb
