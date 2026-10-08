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

#include "instance/InstanceService.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "common/GeometryBounds.h"
#include "design/DesignStore.h"
#include "design/constraint/storage/DesignConstraintStorage.h"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "design/pin_access/storage/DesignPinAccessStorage.h"

namespace eccdb {
namespace {

template <typename Id>
void appendUnique(std::vector<Id>& ids, Id id)
{
  if (id && std::find(ids.begin(), ids.end(), id) == ids.end()) {
    ids.push_back(id);
  }
}

}  // namespace

InstanceService::InstanceService(DatabaseContext& context) noexcept : _context(context) {}

DesignStore& InstanceService::design() const noexcept
{
  return _context.design();
}

void InstanceService::ensureInstance(DesignInstanceId instance) const
{
  if (!design().netlistStorage().contains(instance)) {
    throw std::invalid_argument("instance service requires a live design instance");
  }
}

std::vector<DesignNetId> InstanceService::connectedNets(DesignInstanceId instance) const
{
  const auto& netlist = design().netlistStorage();
  const auto pins = netlist.instancePins(instance);
  std::vector<DesignNetId> nets;
  nets.reserve(pins.size() * 2u);
  for (const auto pin : pins) {
    const auto& value = netlist.instanceTerm(pin);
    appendUnique(nets, value.net);
    appendUnique(nets, value.special_net);
  }
  return nets;
}

void InstanceService::clearPinAccess(DesignInstanceId instance) const
{
  auto& pin_access = design().pinAccessStorage();
  const auto pins = design().netlistStorage().instancePins(instance);
  // A group selection cannot be removed while any preferred entry remains.
  // Clear the sparse per-Term entries first, then remove the instance-level
  // selection.  Both operations are no-ops when no derived PA state exists.
  for (const auto pin : pins) {
    static_cast<void>(pin_access.clearPreferredInstancePinAccess(pin));
  }
  static_cast<void>(pin_access.clearInstancePinAccessGroup(instance));
}

void InstanceService::invalidateInstanceBounds(DesignInstanceId instance) const
{
  auto& registry = design().designRegistry().registry();
  registry.get_or_emplace<GeometryBounds>(instance.entity()).invalidate();
}

void InstanceService::notifyBefore(DatabaseChangeDomain domains,
                                   std::span<const DesignInstanceId> instances,
                                   std::span<const DesignNetId> nets) noexcept
{
  _context.notifyBefore(DatabaseChange{.domains = domains, .instances = instances, .nets = nets, .shapes = {}});
}

void InstanceService::notifyAfter(DatabaseChangeDomain domains,
                                  std::span<const DesignInstanceId> instances,
                                  std::span<const DesignNetId> nets,
                                  std::span<const GeometryOwner> replacements) noexcept
{
  _context.notifyAfter(DatabaseChange{.domains = domains, .instances = instances, .nets = nets,
                                     .geometry_replacements = replacements});
}

DesignInstanceId InstanceService::create(DesignInstance instance, std::span<const Rect> region_bounds)
{
  const auto id = design().netlistStorage().createInstance(std::move(instance), region_bounds);
  // A newly placed master occurrence is observable geometry even if it has no
  // inline COMPONENT REGION rectangles.  The storage only advances its epoch
  // for the latter, so cover the ordinary instance case here.
  if (region_bounds.empty()) {
    design().designRegistry().markGeometryChanged();
  }
  const std::array instances{id};
  notifyAfter(DatabaseChangeDomain::kInstances | DatabaseChangeDomain::kGeometry, instances, {});
  return id;
}

bool InstanceService::destroy(DesignInstanceId instance)
{
  if (!design().netlistStorage().contains(instance)) {
    return false;
  }
  auto& design_store = design();
  auto& netlist = design_store.netlistStorage();
  auto& constraints = design_store.constraintStorage();
  const auto pins = netlist.instancePins(instance);
  const auto nets = connectedNets(instance);
  for (const auto net : nets) {
    if ((netlist.net(net).flags & DesignNetFlag::kMustJoin) != 0u) {
      throw std::invalid_argument("delete the MUSTJOIN net before destroying its sole instance endpoint");
    }
  }
  std::vector<std::pair<DesignGroupId, DesignGroup>> groups_to_update;
  std::vector<DesignBlockageId> blockages_to_destroy;
  for (const auto group_id : constraints.groups()) {
    const auto& group = constraints.group(group_id);
    if (std::find(group.instances.begin(), group.instances.end(), instance) != group.instances.end()) {
      auto replacement = group;
      std::erase(replacement.instances, instance);
      groups_to_update.emplace_back(group_id, std::move(replacement));
    }
  }
  for (const auto blockage_id : constraints.blockages()) {
    if (constraints.blockage(blockage_id).component == instance) {
      blockages_to_destroy.push_back(blockage_id);
    }
  }
  const bool storage_destroys_geometry = !netlist.instance(instance).region_bounds.empty() || !blockages_to_destroy.empty();
  const std::array instances{instance};
  const auto domains = DatabaseChangeDomain::kInstances | DatabaseChangeDomain::kConnectivity
                       | DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kPinAccess
                       | DatabaseChangeDomain::kConstraints;
  notifyBefore(domains, instances, nets);

  // Group membership is removed and component-owned blockages are deleted
  // with their Instance.  Leaving either relation dangling would make the
  // netlist storage correctly reject the final destroy operation.
  for (auto& [group_id, replacement] : groups_to_update) {
    constraints.updateGroup(group_id, std::move(replacement));
  }
  for (const auto blockage_id : blockages_to_destroy) {
    static_cast<void>(constraints.destroyBlockage(blockage_id));
  }
  clearPinAccess(instance);
  // Detach only this instance's Terms.  Calling disconnectAll(net) here would
  // incorrectly remove unrelated endpoint relations on the same Net.
  for (const auto pin : pins) {
    netlist.disconnect(pin);
  }
  if (!netlist.destroyInstance(instance)) {
    throw std::logic_error("instance service could not remove a prepared instance");
  }
  if (!storage_destroys_geometry) {
    // The instance has no canonical inline shapes, but its placed master
    // occurrence still disappeared from physical/query views.
    design_store.designRegistry().markGeometryChanged();
  }
  notifyAfter(domains, instances, nets);
  return true;
}

void InstanceService::update(DesignInstanceId instance,
                             DesignInstance value,
                             std::span<const Rect> region_bounds)
{
  ensureInstance(instance);
  auto& netlist = design().netlistStorage();
  const auto& previous = netlist.instance(instance);
  if (value.master != previous.master) {
    throw std::invalid_argument("instance service requires swapMasterCompatible for master changes");
  }
  const bool placement_changed = value.origin != previous.origin || value.orientation != previous.orientation
                                 || value.placement_status != previous.placement_status;
  const bool region_bounds_replaced = !region_bounds.empty();
  const bool name_changed = value.name != previous.name;
  const bool constraint_changed = value.region != previous.region
      || ((value.flags ^ previous.flags) & DesignInstanceFlag::kHasRegion) != 0u;
  const bool halo_changed = ((value.flags ^ previous.flags) & (DesignInstanceFlag::kHasHalo | DesignInstanceFlag::kHaloSoft)) != 0u
      || value.halo.left != previous.halo.left || value.halo.bottom != previous.halo.bottom
      || value.halo.right != previous.halo.right || value.halo.top != previous.halo.top;
  const bool route_halo_changed = ((value.flags ^ previous.flags) & DesignInstanceFlag::kHasRouteHalo) != 0u
      || value.route_halo.distance != previous.route_halo.distance
      || value.route_halo.min_layer != previous.route_halo.min_layer
      || value.route_halo.max_layer != previous.route_halo.max_layer;
  if (!placement_changed && !region_bounds_replaced && !name_changed && value.source == previous.source
      && value.flags == previous.flags && value.weight == previous.weight && value.region == previous.region
      && value.region_bounds == previous.region_bounds
      && value.halo.left == previous.halo.left && value.halo.bottom == previous.halo.bottom
      && value.halo.right == previous.halo.right && value.halo.top == previous.halo.top
      && value.route_halo.distance == previous.route_halo.distance
      && value.route_halo.min_layer == previous.route_halo.min_layer
      && value.route_halo.max_layer == previous.route_halo.max_layer) {
    return;
  }
  const auto nets = connectedNets(instance);
  const std::array instances{instance};
  auto domains = DatabaseChangeDomain::kInstances;
  if (placement_changed || region_bounds_replaced || halo_changed) {
    domains = domains | DatabaseChangeDomain::kGeometry;
  }
  if (constraint_changed) domains = domains | DatabaseChangeDomain::kConstraints;
  if (halo_changed || route_halo_changed) domains = domains | DatabaseChangeDomain::kFloorplan;
  if (route_halo_changed) domains = domains | DatabaseChangeDomain::kRouting;
  if (placement_changed) {
    domains = domains | DatabaseChangeDomain::kPinAccess;
  }
  netlist.updateInstance(instance, std::move(value), region_bounds);
  if (placement_changed) {
    clearPinAccess(instance);
  }
  if (placement_changed || region_bounds_replaced || halo_changed) {
    invalidateInstanceBounds(instance);
  }
  if ((placement_changed || halo_changed) && !region_bounds_replaced) {
    design().designRegistry().markGeometryChanged();
  }
  const std::array replacements{GeometryOwner::from(GeometryOwnerDomain::kDesignInstance, instance)};
  notifyAfter(domains, instances, nets,
              region_bounds_replaced ? std::span<const GeometryOwner>{replacements} : std::span<const GeometryOwner>{});
}

void InstanceService::place(DesignInstanceId instance,
                            Point origin,
                            DesignOrientation orientation,
                            DesignPlacementStatus placement_status)
{
  ensureInstance(instance);
  auto value = design().netlistStorage().instance(instance);
  value.origin = origin;
  value.orientation = orientation;
  value.placement_status = placement_status;
  update(instance, std::move(value));
}

void InstanceService::move(DesignInstanceId instance, int32_t dx, int32_t dy)
{
  ensureInstance(instance);
  auto value = design().netlistStorage().instance(instance);
  const auto x = static_cast<int64_t>(value.origin.x) + dx;
  const auto y = static_cast<int64_t>(value.origin.y) + dy;
  if (x < std::numeric_limits<int32_t>::min() || x > std::numeric_limits<int32_t>::max()
      || y < std::numeric_limits<int32_t>::min() || y > std::numeric_limits<int32_t>::max()) {
    throw std::overflow_error("instance translation exceeds the DBU coordinate range");
  }
  value.origin = {static_cast<int32_t>(x), static_cast<int32_t>(y)};
  update(instance, std::move(value));
}

void InstanceService::rename(DesignInstanceId instance, std::string name)
{
  ensureInstance(instance);
  auto& netlist = design().netlistStorage();
  const auto nets = connectedNets(instance);
  const std::array instances{instance};
  auto value = netlist.instance(instance);
  if (value.name == name) {
    return;
  }
  value.name = std::move(name);
  netlist.updateInstance(instance, std::move(value));
  notifyAfter(DatabaseChangeDomain::kInstances, instances, nets);
}

void InstanceService::swapMasterCompatible(DesignInstanceId instance, LibraryCellMasterId master)
{
  ensureInstance(instance);
  if (design().netlistStorage().instance(instance).master == master) {
    return;
  }
  const auto nets = connectedNets(instance);
  const std::array instances{instance};
  const auto domains = DatabaseChangeDomain::kInstances | DatabaseChangeDomain::kConnectivity
                       | DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kPinAccess;
  design().netlistStorage().swapMasterCompatible(instance, master);
  clearPinAccess(instance);
  notifyAfter(domains, instances, nets);
}

}  // namespace eccdb
