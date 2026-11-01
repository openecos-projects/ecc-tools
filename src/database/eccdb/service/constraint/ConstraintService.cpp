// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "constraint/ConstraintService.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

#include "design/DesignStore.h"
#include "design/constraint/storage/DesignConstraintStorage.h"
#include "design/netlist/storage/DesignNetlistStorage.h"

namespace eccdb {
namespace {

void appendUnique(std::vector<DesignInstanceId>& instances, DesignInstanceId instance)
{
  if (instance && std::find(instances.begin(), instances.end(), instance) == instances.end()) {
    instances.push_back(instance);
  }
}

[[nodiscard]] std::vector<DesignInstanceId> changedMembers(const DesignGroup& before, const DesignGroup& after)
{
  std::vector<DesignInstanceId> result;
  result.reserve(before.instances.size() + after.instances.size());
  for (const auto instance : before.instances) {
    if (instance) {
      result.push_back(instance);
    }
  }
  for (const auto instance : after.instances) {
    if (instance) {
      result.push_back(instance);
    }
  }
  std::sort(result.begin(), result.end(), [](DesignInstanceId lhs, DesignInstanceId rhs) {
    return lhs.packed() < rhs.packed();
  });
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

[[nodiscard]] std::vector<GeometryShapeId> blockageShapes(const DesignBlockage& blockage)
{
  std::vector<GeometryShapeId> result;
  result.reserve(blockage.rectangles.size() + blockage.polygons.size());
  result.insert(result.end(), blockage.rectangles.begin(), blockage.rectangles.end());
  result.insert(result.end(), blockage.polygons.begin(), blockage.polygons.end());
  return result;
}

}  // namespace

ConstraintService::ConstraintService(DatabaseContext& context) noexcept : _context(context)
{
}

DesignStore& ConstraintService::design() const noexcept
{
  return _context.design();
}

void ConstraintService::ensureRegion(DesignRegionId region) const
{
  if (!design().constraintStorage().contains(region)) {
    throw std::invalid_argument("constraint service requires a live design region");
  }
}

void ConstraintService::ensureGroup(DesignGroupId group) const
{
  if (!design().constraintStorage().contains(group)) {
    throw std::invalid_argument("constraint service requires a live design group");
  }
}

void ConstraintService::ensureBlockage(DesignBlockageId blockage) const
{
  if (!design().constraintStorage().contains(blockage)) {
    throw std::invalid_argument("constraint service requires a live design blockage");
  }
}

void ConstraintService::ensureInstance(DesignInstanceId instance) const
{
  if (!design().netlistStorage().contains(instance)) {
    throw std::invalid_argument("constraint service requires a live design instance");
  }
}

void ConstraintService::notifyBefore(DatabaseChangeDomain domains,
                                     std::span<const DesignInstanceId> instances,
                                     std::span<const GeometryShapeId> shapes) noexcept
{
  _context.notifyBefore(DatabaseChange{.domains = domains, .instances = instances, .nets = {}, .shapes = shapes});
}

void ConstraintService::notifyAfter(DatabaseChangeDomain domains,
                                    std::span<const DesignInstanceId> instances,
                                    std::span<const GeometryShapeId> shapes,
                                    std::span<const GeometryOwner> replacements) noexcept
{
  _context.notifyAfter(DatabaseChange{.domains = domains, .instances = instances, .shapes = shapes,
                                     .geometry_replacements = replacements});
}

DesignRegionId ConstraintService::createRegion(DesignRegionInput region)
{
  auto& constraints = design().constraintStorage();
  const auto id = constraints.createRegion(std::move(region));
  notifyAfter(DatabaseChangeDomain::kConstraints | DatabaseChangeDomain::kGeometry, {}, constraints.region(id).rectangles);
  return id;
}

void ConstraintService::updateRegion(DesignRegionId region, DesignRegionInput value)
{
  ensureRegion(region);
  auto& constraints = design().constraintStorage();
  constraints.updateRegion(region, std::move(value));
  const std::array replacements{GeometryOwner::from(GeometryOwnerDomain::kDesignRegion, region)};
  notifyAfter(DatabaseChangeDomain::kConstraints | DatabaseChangeDomain::kGeometry, {},
              constraints.region(region).rectangles, replacements);
}

bool ConstraintService::deleteRegion(DesignRegionId region)
{
  auto& constraints = design().constraintStorage();
  if (!constraints.contains(region)) {
    return false;
  }

  // Complete discovery and all vector allocations before the first relation
  // changes. Region deletion must make the Storage-level reference check pass
  // without exposing partially detached membership on an allocation failure.
  auto& registry = design().designRegistry().registry();
  const auto instance_view = registry.view<const DesignInstance>();
  const auto group_view = registry.view<const DesignGroup>();
  std::vector<DesignInstanceId> instances;
  std::vector<DesignGroupId> groups;
  const auto shapes = constraints.region(region).rectangles;
  for (const auto entity : instance_view) {
    if (instance_view.get<const DesignInstance>(entity).region == region) {
      instances.emplace_back(entity);
    }
  }
  for (const auto entity : group_view) {
    const auto& group = group_view.get<const DesignGroup>(entity);
    if (group.region == region) {
      groups.emplace_back(entity);
      instances.insert(instances.end(), group.instances.begin(), group.instances.end());
    }
  }
  std::sort(instances.begin(), instances.end(), [](auto lhs, auto rhs) { return lhs.packed() < rhs.packed(); });
  instances.erase(std::unique(instances.begin(), instances.end()), instances.end());

  const auto domains = DatabaseChangeDomain::kConstraints | DatabaseChangeDomain::kGeometry
                       | DatabaseChangeDomain::kInstances;
  notifyBefore(domains, instances, shapes);
  for (const auto instance : instances) {
    auto& value = registry.get<DesignInstance>(instance.entity());
    if (value.region == region) {
      value.region = {};
      value.flags &= ~DesignInstanceFlag::kHasRegion;
    }
  }
  for (const auto group : groups) {
    auto& value = registry.get<DesignGroup>(group.entity());
    value.region = {};
    value.flags &= ~DesignGroupFlag::kHasRegion;
  }
  if (!constraints.destroyRegion(region)) {
    throw std::logic_error("constraint service could not remove a prepared design region");
  }
  notifyAfter(domains, instances, shapes);
  return true;
}

DesignGroupId ConstraintService::createGroup(DesignGroup group)
{
  const auto instances = group.instances;
  const auto id = design().constraintStorage().createGroup(std::move(group));
  notifyAfter(DatabaseChangeDomain::kConstraints | DatabaseChangeDomain::kInstances, instances);
  return id;
}

void ConstraintService::updateGroup(DesignGroupId group, DesignGroup value)
{
  ensureGroup(group);
  const auto& before = design().constraintStorage().group(group);
  const auto instances = changedMembers(before, value);
  design().constraintStorage().updateGroup(group, std::move(value));
  notifyAfter(DatabaseChangeDomain::kConstraints | DatabaseChangeDomain::kInstances, instances);
}

bool ConstraintService::deleteGroup(DesignGroupId group)
{
  auto& constraints = design().constraintStorage();
  if (!constraints.contains(group)) {
    return false;
  }
  const auto instances = constraints.group(group).instances;
  const auto domains = DatabaseChangeDomain::kConstraints | DatabaseChangeDomain::kInstances;
  notifyBefore(domains, instances);
  if (!constraints.destroyGroup(group)) {
    throw std::logic_error("constraint service could not remove a prepared design group");
  }
  notifyAfter(domains, instances);
  return true;
}

void ConstraintService::replaceGroupMembers(DesignGroupId group, std::span<const DesignInstanceId> members)
{
  ensureGroup(group);
  auto replacement = design().constraintStorage().group(group);
  replacement.instances.assign(members.begin(), members.end());
  updateGroup(group, std::move(replacement));
}

void ConstraintService::assignGroupRegion(DesignGroupId group, std::optional<DesignRegionId> region)
{
  ensureGroup(group);
  if (region.has_value()) {
    ensureRegion(*region);
  }
  auto replacement = design().constraintStorage().group(group);
  if (replacement.region == region.value_or(DesignRegionId{})
      && ((replacement.flags & DesignGroupFlag::kHasRegion) != 0u) == region.has_value()) {
    return;
  }
  replacement.region = region.value_or(DesignRegionId{});
  if (region.has_value()) {
    replacement.flags |= DesignGroupFlag::kHasRegion;
  } else {
    replacement.flags &= ~DesignGroupFlag::kHasRegion;
  }
  updateGroup(group, std::move(replacement));
}

DesignBlockageId ConstraintService::createBlockage(DesignBlockageInput blockage)
{
  std::vector<DesignInstanceId> instances;
  if (blockage.component) {
    instances.push_back(blockage.component);
  }
  auto& constraints = design().constraintStorage();
  const auto id = constraints.createBlockage(std::move(blockage));
  notifyAfter(DatabaseChangeDomain::kConstraints | DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kInstances,
              instances);
  return id;
}

void ConstraintService::updateBlockage(DesignBlockageId blockage, DesignBlockageInput value)
{
  ensureBlockage(blockage);
  std::vector<DesignInstanceId> instances;
  auto& constraints = design().constraintStorage();
  const auto& before = constraints.blockage(blockage);
  appendUnique(instances, before.component);
  appendUnique(instances, value.component);
  constraints.updateBlockage(blockage, std::move(value));
  const std::array replacements{GeometryOwner::from(GeometryOwnerDomain::kDesignBlockage, blockage)};
  notifyAfter(DatabaseChangeDomain::kConstraints | DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kInstances,
              instances, {}, replacements);
}

bool ConstraintService::deleteBlockage(DesignBlockageId blockage)
{
  auto& constraints = design().constraintStorage();
  if (!constraints.contains(blockage)) {
    return false;
  }
  const auto& value = constraints.blockage(blockage);
  const auto component = value.component;
  const auto shapes = blockageShapes(value);
  const std::array instances{component};
  auto domains = DatabaseChangeDomain::kConstraints | DatabaseChangeDomain::kGeometry;
  if (component) {
    domains = domains | DatabaseChangeDomain::kInstances;
  }
  notifyBefore(
      domains, component ? std::span<const DesignInstanceId>{instances} : std::span<const DesignInstanceId>{}, shapes);
  if (!constraints.destroyBlockage(blockage)) {
    throw std::logic_error("constraint service could not remove a prepared design blockage");
  }
  notifyAfter(
      domains, component ? std::span<const DesignInstanceId>{instances} : std::span<const DesignInstanceId>{}, shapes);
  return true;
}

void ConstraintService::assignInstanceRegion(DesignInstanceId instance, std::optional<DesignRegionId> region)
{
  ensureInstance(instance);
  if (region.has_value()) {
    ensureRegion(*region);
  }

  auto& netlist = design().netlistStorage();
  auto replacement = netlist.instance(instance);
  const auto replacement_region = region.value_or(DesignRegionId{});
  const bool replacement_has_region = region.has_value();
  if (replacement.region == replacement_region
      && ((replacement.flags & DesignInstanceFlag::kHasRegion) != 0u) == replacement_has_region) {
    return;
  }
  replacement.region = replacement_region;
  if (replacement_has_region) {
    replacement.flags |= DesignInstanceFlag::kHasRegion;
  } else {
    replacement.flags &= ~DesignInstanceFlag::kHasRegion;
  }
  // Storage enforces the named-vs-inline REGION invariant and leaves any
  // unrelated instance fields untouched.
  netlist.updateInstance(instance, std::move(replacement));
  const std::array instances{instance};
  notifyAfter(DatabaseChangeDomain::kConstraints | DatabaseChangeDomain::kInstances, instances);
}

bool ConstraintService::clearInstanceRegion(DesignInstanceId instance)
{
  ensureInstance(instance);
  const auto& value = design().netlistStorage().instance(instance);
  if (!value.region && (value.flags & DesignInstanceFlag::kHasRegion) == 0u) {
    return false;
  }
  assignInstanceRegion(instance, std::nullopt);
  return true;
}

}  // namespace eccdb
