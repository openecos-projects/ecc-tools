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

#include "design/constraint/storage/DesignConstraintStorage.h"

#include <stdexcept>
#include <utility>

#include <entt/container/dense_set.hpp>

#include "design/netlist/model/NetlistComponents.h"
#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {
namespace {

bool isValidRegionType(DesignRegionType type)
{
  return type == DesignRegionType::kFence || type == DesignRegionType::kGuide;
}

bool isValidBlockageKind(DesignBlockageKind kind)
{
  return kind == DesignBlockageKind::kPlacement || kind == DesignBlockageKind::kRouting;
}

void validateRectangles(const std::vector<Rect>& rectangles)
{
  if (rectangles.empty()) {
    throw std::invalid_argument("design constraint requires at least one rectangle");
  }
  for (const auto rectangle : rectangles) {
    if (!rectangle.isValid() || !rectangle.hasArea()) {
      throw std::invalid_argument("design constraint rectangle must have area");
    }
  }
}

void validatePolygons(const std::vector<std::vector<Point>>& polygons)
{
  for (const auto& polygon : polygons) {
    if (polygon.size() < 3u) {
      throw std::invalid_argument("design constraint polygon requires at least three points");
    }
  }
}

template <typename Id, typename Component>
std::vector<Id> componentIds(const DesignConstraintStorage::registry_type& registry)
{
  const auto view = registry.view<const Component>();
  std::vector<Id> result;
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

}  // namespace

DesignRegionId DesignConstraintStorage::createRegion(DesignRegionInput region)
{
  validateRegion(region);
  const auto entity = _registry.create();
  const DesignRegionId id{entity};
  DesignRegion materialized;
  try {
    materialized = materializeRegion(id, std::move(region));
    _registry.emplace<DesignRegion>(entity, std::move(materialized));
  } catch (...) {
    destroyRegionGeometry(materialized);
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
  _design_registry.markGeometryChanged();
  return id;
}

void DesignConstraintStorage::updateRegion(DesignRegionId id, DesignRegionInput region)
{
  ensureRegion(id);
  validateRegion(region, id);
  const auto old = this->region(id);
  auto replacement = materializeRegion(id, std::move(region));
  try {
    _registry.replace<DesignRegion>(id.entity(), std::move(replacement));
  } catch (...) {
    destroyRegionGeometry(replacement);
    throw;
  }
  destroyRegionGeometry(old);
  _design_registry.markGeometryChanged();
}

bool DesignConstraintStorage::destroyRegion(DesignRegionId id)
{
  if (!contains(id) || regionIsReferenced(id)) {
    return false;
  }
  const auto value = region(id);
  _registry.destroy(id.entity());
  destroyRegionGeometry(value);
  _design_registry.markGeometryChanged();
  return true;
}

bool DesignConstraintStorage::contains(DesignRegionId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignRegion>(id.entity());
}

DesignRegionId DesignConstraintStorage::findRegion(std::string_view name) const
{
  const auto view = _registry.view<const DesignRegion>();
  for (const auto entity : view) {
    if (view.get<const DesignRegion>(entity).name == name) {
      return DesignRegionId{entity};
    }
  }
  return {};
}

const DesignRegion& DesignConstraintStorage::region(DesignRegionId id) const
{
  ensureRegion(id);
  return _registry.get<const DesignRegion>(id.entity());
}

std::vector<DesignRegionId> DesignConstraintStorage::regions() const
{
  return componentIds<DesignRegionId, DesignRegion>(_registry);
}

DesignGroupId DesignConstraintStorage::createGroup(DesignGroup group)
{
  validateGroup(group);
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignGroup>(entity, std::move(group));
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignGroupId{entity};
}

void DesignConstraintStorage::updateGroup(DesignGroupId id, DesignGroup group)
{
  ensureGroup(id);
  validateGroup(group, id);
  _registry.replace<DesignGroup>(id.entity(), std::move(group));
}

bool DesignConstraintStorage::destroyGroup(DesignGroupId id)
{
  if (!contains(id)) {
    return false;
  }
  _registry.destroy(id.entity());
  return true;
}

bool DesignConstraintStorage::contains(DesignGroupId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignGroup>(id.entity());
}

DesignGroupId DesignConstraintStorage::findGroup(std::string_view name) const
{
  const auto view = _registry.view<const DesignGroup>();
  for (const auto entity : view) {
    if (view.get<const DesignGroup>(entity).name == name) {
      return DesignGroupId{entity};
    }
  }
  return {};
}

const DesignGroup& DesignConstraintStorage::group(DesignGroupId id) const
{
  ensureGroup(id);
  return _registry.get<const DesignGroup>(id.entity());
}

std::vector<DesignGroupId> DesignConstraintStorage::groups() const
{
  return componentIds<DesignGroupId, DesignGroup>(_registry);
}

DesignBlockageId DesignConstraintStorage::createBlockage(DesignBlockageInput blockage)
{
  validateBlockage(blockage);
  const auto entity = _registry.create();
  const DesignBlockageId id{entity};
  DesignBlockage materialized;
  try {
    materialized = materializeBlockage(id, std::move(blockage));
    _registry.emplace<DesignBlockage>(entity, std::move(materialized));
  } catch (...) {
    destroyBlockageGeometry(materialized);
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
  _design_registry.markGeometryChanged();
  return id;
}

void DesignConstraintStorage::updateBlockage(DesignBlockageId id, DesignBlockageInput blockage)
{
  ensureBlockage(id);
  validateBlockage(blockage);
  const auto old = this->blockage(id);
  auto replacement = materializeBlockage(id, std::move(blockage));
  try {
    _registry.replace<DesignBlockage>(id.entity(), std::move(replacement));
  } catch (...) {
    destroyBlockageGeometry(replacement);
    throw;
  }
  destroyBlockageGeometry(old);
  _design_registry.markGeometryChanged();
}

bool DesignConstraintStorage::destroyBlockage(DesignBlockageId id)
{
  if (!contains(id)) {
    return false;
  }
  const auto value = blockage(id);
  _registry.destroy(id.entity());
  destroyBlockageGeometry(value);
  _design_registry.markGeometryChanged();
  return true;
}

bool DesignConstraintStorage::contains(DesignBlockageId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignBlockage>(id.entity());
}

const DesignBlockage& DesignConstraintStorage::blockage(DesignBlockageId id) const
{
  ensureBlockage(id);
  return _registry.get<const DesignBlockage>(id.entity());
}

std::vector<DesignBlockageId> DesignConstraintStorage::blockages() const
{
  return componentIds<DesignBlockageId, DesignBlockage>(_registry);
}

bool DesignConstraintStorage::referencesInstance(DesignInstanceId id) const
{
  if (!id) {
    return false;
  }
  const auto group_view = _registry.view<const DesignGroup>();
  for (const auto entity : group_view) {
    for (const auto member : group_view.get<const DesignGroup>(entity).instances) {
      if (member == id) {
        return true;
      }
    }
  }
  const auto blockage_view = _registry.view<const DesignBlockage>();
  for (const auto entity : blockage_view) {
    if (blockage_view.get<const DesignBlockage>(entity).component == id) {
      return true;
    }
  }
  return false;
}

uint32_t DesignConstraintStorage::regionCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignRegion>().size());
}

uint32_t DesignConstraintStorage::groupCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignGroup>().size());
}

uint32_t DesignConstraintStorage::blockageCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignBlockage>().size());
}

void DesignConstraintStorage::validateRegion(const DesignRegionInput& value, DesignRegionId ignored) const
{
  if (value.name.empty() || regionNameInUse(value.name, ignored) || !isValidRegionType(value.type)) {
    throw std::invalid_argument("design region name or type is invalid");
  }
  validateRectangles(value.rectangles);
}

void DesignConstraintStorage::validateGroup(const DesignGroup& value, DesignGroupId ignored) const
{
  constexpr uint32_t kKnownFlags = DesignGroupFlag::kHasRegion;
  if (value.name.empty() || groupNameInUse(value.name, ignored) || (value.flags & ~kKnownFlags) != 0u) {
    throw std::invalid_argument("design group name or flags are invalid");
  }
  const bool has_region = (value.flags & DesignGroupFlag::kHasRegion) != 0u;
  if (has_region != static_cast<bool>(value.region) || (has_region && !contains(value.region))) {
    throw std::invalid_argument("design group region and flags are inconsistent");
  }

  entt::dense_set<uint32_t> members;
  members.reserve(value.instances.size());
  for (const auto instance : value.instances) {
    if (!instance || !_registry.valid(instance.entity()) || !_registry.all_of<DesignInstance>(instance.entity())
        || !members.emplace(instance.packed()).second) {
      throw std::invalid_argument("design group contains an invalid or duplicate instance");
    }
  }
}

void DesignConstraintStorage::validateBlockage(const DesignBlockageInput& value) const
{
  constexpr uint32_t kKnownFlags = DesignBlockageFlag::kHasLayer | DesignBlockageFlag::kSoft | DesignBlockageFlag::kPushdown
                                   | DesignBlockageFlag::kHasPartial | DesignBlockageFlag::kHasComponent | DesignBlockageFlag::kSlots
                                   | DesignBlockageFlag::kFills | DesignBlockageFlag::kExceptPgNet | DesignBlockageFlag::kHasSpacing
                                   | DesignBlockageFlag::kHasDesignRuleWidth | DesignBlockageFlag::kHasMask;
  if (!isValidBlockageKind(value.kind) || (value.flags & ~kKnownFlags) != 0u) {
    throw std::invalid_argument("design blockage kind or flags are invalid");
  }
  if (value.rectangles.empty() && value.polygons.empty()) {
    throw std::invalid_argument("design blockage requires geometry");
  }
  if (!value.rectangles.empty()) {
    validateRectangles(value.rectangles);
  }
  validatePolygons(value.polygons);

  const bool has_layer = (value.flags & DesignBlockageFlag::kHasLayer) != 0u;
  if ((value.kind == DesignBlockageKind::kRouting) != has_layer || has_layer != static_cast<bool>(value.layer)) {
    throw std::invalid_argument("design blockage kind, layer and flags are inconsistent");
  }
  if (has_layer) {
    const auto& registry = _tech_registry.registry();
    if (!registry.valid(value.layer.entity()) || !registry.all_of<TechLayerInfo, TechRoutingLayer>(value.layer.entity())) {
      throw std::invalid_argument("design routing blockage references an invalid routing layer");
    }
  }

  const bool has_component = (value.flags & DesignBlockageFlag::kHasComponent) != 0u;
  if (has_component != static_cast<bool>(value.component)
      || (has_component && (!_registry.valid(value.component.entity()) || !_registry.all_of<DesignInstance>(value.component.entity())))) {
    throw std::invalid_argument("design blockage component and flags are inconsistent");
  }

  const bool has_partial = (value.flags & DesignBlockageFlag::kHasPartial) != 0u;
  if ((has_partial && (value.partial < 0.0 || value.partial > 100.0)) || (!has_partial && value.partial != 0.0)) {
    throw std::invalid_argument("design blockage partial value and flags are inconsistent");
  }
  if (value.kind == DesignBlockageKind::kRouting && (value.flags & (DesignBlockageFlag::kSoft | DesignBlockageFlag::kHasPartial)) != 0u) {
    throw std::invalid_argument("design routing blockage cannot be soft or partial");
  }
  const bool has_spacing = (value.flags & DesignBlockageFlag::kHasSpacing) != 0u;
  const bool has_design_rule_width = (value.flags & DesignBlockageFlag::kHasDesignRuleWidth) != 0u;
  const bool has_mask = (value.flags & DesignBlockageFlag::kHasMask) != 0u;
  constexpr uint32_t kRoutingOnlyFlags = DesignBlockageFlag::kSlots | DesignBlockageFlag::kFills | DesignBlockageFlag::kExceptPgNet
                                         | DesignBlockageFlag::kHasSpacing | DesignBlockageFlag::kHasDesignRuleWidth
                                         | DesignBlockageFlag::kHasMask;
  if ((has_spacing && has_design_rule_width) || has_spacing == (value.spacing == 0)
      || has_design_rule_width == (value.design_rule_width == 0) || has_mask != (value.mask != 0u) || value.spacing < 0
      || value.design_rule_width < 0 || (value.kind == DesignBlockageKind::kPlacement && (value.flags & kRoutingOnlyFlags) != 0u)
      || (value.kind == DesignBlockageKind::kPlacement && !value.polygons.empty())) {
    throw std::invalid_argument("design blockage routing options and flags are inconsistent");
  }
}

DesignRegion DesignConstraintStorage::materializeRegion(DesignRegionId id, DesignRegionInput input)
{
  DesignRegion result{.name = std::move(input.name), .type = input.type};
  try {
    result.rectangles.reserve(input.rectangles.size());
    for (const auto rectangle : input.rectangles) {
      result.rectangles.push_back(
          geometryRegistry().create(designRegionOwner(id), GeometryRectangle{.rectangle = rectangle.normalized()}));
    }
    return result;
  } catch (...) {
    destroyRegionGeometry(result);
    throw;
  }
}

DesignBlockage DesignConstraintStorage::materializeBlockage(DesignBlockageId id, DesignBlockageInput input)
{
  DesignBlockage result{.kind = input.kind,
                        .flags = input.flags,
                        .layer = input.layer,
                        .component = input.component,
                        .partial = input.partial,
                        .spacing = input.spacing,
                        .design_rule_width = input.design_rule_width,
                        .mask = input.mask};
  try {
    result.rectangles.reserve(input.rectangles.size());
    result.polygons.reserve(input.polygons.size());
    for (const auto rectangle : input.rectangles) {
      result.rectangles.push_back(
          geometryRegistry().create(designBlockageOwner(id), GeometryRectangle{.rectangle = rectangle.normalized()}));
    }
    for (auto& polygon : input.polygons) {
      result.polygons.push_back(
          geometryRegistry().create(designBlockageOwner(id), GeometryPolygon{.points = std::move(polygon)}));
    }
    return result;
  } catch (...) {
    destroyBlockageGeometry(result);
    throw;
  }
}

void DesignConstraintStorage::destroyRegionGeometry(const DesignRegion& region) noexcept
{
  geometryRegistry().destroy(region.rectangles);
}

void DesignConstraintStorage::destroyBlockageGeometry(const DesignBlockage& blockage) noexcept
{
  geometryRegistry().destroy(blockage.rectangles);
  geometryRegistry().destroy(blockage.polygons);
}

bool DesignConstraintStorage::regionNameInUse(std::string_view name, DesignRegionId ignored) const
{
  const auto view = _registry.view<const DesignRegion>();
  for (const auto entity : view) {
    if (entity != ignored.entity() && view.get<const DesignRegion>(entity).name == name) {
      return true;
    }
  }
  return false;
}

bool DesignConstraintStorage::groupNameInUse(std::string_view name, DesignGroupId ignored) const
{
  const auto view = _registry.view<const DesignGroup>();
  for (const auto entity : view) {
    if (entity != ignored.entity() && view.get<const DesignGroup>(entity).name == name) {
      return true;
    }
  }
  return false;
}

bool DesignConstraintStorage::regionIsReferenced(DesignRegionId id) const
{
  const auto instance_view = _registry.view<const DesignInstance>();
  for (const auto entity : instance_view) {
    if (instance_view.get<const DesignInstance>(entity).region == id) {
      return true;
    }
  }
  const auto view = _registry.view<const DesignGroup>();
  for (const auto entity : view) {
    if (view.get<const DesignGroup>(entity).region == id) {
      return true;
    }
  }
  return false;
}

void DesignConstraintStorage::ensureRegion(DesignRegionId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design region id");
  }
}

void DesignConstraintStorage::ensureGroup(DesignGroupId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design group id");
  }
}

void DesignConstraintStorage::ensureBlockage(DesignBlockageId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design blockage id");
  }
}

}  // namespace eccdb
