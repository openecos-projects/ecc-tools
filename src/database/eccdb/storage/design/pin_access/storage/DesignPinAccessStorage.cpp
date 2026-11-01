// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "design/pin_access/storage/DesignPinAccessStorage.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <string>
#include <utility>

#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "design/via/component/ViaComponents.h"
#include "library/LibraryRegistry.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {

void DesignPinAccessStorage::replacePinAccess(DesignPinAccessUpdate update)
{
  ensureRoot();
  std::unordered_map<uint64_t, const DesignMasterPortPinAccessGroups*> ports;
  std::unordered_map<uint64_t, std::size_t> installed_port_indices;
  if (const auto* current = catalog()) {
    ports.reserve(current->master_port_groups.size() + update.master_ports.size());
    installed_port_indices.reserve(current->master_port_groups.size());
    for (std::size_t index = 0; index < current->master_port_groups.size(); ++index) {
      const auto& port = current->master_port_groups[index];
      ports.emplace(port.master_port.packed(), &port);
      installed_port_indices.emplace(port.master_port.packed(), index);
    }
  }
  std::unordered_set<uint64_t> changed_ports;
  for (const auto& port : update.master_ports) {
    ensureMasterPort(port.master_port);
    if (!changed_ports.insert(port.master_port.packed()).second)
      throw std::invalid_argument("pin-access update repeats a master port");
    validateCandidateGroups(port.groups);
    ports.insert_or_assign(port.master_port.packed(), &port);
  }
  std::unordered_set<uint64_t> changed_pins;
  for (const auto& pin : update.block_pins) {
    if (!changed_pins.insert(pin.pin.packed()).second) throw std::invalid_argument("pin-access update repeats a block pin");
    validateBlockPinGroups(pin.pin, pin.candidates);
  }
  std::unordered_map<uint64_t, const DesignInstancePinAccessUpdate*> instances;
  instances.reserve(update.instances.size());
  for (const auto& instance : update.instances) {
    ensureInstance(instance.instance);
    if (!instances.emplace(instance.instance.packed(), &instance).second)
      throw std::invalid_argument("pin-access update repeats an instance");
  }
  const auto group = [&](LibraryMasterPortId port, uint32_t index) -> const DesignPinAccessCandidateGroup* {
    const auto found = ports.find(port.packed());
    if (found == ports.end()) return nullptr;
    const auto& groups = found->second->groups;
    const auto candidate = std::find_if(groups.begin(), groups.end(), [index](const auto& value) {
      return value.group_index == index;
    });
    return candidate == groups.end() ? nullptr : &*candidate;
  };
  const auto validate_instance = [&](DesignInstanceId instance,
                                      std::optional<uint32_t> selection,
                                      const DesignInstancePreferredPinAccess* preferences) {
    if (!selection) {
      if (preferences && !preferences->entries.empty())
        throw std::invalid_argument("preferred pin access requires an instance group selection");
      return;
    }
    const auto& library = _library_registry.registry();
    const auto& master = library.get<const LibraryCellMaster>(_netlist.instance(instance).master.entity());
    bool has_group = false;
    for (const auto term_id : master.terms) {
      for (const auto port_id : library.get<const LibraryMasterTerm>(term_id.entity()).ports)
        has_group |= group(port_id, *selection) != nullptr;
    }
    if (!has_group) throw std::invalid_argument("instance selection is absent from proposed pin-access candidates");
    if (!preferences) return;
    for (auto entry_it = preferences->entries.begin(); entry_it != preferences->entries.end(); ++entry_it) {
      const auto& entry = *entry_it;
      ensureInstancePin(entry.pin);
      if (entry.values.empty() || _netlist.instancePin(entry.pin).instance != instance
          || std::any_of(preferences->entries.begin(), entry_it, [&](const auto& previous) { return previous.pin == entry.pin; }))
        throw std::invalid_argument("invalid preferred pin-access entry");
      for (auto reference_it = entry.values.begin(); reference_it != entry.values.end(); ++reference_it) {
        const auto& reference = *reference_it;
        const auto* candidate = group(reference.master_port, reference.group_index);
        if (!masterPortBelongsToInstancePin(reference.master_port, entry.pin) || reference.group_index != *selection
            || !candidate || reference.point_index >= candidate->points.size()
            || std::any_of(entry.values.begin(), reference_it, [&](const auto& previous) {
                 return previous.master_port == reference.master_port;
               }))
          throw std::invalid_argument("preferred access is absent from proposed pin-access candidates");
      }
    }
  };
  for (const auto& instance : update.instances)
    validate_instance(instance.instance, instance.group_index, &instance.preferences);
  // Only a catalog change can affect otherwise untouched instance references.
  // Check each dependent instance once, never rescan it per candidate point.
  if (!update.master_ports.empty()) {
    std::unordered_set<uint64_t> checked;
    for (const auto& instance : update.instances) checked.insert(instance.instance.packed());
    const auto validate_existing = [&](DesignEntity entity) {
      if (!checked.insert(entt::to_integral(entity)).second) return;
      const auto* selection = _registry.try_get<const DesignInstancePinAccessGroupIndex>(entity);
      validate_instance(DesignInstanceId{entity}, selection ? std::optional<uint32_t>{selection->group_index} : std::nullopt,
                        _registry.try_get<const DesignInstancePreferredPinAccess>(entity));
    };
    for (const auto entity : _registry.view<const DesignInstancePinAccessGroupIndex>()) validate_existing(entity);
    for (const auto entity : _registry.view<const DesignInstancePreferredPinAccess>()) validate_existing(entity);
  }

  std::vector<DesignEntity> new_pins, new_selections, new_preferences;
  new_pins.reserve(update.block_pins.size());
  new_selections.reserve(update.instances.size());
  new_preferences.reserve(update.instances.size());
  const bool had_catalog = _registry.all_of<DesignPinAccessCatalog>(_root.entity());
  try {
    if (!update.master_ports.empty()) {
      auto& value = _registry.get_or_emplace<DesignPinAccessCatalog>(_root.entity());
      value.master_port_groups.reserve(value.master_port_groups.size() + update.master_ports.size());
    }
    for (const auto& pin : update.block_pins) {
      if (!pin.candidates.groups.empty() && !_registry.all_of<DesignBlockPinAccessGroups>(pin.pin.entity())) {
        _registry.emplace<DesignBlockPinAccessGroups>(pin.pin.entity());
        new_pins.push_back(pin.pin.entity());
      }
    }
    for (const auto& instance : update.instances) {
      if (instance.group_index && !_registry.all_of<DesignInstancePinAccessGroupIndex>(instance.instance.entity())) {
        _registry.emplace<DesignInstancePinAccessGroupIndex>(instance.instance.entity());
        new_selections.push_back(instance.instance.entity());
      }
      if (!instance.preferences.entries.empty() && !_registry.all_of<DesignInstancePreferredPinAccess>(instance.instance.entity())) {
        _registry.emplace<DesignInstancePreferredPinAccess>(instance.instance.entity());
        new_preferences.push_back(instance.instance.entity());
      }
    }
  } catch (...) {
    for (const auto entity : new_pins) _registry.remove<DesignBlockPinAccessGroups>(entity);
    for (const auto entity : new_selections) _registry.remove<DesignInstancePinAccessGroupIndex>(entity);
    for (const auto entity : new_preferences) _registry.remove<DesignInstancePreferredPinAccess>(entity);
    if (!had_catalog) _registry.remove<DesignPinAccessCatalog>(_root.entity());
    throw;
  }
  if (!update.master_ports.empty()) {
    auto& installed = _registry.get<DesignPinAccessCatalog>(_root.entity()).master_port_groups;
    for (auto& port : update.master_ports) {
      const auto found = installed_port_indices.find(port.master_port.packed());
      if (found == installed_port_indices.end()) installed.push_back(std::move(port));
      else installed[found->second] = std::move(port);
    }
    std::erase_if(installed, [](const auto& port) { return port.groups.empty(); });
    if (installed.empty()) _registry.remove<DesignPinAccessCatalog>(_root.entity());
  }
  for (auto& pin : update.block_pins) {
    if (pin.candidates.groups.empty()) _registry.remove<DesignBlockPinAccessGroups>(pin.pin.entity());
    else _registry.get<DesignBlockPinAccessGroups>(pin.pin.entity()) = std::move(pin.candidates);
  }
  for (auto& instance : update.instances) {
    if (instance.group_index)
      _registry.get<DesignInstancePinAccessGroupIndex>(instance.instance.entity()).group_index = *instance.group_index;
    else _registry.remove<DesignInstancePinAccessGroupIndex>(instance.instance.entity());
    if (instance.preferences.entries.empty()) _registry.remove<DesignInstancePreferredPinAccess>(instance.instance.entity());
    else _registry.get<DesignInstancePreferredPinAccess>(instance.instance.entity()) = std::move(instance.preferences);
  }
}

void DesignPinAccessStorage::invalidateAll() noexcept
{
  const auto clear = [&]<typename Component>() {
    if (auto* storage = _registry.storage(entt::type_hash<Component>::value()); storage != nullptr) {
      storage->clear();
    }
  };
  clear.operator()<DesignInstancePreferredPinAccess>();
  clear.operator()<DesignInstancePinAccessGroupIndex>();
  clear.operator()<DesignBlockPinAccessGroups>();
  clear.operator()<DesignPinAccessCatalog>();
}

namespace {

bool isValidAccessType(DesignPinAccessType type) noexcept
{
  switch (type) {
    case DesignPinAccessType::kOnGrid:
    case DesignPinAccessType::kHalfGrid:
    case DesignPinAccessType::kCenter:
    case DesignPinAccessType::kEncOpt:
    case DesignPinAccessType::kNearbyGrid:
      return true;
  }
  return false;
}

template <typename Groups>
const DesignPinAccessCandidateGroup* findGroup(const Groups& groups, uint32_t group_index)
{
  const auto found = std::find_if(groups.begin(), groups.end(), [group_index](const auto& group) {
    return group.group_index == group_index;
  });
  return found == groups.end() ? nullptr : &*found;
}

template <typename Groups>
DesignPinAccessCandidateGroup* findGroup(Groups& groups, uint32_t group_index)
{
  const auto found = std::find_if(groups.begin(), groups.end(), [group_index](const auto& group) {
    return group.group_index == group_index;
  });
  return found == groups.end() ? nullptr : &*found;
}

template <typename Entries>
auto findPreferredEntry(Entries& entries, DesignInstancePinId pin)
{
  const auto found = std::find_if(entries.begin(), entries.end(), [pin](const auto& entry) {
    return entry.pin == pin;
  });
  return found == entries.end() ? nullptr : &*found;
}

bool hasPreferences(const DesignPinAccessStorage::registry_type& registry, DesignInstanceId instance)
{
  const auto* preferences = registry.try_get<const DesignInstancePreferredPinAccess>(instance.entity());
  return preferences != nullptr && !preferences->entries.empty();
}

}  // namespace

const DesignPinAccessCatalog* DesignPinAccessStorage::catalog() const
{
  ensureRoot();
  return _registry.try_get<const DesignPinAccessCatalog>(_root.entity());
}

void DesignPinAccessStorage::setMasterPortCandidateGroup(LibraryMasterPortId master_port,
                                                          uint32_t group_index,
                                                          std::vector<DesignPinAccessPoint> points)
{
  ensureRoot();
  ensureMasterPort(master_port);
  for (const auto& point : points) {
    validateAccessPoint(point);
  }

  if (auto* root_catalog = _registry.try_get<DesignPinAccessCatalog>(_root.entity()); root_catalog != nullptr) {
    auto* port_groups = findMasterPortGroups(*root_catalog, master_port);
    if (port_groups == nullptr) {
      // vector::push_back leaves the catalog unchanged if its allocation fails.
      root_catalog->master_port_groups.push_back(
          DesignMasterPortPinAccessGroups{.master_port = master_port,
                                          .groups = {{.group_index = group_index, .points = std::move(points)}}});
      return;
    }
    if (auto* group = findGroup(port_groups->groups, group_index); group != nullptr) {
      if (isCandidateGroupReferenced(master_port, group_index) || isCandidateGroupSelectedByInstance(master_port, group_index)) {
        throw std::invalid_argument("cannot replace a selected pin-access candidate group");
      }
      // Both vectors use the default allocator, so swap is constant-time and
      // non-throwing after the new point payload has been validated.
      group->points.swap(points);
      return;
    }
    port_groups->groups.push_back(DesignPinAccessCandidateGroup{.group_index = group_index, .points = std::move(points)});
    return;
  }

  DesignPinAccessCatalog root_catalog;
  root_catalog.master_port_groups.push_back(
      DesignMasterPortPinAccessGroups{.master_port = master_port, .groups = {{.group_index = group_index, .points = std::move(points)}}});
  _registry.emplace<DesignPinAccessCatalog>(_root.entity(), std::move(root_catalog));
}

bool DesignPinAccessStorage::hasMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index) const
{
  ensureRoot();
  ensureMasterPort(master_port);
  return findCandidateGroup(master_port, group_index) != nullptr;
}

std::span<const DesignPinAccessCandidateGroup> DesignPinAccessStorage::masterPortCandidateGroups(LibraryMasterPortId master_port) const
{
  ensureRoot();
  ensureMasterPort(master_port);
  const auto* groups = findMasterPortGroups(master_port);
  return groups == nullptr ? std::span<const DesignPinAccessCandidateGroup>{}
                           : std::span<const DesignPinAccessCandidateGroup>{groups->groups};
}

const DesignPinAccessCandidateGroup& DesignPinAccessStorage::masterPortCandidateGroup(LibraryMasterPortId master_port,
                                                                                         uint32_t group_index) const
{
  ensureRoot();
  ensureMasterPort(master_port);
  const auto* group = findCandidateGroup(master_port, group_index);
  if (group == nullptr) {
    throw std::out_of_range("master port has no such pin-access candidate group");
  }
  return *group;
}

bool DesignPinAccessStorage::clearMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index)
{
  ensureRoot();
  ensureMasterPort(master_port);
  auto* existing = _registry.try_get<DesignPinAccessCatalog>(_root.entity());
  if (existing == nullptr) {
    return false;
  }
  auto* groups = findMasterPortGroups(*existing, master_port);
  if (groups == nullptr) {
    return false;
  }
  const auto found = std::find_if(groups->groups.begin(), groups->groups.end(), [group_index](const auto& group) {
    return group.group_index == group_index;
  });
  if (found == groups->groups.end()) {
    return false;
  }
  if (isCandidateGroupReferenced(master_port, group_index)) {
    return false;
  }
  if (isCandidateGroupSelectedByInstance(master_port, group_index)) {
    return false;
  }
  groups->groups.erase(found);
  if (groups->groups.empty()) {
    const auto port_found = std::find_if(existing->master_port_groups.begin(), existing->master_port_groups.end(),
                                         [master_port](const auto& entry) { return entry.master_port == master_port; });
    existing->master_port_groups.erase(port_found);
  }
  if (existing->master_port_groups.empty()) {
    _registry.remove<DesignPinAccessCatalog>(_root.entity());
  }
  return true;
}

bool DesignPinAccessStorage::clearMasterPortCandidateGroups(LibraryMasterPortId master_port)
{
  ensureRoot();
  ensureMasterPort(master_port);
  const auto* groups = findMasterPortGroups(master_port);
  if (groups == nullptr) {
    return false;
  }
  for (const auto& group : groups->groups) {
    if (isCandidateGroupReferenced(master_port, group.group_index)
        || isCandidateGroupSelectedByInstance(master_port, group.group_index)) {
      return false;
    }
  }
  auto* existing = _registry.try_get<DesignPinAccessCatalog>(_root.entity());
  const auto found = std::find_if(existing->master_port_groups.begin(), existing->master_port_groups.end(),
                                  [master_port](const auto& entry) { return entry.master_port == master_port; });
  existing->master_port_groups.erase(found);
  if (existing->master_port_groups.empty()) {
    _registry.remove<DesignPinAccessCatalog>(_root.entity());
  }
  return true;
}

void DesignPinAccessStorage::setBlockPinCandidateGroup(DesignBlockPinId pin,
                                                        uint32_t group_index,
                                                        std::vector<DesignPinAccessPoint> points)
{
  ensureBlockPin(pin);

  DesignBlockPinAccessGroups replacement;
  if (const auto* existing = _registry.try_get<const DesignBlockPinAccessGroups>(pin.entity()); existing != nullptr) {
    replacement = *existing;
  }
  if (auto* group = findGroup(replacement.groups, group_index); group != nullptr) {
    group->points = std::move(points);
  } else {
    replacement.groups.push_back(DesignPinAccessCandidateGroup{.group_index = group_index, .points = std::move(points)});
  }
  validateBlockPinGroups(pin, replacement);
  _registry.emplace_or_replace<DesignBlockPinAccessGroups>(pin.entity(), std::move(replacement));
}

void DesignPinAccessStorage::appendBlockPinCandidatePoints(DesignBlockPinId pin,
                                                            uint32_t group_index,
                                                            std::vector<DesignPinAccessPoint> points)
{
  ensureBlockPin(pin);
  if (points.empty()) {
    return;
  }
  for (const auto& point : points) {
    validateAccessPoint(point);
  }

  // Do not mutate the installed component until the entire appended state
  // validates. In particular, a failed batch must not leave an empty group or
  // a partial prefix of its candidate points behind.
  DesignBlockPinAccessGroups replacement;
  if (const auto* existing = _registry.try_get<const DesignBlockPinAccessGroups>(pin.entity()); existing != nullptr) {
    replacement = *existing;
  }
  if (auto* group = findGroup(replacement.groups, group_index); group != nullptr) {
    group->points.reserve(group->points.size() + points.size());
    for (auto& point : points) {
      group->points.push_back(std::move(point));
    }
  } else {
    replacement.groups.push_back(DesignPinAccessCandidateGroup{.group_index = group_index, .points = std::move(points)});
  }
  validateBlockPinGroups(pin, replacement);
  _registry.emplace_or_replace<DesignBlockPinAccessGroups>(pin.entity(), std::move(replacement));
}

bool DesignPinAccessStorage::hasBlockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index) const
{
  ensureBlockPin(pin);
  const auto* groups = _registry.try_get<const DesignBlockPinAccessGroups>(pin.entity());
  return groups != nullptr && findGroup(groups->groups, group_index) != nullptr;
}

std::span<const DesignPinAccessCandidateGroup> DesignPinAccessStorage::blockPinCandidateGroups(DesignBlockPinId pin) const
{
  ensureBlockPin(pin);
  const auto* groups = _registry.try_get<const DesignBlockPinAccessGroups>(pin.entity());
  return groups == nullptr ? std::span<const DesignPinAccessCandidateGroup>{}
                           : std::span<const DesignPinAccessCandidateGroup>{groups->groups};
}

const DesignPinAccessCandidateGroup& DesignPinAccessStorage::blockPinCandidateGroup(DesignBlockPinId pin,
                                                                                       uint32_t group_index) const
{
  ensureBlockPin(pin);
  const auto* groups = _registry.try_get<const DesignBlockPinAccessGroups>(pin.entity());
  const auto* group = groups == nullptr ? nullptr : findGroup(groups->groups, group_index);
  if (group == nullptr) {
    throw std::out_of_range("block pin has no such pin-access candidate group");
  }
  return *group;
}

bool DesignPinAccessStorage::clearBlockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index)
{
  ensureBlockPin(pin);
  auto* groups = _registry.try_get<DesignBlockPinAccessGroups>(pin.entity());
  if (groups == nullptr) {
    return false;
  }
  const auto found = std::find_if(groups->groups.begin(), groups->groups.end(), [group_index](const auto& group) {
    return group.group_index == group_index;
  });
  if (found == groups->groups.end()) {
    return false;
  }
  groups->groups.erase(found);
  if (groups->groups.empty()) {
    _registry.remove<DesignBlockPinAccessGroups>(pin.entity());
  }
  return true;
}

bool DesignPinAccessStorage::clearBlockPinCandidateGroups(DesignBlockPinId pin)
{
  ensureBlockPin(pin);
  if (!_registry.all_of<DesignBlockPinAccessGroups>(pin.entity())) {
    return false;
  }
  _registry.remove<DesignBlockPinAccessGroups>(pin.entity());
  return true;
}

void DesignPinAccessStorage::setIoPinPortCandidateGroup(DesignIoPinId pin,
                                                         uint32_t port_index,
                                                         uint32_t group_index,
                                                         std::vector<DesignPinAccessPoint> points)
{
  setBlockPinCandidateGroup(blockPinAt(pin, port_index), group_index, std::move(points));
}

void DesignPinAccessStorage::appendIoPinPortCandidatePoints(DesignIoPinId pin,
                                                             uint32_t port_index,
                                                             uint32_t group_index,
                                                             std::vector<DesignPinAccessPoint> points)
{
  appendBlockPinCandidatePoints(blockPinAt(pin, port_index), group_index, std::move(points));
}

bool DesignPinAccessStorage::hasIoPinPortCandidateGroup(DesignIoPinId pin,
                                                         uint32_t port_index,
                                                         uint32_t group_index) const
{
  return hasBlockPinCandidateGroup(blockPinAt(pin, port_index), group_index);
}

std::span<const DesignPinAccessCandidateGroup> DesignPinAccessStorage::ioPinPortCandidateGroups(DesignIoPinId pin,
                                                                                                    uint32_t port_index) const
{
  return blockPinCandidateGroups(blockPinAt(pin, port_index));
}

const DesignPinAccessCandidateGroup& DesignPinAccessStorage::ioPinPortCandidateGroup(DesignIoPinId pin,
                                                                                        uint32_t port_index,
                                                                                        uint32_t group_index) const
{
  return blockPinCandidateGroup(blockPinAt(pin, port_index), group_index);
}

bool DesignPinAccessStorage::clearIoPinPortCandidateGroup(DesignIoPinId pin,
                                                           uint32_t port_index,
                                                           uint32_t group_index)
{
  return clearBlockPinCandidateGroup(blockPinAt(pin, port_index), group_index);
}

bool DesignPinAccessStorage::clearIoPinPortCandidateGroups(DesignIoPinId pin, uint32_t port_index)
{
  return clearBlockPinCandidateGroups(blockPinAt(pin, port_index));
}

void DesignPinAccessStorage::setInstancePinAccessGroup(DesignInstanceId instance, uint32_t group_index)
{
  ensureRoot();
  validateInstanceGroup(instance, group_index);
  const auto* previous = _registry.try_get<const DesignInstancePinAccessGroupIndex>(instance.entity());
  if (previous != nullptr && previous->group_index != group_index && hasPreferences(_registry, instance)) {
    throw std::invalid_argument("cannot change an instance pin-access group while preferred points exist");
  }
  _registry.emplace_or_replace<DesignInstancePinAccessGroupIndex>(instance.entity(),
                                                                    DesignInstancePinAccessGroupIndex{.group_index = group_index});
}

std::optional<uint32_t> DesignPinAccessStorage::instancePinAccessGroup(DesignInstanceId instance) const
{
  ensureInstance(instance);
  const auto* selection = _registry.try_get<const DesignInstancePinAccessGroupIndex>(instance.entity());
  return selection == nullptr ? std::nullopt : std::optional<uint32_t>{selection->group_index};
}

bool DesignPinAccessStorage::clearInstancePinAccessGroup(DesignInstanceId instance)
{
  ensureInstance(instance);
  if (hasPreferences(_registry, instance) || !_registry.all_of<DesignInstancePinAccessGroupIndex>(instance.entity())) {
    return false;
  }
  _registry.remove<DesignInstancePinAccessGroupIndex>(instance.entity());
  return true;
}

void DesignPinAccessStorage::setPreferredInstancePinAccess(DesignInstancePinId pin,
                                                            std::vector<DesignPreferredPinAccessRef> preferences)
{
  ensureRoot();
  ensureInstancePin(pin);
  if (preferences.empty()) {
    (void) clearPreferredInstancePinAccess(pin);
    return;
  }
  validatePreferences(pin, preferences);
  const auto instance = _netlist.instancePin(pin).instance;
  auto* existing = _registry.try_get<DesignInstancePreferredPinAccess>(instance.entity());
  if (existing == nullptr) {
    DesignInstancePreferredPinAccess replacement;
    replacement.entries.push_back(
        DesignInstancePreferredPinAccessEntry{.pin = pin, .values = std::move(preferences)});
    _registry.emplace<DesignInstancePreferredPinAccess>(instance.entity(), std::move(replacement));
    return;
  }
  if (auto* entry = findPreferredEntry(existing->entries, pin); entry != nullptr) {
    entry->values = std::move(preferences);
    return;
  }
  existing->entries.push_back(DesignInstancePreferredPinAccessEntry{.pin = pin, .values = std::move(preferences)});
}

std::span<const DesignPreferredPinAccessRef> DesignPinAccessStorage::preferredInstancePinAccess(DesignInstancePinId pin) const
{
  ensureInstancePin(pin);
  const auto instance = _netlist.instancePin(pin).instance;
  const auto* preferences = _registry.try_get<const DesignInstancePreferredPinAccess>(instance.entity());
  if (preferences == nullptr) {
    return {};
  }
  const auto* entry = findPreferredEntry(preferences->entries, pin);
  return entry == nullptr ? std::span<const DesignPreferredPinAccessRef>{}
                          : std::span<const DesignPreferredPinAccessRef>{entry->values};
}

bool DesignPinAccessStorage::clearPreferredInstancePinAccess(DesignInstancePinId pin)
{
  ensureInstancePin(pin);
  const auto instance = _netlist.instancePin(pin).instance;
  auto* preferences = _registry.try_get<DesignInstancePreferredPinAccess>(instance.entity());
  if (preferences == nullptr) {
    return false;
  }
  const auto found = std::find_if(preferences->entries.begin(), preferences->entries.end(), [pin](const auto& entry) {
    return entry.pin == pin;
  });
  if (found == preferences->entries.end()) {
    return false;
  }
  preferences->entries.erase(found);
  if (preferences->entries.empty()) {
    _registry.remove<DesignInstancePreferredPinAccess>(instance.entity());
  }
  return true;
}

bool DesignPinAccessStorage::referencesDesignVia(DesignViaId via) const
{
  if (!via || !_registry.valid(via.entity()) || !_registry.all_of<DesignVia>(via.entity())) {
    return false;
  }
  const auto references = [via](const auto& groups) {
    for (const auto& group : groups) {
      for (const auto& point : group.points) {
        if (std::any_of(point.vias.begin(), point.vias.end(), [via](const auto& candidate) {
              return candidate.design_via == via;
            })) {
          return true;
        }
      }
    }
    return false;
  };

  if (const auto* root_catalog = catalog(); root_catalog != nullptr) {
    for (const auto& port_groups : root_catalog->master_port_groups) {
      if (references(port_groups.groups)) {
        return true;
      }
    }
  }
  const auto block_pins = _registry.view<const DesignBlockPin, const DesignBlockPinAccessGroups>();
  for (const auto entity : block_pins) {
    if (references(block_pins.get<const DesignBlockPinAccessGroups>(entity).groups)) {
      return true;
    }
  }
  return false;
}

std::vector<DesignViaId> DesignPinAccessStorage::referencedDesignVias() const
{
  std::vector<DesignViaId> result;
  const auto append = [&result](const auto& groups) {
    for (const auto& group : groups) {
      for (const auto& point : group.points) {
        for (const auto& via : point.vias) {
          if (via.design_via && std::find(result.begin(), result.end(), via.design_via) == result.end()) {
            result.push_back(via.design_via);
          }
        }
      }
    }
  };
  if (const auto* root_catalog = catalog(); root_catalog != nullptr) {
    for (const auto& port_groups : root_catalog->master_port_groups) {
      append(port_groups.groups);
    }
  }
  const auto block_pins = _registry.view<const DesignBlockPin, const DesignBlockPinAccessGroups>();
  for (const auto entity : block_pins) {
    append(block_pins.get<const DesignBlockPinAccessGroups>(entity).groups);
  }
  return result;
}

void DesignPinAccessStorage::validateRestoredState() const
{
  try {
    ensureRoot();
    const auto catalogs = _registry.view<const DesignPinAccessCatalog>();
    for (const auto entity : catalogs) {
      if (entity != _root.entity()) {
        throw std::invalid_argument("pin-access catalog is not attached to the design root");
      }
      validateCatalog(catalogs.get<const DesignPinAccessCatalog>(entity));
    }
    const auto selections = _registry.view<const DesignInstancePinAccessGroupIndex>();
    for (const auto entity : selections) {
      validateInstanceGroup(DesignInstanceId{entity}, selections.get<const DesignInstancePinAccessGroupIndex>(entity).group_index);
    }
    const auto preferences = _registry.view<const DesignInstancePreferredPinAccess>();
    for (const auto entity : preferences) {
      validatePreferredAccess(DesignInstanceId{entity}, preferences.get<const DesignInstancePreferredPinAccess>(entity));
    }
    const auto block_pins = _registry.view<const DesignBlockPinAccessGroups>();
    for (const auto entity : block_pins) {
      validateBlockPinGroups(DesignBlockPinId{entity}, block_pins.get<const DesignBlockPinAccessGroups>(entity));
    }
  } catch (const std::exception& error) {
    throw std::runtime_error(std::string{"binary design has invalid pin-access state: "} + error.what());
  }
}

void DesignPinAccessStorage::ensureRoot() const
{
  if (!_root || !_registry.valid(_root.entity()) || !_registry.all_of<DesignRoot>(_root.entity())) {
    throw std::out_of_range("invalid design root id");
  }
}

void DesignPinAccessStorage::ensureInstance(DesignInstanceId id) const
{
  if (!id || !_registry.valid(id.entity()) || !_registry.all_of<DesignInstance, DesignInstancePins>(id.entity())) {
    throw std::out_of_range("invalid design instance id");
  }
}

void DesignPinAccessStorage::ensureInstancePin(DesignInstancePinId id) const
{
  if (!_netlist.contains(id)) {
    throw std::out_of_range("invalid design instance pin id");
  }
}

void DesignPinAccessStorage::ensureIoPin(DesignIoPinId id) const
{
  if (!id || !_registry.valid(id.entity()) || !_registry.all_of<DesignBlockTerm>(id.entity())) {
    throw std::out_of_range("invalid design IO pin id");
  }
}

void DesignPinAccessStorage::ensureBlockPin(DesignBlockPinId id) const
{
  if (!_netlist.contains(id)) {
    throw std::out_of_range("invalid design block pin id");
  }
}

void DesignPinAccessStorage::ensureMasterPort(LibraryMasterPortId id) const
{
  const auto& library = _library_registry.registry();
  if (!id || !library.valid(id.entity()) || !library.all_of<LibraryMasterPort>(id.entity())) {
    throw std::out_of_range("invalid library master port id");
  }
  const auto term = library.get<const LibraryMasterPort>(id.entity()).term;
  if (!term || !library.valid(term.entity()) || !library.all_of<LibraryMasterTerm>(term.entity())) {
    throw std::invalid_argument("library master port has an invalid owner term");
  }
}

void DesignPinAccessStorage::validateAccessPoint(const DesignPinAccessPoint& point) const
{
  const auto& tech = _tech_registry.registry();
  if (!point.layer || !tech.valid(point.layer.entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(point.layer.entity())
      || (point.access_directions & ~DesignPinAccessDirection::kAll) != 0u || !isValidAccessType(point.low_type)
      || !isValidAccessType(point.high_type)) {
    throw std::invalid_argument("pin-access point layer, direction mask or access type is invalid");
  }
  for (const auto& via : point.vias) {
    const bool has_tech_via = static_cast<bool>(via.tech_via);
    const bool has_design_via = static_cast<bool>(via.design_via);
    if (via.cut_count == 0u || has_tech_via == has_design_via
        || (has_tech_via && (!tech.valid(via.tech_via.entity()) || !tech.all_of<TechViaMaster>(via.tech_via.entity())))
        || (has_design_via && (!_registry.valid(via.design_via.entity()) || !_registry.all_of<DesignVia>(via.design_via.entity())))) {
      throw std::invalid_argument("pin-access point VIA reference is invalid");
    }
  }
  constexpr uint32_t kKnownSegmentFlags = DesignPinAccessPathSegmentFlag::kBeginTruncated | DesignPinAccessPathSegmentFlag::kEndTruncated;
  for (const auto& segment : point.path_segments) {
    if (!segment.rectangle.isValid() || (segment.rectangle.width() == 0 && segment.rectangle.height() == 0)
        || (segment.flags & ~kKnownSegmentFlags) != 0u) {
      throw std::invalid_argument("pin-access path segment geometry or flags are invalid");
    }
  }
}

void DesignPinAccessStorage::validateCandidateGroups(const std::vector<DesignPinAccessCandidateGroup>& groups) const
{
  for (std::size_t index = 0; index < groups.size(); ++index) {
    const auto& group = groups[index];
    if (std::any_of(groups.begin(), groups.begin() + index, [&group](const auto& prior) {
          return prior.group_index == group.group_index;
        })) {
      throw std::invalid_argument("pin-access candidate group index is duplicated");
    }
    for (const auto& point : group.points) {
      validateAccessPoint(point);
    }
  }
}

void DesignPinAccessStorage::validateCatalog(const DesignPinAccessCatalog& root_catalog) const
{
  for (std::size_t index = 0; index < root_catalog.master_port_groups.size(); ++index) {
    const auto& port_groups = root_catalog.master_port_groups[index];
    ensureMasterPort(port_groups.master_port);
    if (std::any_of(root_catalog.master_port_groups.begin(), root_catalog.master_port_groups.begin() + index,
                    [&port_groups](const auto& prior) { return prior.master_port == port_groups.master_port; })) {
      throw std::invalid_argument("pin-access catalog repeats a library master port");
    }
    validateCandidateGroups(port_groups.groups);
  }
}

void DesignPinAccessStorage::validateBlockPinGroups(DesignBlockPinId pin,
                                                     const DesignBlockPinAccessGroups& groups) const
{
  ensureBlockPin(pin);
  validateCandidateGroups(groups.groups);
}

void DesignPinAccessStorage::validateInstanceGroup(DesignInstanceId instance, uint32_t group_index) const
{
  ensureInstance(instance);
  if (!instanceHasCandidateGroup(instance, group_index)) {
    throw std::invalid_argument("design instance pin-access group is absent from its master-port candidates");
  }
}

void DesignPinAccessStorage::validatePreferences(DesignInstancePinId pin,
                                                  const std::vector<DesignPreferredPinAccessRef>& preferences) const
{
  ensureInstancePin(pin);
  const auto& instance_pin = _netlist.instancePin(pin);
  const auto* selection = _registry.try_get<const DesignInstancePinAccessGroupIndex>(instance_pin.instance.entity());
  if (selection == nullptr) {
    throw std::invalid_argument("preferred pin-access point requires an instance group selection");
  }
  validateInstanceGroup(instance_pin.instance, selection->group_index);
  for (std::size_t index = 0; index < preferences.size(); ++index) {
    const auto& preference = preferences[index];
    if (!masterPortBelongsToInstancePin(preference.master_port, pin) || preference.group_index != selection->group_index
        || std::any_of(preferences.begin(), preferences.begin() + index, [&preference](const auto& prior) {
             return prior.master_port == preference.master_port;
           })) {
      throw std::invalid_argument("preferred pin-access master port or group is invalid");
    }
    const auto* group = findCandidateGroup(preference.master_port, preference.group_index);
    if (group == nullptr || preference.point_index >= group->points.size()) {
      throw std::invalid_argument("preferred pin-access point index is invalid");
    }
  }
}

void DesignPinAccessStorage::validatePreferredAccess(DesignInstanceId instance,
                                                      const DesignInstancePreferredPinAccess& preferences) const
{
  ensureInstance(instance);
  if (preferences.entries.empty()) {
    throw std::invalid_argument("instance preferred pin-access component is empty");
  }
  for (std::size_t index = 0; index < preferences.entries.size(); ++index) {
    const auto& entry = preferences.entries[index];
    ensureInstancePin(entry.pin);
    if (entry.values.empty() || _netlist.instancePin(entry.pin).instance != instance
        || std::any_of(preferences.entries.begin(), preferences.entries.begin() + index,
                       [&entry](const auto& prior) { return prior.pin == entry.pin; })) {
      throw std::invalid_argument("instance preferred pin-access entry is invalid");
    }
    validatePreferences(entry.pin, entry.values);
  }
}

const DesignMasterPortPinAccessGroups* DesignPinAccessStorage::findMasterPortGroups(LibraryMasterPortId master_port) const
{
  const auto* root_catalog = catalog();
  if (root_catalog == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(root_catalog->master_port_groups.begin(), root_catalog->master_port_groups.end(),
                                  [master_port](const auto& entry) { return entry.master_port == master_port; });
  return found == root_catalog->master_port_groups.end() ? nullptr : &*found;
}

DesignMasterPortPinAccessGroups* DesignPinAccessStorage::findMasterPortGroups(DesignPinAccessCatalog& root_catalog,
                                                                                LibraryMasterPortId master_port) const
{
  const auto found = std::find_if(root_catalog.master_port_groups.begin(), root_catalog.master_port_groups.end(),
                                  [master_port](const auto& entry) { return entry.master_port == master_port; });
  return found == root_catalog.master_port_groups.end() ? nullptr : &*found;
}

DesignBlockPinId DesignPinAccessStorage::blockPinAt(DesignIoPinId pin, uint32_t port_index) const
{
  ensureIoPin(pin);
  const auto pins = _netlist.blockPins(DesignBlockTermId{pin.entity()});
  if (port_index >= pins.size()) {
    throw std::out_of_range("IO pin access references an invalid port index");
  }
  return pins[port_index];
}

const DesignPinAccessCandidateGroup* DesignPinAccessStorage::findCandidateGroup(LibraryMasterPortId master_port,
                                                                                   uint32_t group_index) const
{
  const auto* groups = findMasterPortGroups(master_port);
  return groups == nullptr ? nullptr : findGroup(groups->groups, group_index);
}

bool DesignPinAccessStorage::isCandidateGroupReferenced(LibraryMasterPortId master_port, uint32_t group_index) const
{
  const auto preferences = _registry.view<const DesignInstancePreferredPinAccess>();
  for (const auto entity : preferences) {
    for (const auto& entry : preferences.get<const DesignInstancePreferredPinAccess>(entity).entries) {
      for (const auto& preference : entry.values) {
        if (preference.master_port == master_port && preference.group_index == group_index) {
          return true;
        }
      }
    }
  }
  return false;
}

bool DesignPinAccessStorage::isCandidateGroupSelectedByInstance(LibraryMasterPortId master_port, uint32_t group_index) const
{
  const auto& library = _library_registry.registry();
  const auto& port = library.get<const LibraryMasterPort>(master_port.entity());
  const auto& term = library.get<const LibraryMasterTerm>(port.term.entity());
  const auto instances = _registry.view<const DesignInstance, const DesignInstancePinAccessGroupIndex>();
  for (const auto entity : instances) {
    const auto& instance = instances.get<const DesignInstance>(entity);
    const auto& selection = instances.get<const DesignInstancePinAccessGroupIndex>(entity);
    if (instance.master == term.master && selection.group_index == group_index) {
      return true;
    }
  }
  return false;
}

bool DesignPinAccessStorage::instanceHasCandidateGroup(DesignInstanceId instance, uint32_t group_index) const
{
  const auto& library = _library_registry.registry();
  const auto& design_instance = _registry.get<const DesignInstance>(instance.entity());
  if (!design_instance.master || !library.valid(design_instance.master.entity())
      || !library.all_of<LibraryCellMaster>(design_instance.master.entity())) {
    return false;
  }
  const auto& master = library.get<const LibraryCellMaster>(design_instance.master.entity());
  for (const auto term_id : master.terms) {
    if (!term_id || !library.valid(term_id.entity()) || !library.all_of<LibraryMasterTerm>(term_id.entity())) {
      return false;
    }
    const auto& term = library.get<const LibraryMasterTerm>(term_id.entity());
    for (const auto port_id : term.ports) {
      if (!port_id || !library.valid(port_id.entity()) || !library.all_of<LibraryMasterPort>(port_id.entity())) {
        return false;
      }
      if (findCandidateGroup(port_id, group_index) != nullptr) {
        return true;
      }
    }
  }
  return false;
}

bool DesignPinAccessStorage::masterPortBelongsToInstancePin(LibraryMasterPortId master_port, DesignInstancePinId pin) const
{
  ensureMasterPort(master_port);
  ensureInstancePin(pin);
  const auto& library = _library_registry.registry();
  const auto& port = library.get<const LibraryMasterPort>(master_port.entity());
  return port.term == _netlist.instancePin(pin).master_term;
}

}  // namespace eccdb
