#include "design/pin_access/storage/DesignPinAccessStorage.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/via/component/ViaComponents.h"
#include "library/LibraryRegistry.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {
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

bool hasPreferences(const DesignPinAccessStorage::registry_type& registry, DesignInstanceId instance)
{
  const auto* pins = registry.try_get<const DesignInstancePins>(instance.entity());
  if (pins == nullptr) {
    return false;
  }
  return std::any_of(pins->values.begin(), pins->values.end(), [&registry](const auto pin) {
    const auto* preferences = registry.try_get<const DesignInstancePreferredPinAccess>(pin.entity());
    return preferences != nullptr && !preferences->values.empty();
  });
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

void DesignPinAccessStorage::setIoPinPortCandidateGroup(DesignIoPinId pin,
                                                         uint32_t port_index,
                                                         uint32_t group_index,
                                                         std::vector<DesignPinAccessPoint> points)
{
  ensureIoPin(pin);
  const auto& io_pin = _registry.get<const DesignIoPin>(pin.entity());
  if (port_index >= io_pin.ports.size()) {
    throw std::out_of_range("IO pin access references an invalid port index");
  }

  DesignIoPinAccessGroups replacement;
  if (const auto* existing = _registry.try_get<const DesignIoPinAccessGroups>(pin.entity()); existing != nullptr) {
    replacement = *existing;
  }
  auto* port_groups = findIoPinPortGroups(replacement, port_index);
  if (port_groups == nullptr) {
    replacement.port_groups.push_back(DesignIoPinPortAccessGroups{.port_index = port_index});
    port_groups = &replacement.port_groups.back();
  }
  if (auto* group = findGroup(port_groups->groups, group_index); group != nullptr) {
    group->points = std::move(points);
  } else {
    port_groups->groups.push_back(DesignPinAccessCandidateGroup{.group_index = group_index, .points = std::move(points)});
  }
  validateIoPinGroups(pin, replacement);
  _registry.emplace_or_replace<DesignIoPinAccessGroups>(pin.entity(), std::move(replacement));
}

bool DesignPinAccessStorage::hasIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index) const
{
  ensureIoPin(pin);
  const auto& io_pin = _registry.get<const DesignIoPin>(pin.entity());
  if (port_index >= io_pin.ports.size()) {
    throw std::out_of_range("IO pin access references an invalid port index");
  }
  const auto* groups = findIoPinPortGroups(pin, port_index);
  return groups != nullptr && findGroup(groups->groups, group_index) != nullptr;
}

std::span<const DesignPinAccessCandidateGroup> DesignPinAccessStorage::ioPinPortCandidateGroups(DesignIoPinId pin,
                                                                                                    uint32_t port_index) const
{
  ensureIoPin(pin);
  const auto& io_pin = _registry.get<const DesignIoPin>(pin.entity());
  if (port_index >= io_pin.ports.size()) {
    throw std::out_of_range("IO pin access references an invalid port index");
  }
  const auto* groups = findIoPinPortGroups(pin, port_index);
  return groups == nullptr ? std::span<const DesignPinAccessCandidateGroup>{}
                           : std::span<const DesignPinAccessCandidateGroup>{groups->groups};
}

const DesignPinAccessCandidateGroup& DesignPinAccessStorage::ioPinPortCandidateGroup(DesignIoPinId pin,
                                                                                        uint32_t port_index,
                                                                                        uint32_t group_index) const
{
  ensureIoPin(pin);
  const auto& io_pin = _registry.get<const DesignIoPin>(pin.entity());
  if (port_index >= io_pin.ports.size()) {
    throw std::out_of_range("IO pin access references an invalid port index");
  }
  const auto* groups = findIoPinPortGroups(pin, port_index);
  const auto* group = groups == nullptr ? nullptr : findGroup(groups->groups, group_index);
  if (group == nullptr) {
    throw std::out_of_range("IO pin port has no such pin-access candidate group");
  }
  return *group;
}

bool DesignPinAccessStorage::clearIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index)
{
  ensureIoPin(pin);
  if (port_index >= _registry.get<const DesignIoPin>(pin.entity()).ports.size()) {
    throw std::out_of_range("IO pin access references an invalid port index");
  }
  auto* existing = _registry.try_get<DesignIoPinAccessGroups>(pin.entity());
  if (existing == nullptr) {
    return false;
  }
  auto* groups = findIoPinPortGroups(*existing, port_index);
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
    const auto port_found = std::find_if(existing->port_groups.begin(), existing->port_groups.end(), [port_index](const auto& entry) {
      return entry.port_index == port_index;
    });
    existing->port_groups.erase(port_found);
  }
  if (existing->port_groups.empty()) {
    _registry.remove<DesignIoPinAccessGroups>(pin.entity());
  }
  return true;
}

bool DesignPinAccessStorage::clearIoPinPortCandidateGroups(DesignIoPinId pin, uint32_t port_index)
{
  ensureIoPin(pin);
  if (port_index >= _registry.get<const DesignIoPin>(pin.entity()).ports.size()) {
    throw std::out_of_range("IO pin access references an invalid port index");
  }
  const auto* groups = findIoPinPortGroups(pin, port_index);
  if (groups == nullptr) {
    return false;
  }
  // Copy the indices because each clear can erase the containing group row.
  std::vector<uint32_t> group_indices;
  group_indices.reserve(groups->groups.size());
  for (const auto& group : groups->groups) {
    group_indices.push_back(group.group_index);
  }
  for (const auto group_index : group_indices) {
    (void) clearIoPinPortCandidateGroup(pin, port_index, group_index);
  }
  return true;
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
  _registry.emplace_or_replace<DesignInstancePreferredPinAccess>(
      pin.entity(), DesignInstancePreferredPinAccess{.values = std::move(preferences)});
}

std::span<const DesignPreferredPinAccessRef> DesignPinAccessStorage::preferredInstancePinAccess(DesignInstancePinId pin) const
{
  ensureInstancePin(pin);
  const auto* preferences = _registry.try_get<const DesignInstancePreferredPinAccess>(pin.entity());
  return preferences == nullptr ? std::span<const DesignPreferredPinAccessRef>{}
                              : std::span<const DesignPreferredPinAccessRef>{preferences->values};
}

bool DesignPinAccessStorage::clearPreferredInstancePinAccess(DesignInstancePinId pin)
{
  ensureInstancePin(pin);
  if (!_registry.all_of<DesignInstancePreferredPinAccess>(pin.entity())) {
    return false;
  }
  _registry.remove<DesignInstancePreferredPinAccess>(pin.entity());
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
  const auto io_pins = _registry.view<const DesignIoPinAccessGroups>();
  for (const auto entity : io_pins) {
    for (const auto& port_groups : io_pins.get<const DesignIoPinAccessGroups>(entity).port_groups) {
      if (references(port_groups.groups)) {
        return true;
      }
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
  const auto io_pins = _registry.view<const DesignIoPinAccessGroups>();
  for (const auto entity : io_pins) {
    for (const auto& port_groups : io_pins.get<const DesignIoPinAccessGroups>(entity).port_groups) {
      append(port_groups.groups);
    }
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
      validatePreferences(DesignInstancePinId{entity}, preferences.get<const DesignInstancePreferredPinAccess>(entity).values);
    }
    const auto io_pins = _registry.view<const DesignIoPinAccessGroups>();
    for (const auto entity : io_pins) {
      validateIoPinGroups(DesignIoPinId{entity}, io_pins.get<const DesignIoPinAccessGroups>(entity));
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
  if (!id || !_registry.valid(id.entity()) || !_registry.all_of<DesignInstancePin>(id.entity())) {
    throw std::out_of_range("invalid design instance pin id");
  }
}

void DesignPinAccessStorage::ensureIoPin(DesignIoPinId id) const
{
  if (!id || !_registry.valid(id.entity()) || !_registry.all_of<DesignIoPin>(id.entity())) {
    throw std::out_of_range("invalid design IO pin id");
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

void DesignPinAccessStorage::validateIoPinGroups(DesignIoPinId pin, const DesignIoPinAccessGroups& groups) const
{
  ensureIoPin(pin);
  const auto& io_pin = _registry.get<const DesignIoPin>(pin.entity());
  for (std::size_t index = 0; index < groups.port_groups.size(); ++index) {
    const auto& port_groups = groups.port_groups[index];
    if (port_groups.port_index >= io_pin.ports.size()
        || std::any_of(groups.port_groups.begin(), groups.port_groups.begin() + index,
                       [&port_groups](const auto& prior) { return prior.port_index == port_groups.port_index; })) {
      throw std::invalid_argument("IO pin-access port index is invalid or duplicated");
    }
    validateCandidateGroups(port_groups.groups);
  }
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
  const auto& instance_pin = _registry.get<const DesignInstancePin>(pin.entity());
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

const DesignIoPinPortAccessGroups* DesignPinAccessStorage::findIoPinPortGroups(DesignIoPinId pin, uint32_t port_index) const
{
  const auto* groups = _registry.try_get<const DesignIoPinAccessGroups>(pin.entity());
  if (groups == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(groups->port_groups.begin(), groups->port_groups.end(), [port_index](const auto& entry) {
    return entry.port_index == port_index;
  });
  return found == groups->port_groups.end() ? nullptr : &*found;
}

DesignIoPinPortAccessGroups* DesignPinAccessStorage::findIoPinPortGroups(DesignIoPinAccessGroups& groups,
                                                                           uint32_t port_index) const
{
  const auto found = std::find_if(groups.port_groups.begin(), groups.port_groups.end(), [port_index](const auto& entry) {
    return entry.port_index == port_index;
  });
  return found == groups.port_groups.end() ? nullptr : &*found;
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
    for (const auto& preference : preferences.get<const DesignInstancePreferredPinAccess>(entity).values) {
      if (preference.master_port == master_port && preference.group_index == group_index) {
        return true;
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
  return port.term == _registry.get<const DesignInstancePin>(pin.entity()).master_term;
}

}  // namespace eccdb
