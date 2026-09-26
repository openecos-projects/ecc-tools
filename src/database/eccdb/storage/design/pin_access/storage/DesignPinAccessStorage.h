#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "design/DesignRegistry.h"
#include "design/pin_access/model/PinAccessComponents.h"

namespace eccdb {

class LibraryRegistry;
class TechRegistry;

// Design-scope storage for TritonRoute/OpenDB-style pin-access data.  Library
// PORT candidates live once on DesignRoot; selected candidates live on the
// individual DesignInstancePin that uses them.
class DesignPinAccessStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;

  DesignPinAccessStorage(DesignRegistry& design_registry,
                         const TechRegistry& tech_registry,
                         const LibraryRegistry& library_registry,
                         DesignRootId root)
      : _registry(design_registry.registry()), _tech_registry(tech_registry), _library_registry(library_registry), _root(root)
  {
  }

  [[nodiscard]] DesignRootId rootId() const noexcept { return _root; }
  void rebindRoot(DesignRootId root) noexcept { _root = root; }

  [[nodiscard]] const DesignPinAccessCatalog* catalog() const;

  // Master-port candidate points use master-local coordinates.  The group
  // index is an explicit ODB/TritonRoute pin-access index, not a vector slot.
  void setMasterPortCandidateGroup(LibraryMasterPortId master_port,
                                   uint32_t group_index,
                                   std::vector<DesignPinAccessPoint> points);
  [[nodiscard]] bool hasMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index) const;
  [[nodiscard]] std::span<const DesignPinAccessCandidateGroup> masterPortCandidateGroups(LibraryMasterPortId master_port) const;
  [[nodiscard]] const DesignPinAccessCandidateGroup& masterPortCandidateGroup(LibraryMasterPortId master_port,
                                                                                 uint32_t group_index) const;
  // A referenced candidate group cannot be removed until the corresponding
  // instance-pin preferences are cleared.
  [[nodiscard]] bool clearMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index);
  [[nodiscard]] bool clearMasterPortCandidateGroups(LibraryMasterPortId master_port);

  // IO-port candidate points use block-absolute coordinates.  Both indices
  // are retained because a DesignIoPin can have multiple DEF PORTs and each
  // ODB dbBPin can hold multiple candidate groups.
  void setIoPinPortCandidateGroup(DesignIoPinId pin,
                                  uint32_t port_index,
                                  uint32_t group_index,
                                  std::vector<DesignPinAccessPoint> points);
  [[nodiscard]] bool hasIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index) const;
  [[nodiscard]] std::span<const DesignPinAccessCandidateGroup> ioPinPortCandidateGroups(DesignIoPinId pin,
                                                                                            uint32_t port_index) const;
  [[nodiscard]] const DesignPinAccessCandidateGroup& ioPinPortCandidateGroup(DesignIoPinId pin,
                                                                                uint32_t port_index,
                                                                                uint32_t group_index) const;
  [[nodiscard]] bool clearIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index);
  [[nodiscard]] bool clearIoPinPortCandidateGroups(DesignIoPinId pin, uint32_t port_index);

  // A group selection belongs to an instance.  Setting a selection requires
  // at least one physical library port of that instance's master to carry the
  // requested group index in the root catalog.
  void setInstancePinAccessGroup(DesignInstanceId instance, uint32_t group_index);
  [[nodiscard]] std::optional<uint32_t> instancePinAccessGroup(DesignInstanceId instance) const;
  [[nodiscard]] bool clearInstancePinAccessGroup(DesignInstanceId instance);

  // Preferred entries are owned by a logical instance terminal.  Each entry
  // identifies one candidate by (master_port, group_index, point_index).
  void setPreferredInstancePinAccess(DesignInstancePinId pin, std::vector<DesignPreferredPinAccessRef> preferences);
  [[nodiscard]] std::span<const DesignPreferredPinAccessRef> preferredInstancePinAccess(DesignInstancePinId pin) const;
  [[nodiscard]] bool clearPreferredInstancePinAccess(DesignInstancePinId pin);

  // Design-local VIA ownership is relevant when routing storage considers
  // deleting a DesignVia.
  [[nodiscard]] bool referencesDesignVia(DesignViaId via) const;
  [[nodiscard]] std::vector<DesignViaId> referencedDesignVias() const;

  // Run after direct EnTT/binary restoration.  Normal mutating APIs validate
  // their complete replacement values before committing them.
  void validateRestoredState() const;

 private:
  void ensureRoot() const;
  void ensureInstance(DesignInstanceId id) const;
  void ensureInstancePin(DesignInstancePinId id) const;
  void ensureIoPin(DesignIoPinId id) const;
  void ensureMasterPort(LibraryMasterPortId id) const;

  void validateAccessPoint(const DesignPinAccessPoint& point) const;
  void validateCandidateGroups(const std::vector<DesignPinAccessCandidateGroup>& groups) const;
  void validateCatalog(const DesignPinAccessCatalog& catalog) const;
  void validateIoPinGroups(DesignIoPinId pin, const DesignIoPinAccessGroups& groups) const;
  void validateInstanceGroup(DesignInstanceId instance, uint32_t group_index) const;
  void validatePreferences(DesignInstancePinId pin, const std::vector<DesignPreferredPinAccessRef>& preferences) const;

  [[nodiscard]] const DesignMasterPortPinAccessGroups* findMasterPortGroups(LibraryMasterPortId master_port) const;
  [[nodiscard]] DesignMasterPortPinAccessGroups* findMasterPortGroups(DesignPinAccessCatalog& catalog,
                                                                        LibraryMasterPortId master_port) const;
  [[nodiscard]] const DesignIoPinPortAccessGroups* findIoPinPortGroups(DesignIoPinId pin, uint32_t port_index) const;
  [[nodiscard]] DesignIoPinPortAccessGroups* findIoPinPortGroups(DesignIoPinAccessGroups& groups, uint32_t port_index) const;
  [[nodiscard]] const DesignPinAccessCandidateGroup* findCandidateGroup(LibraryMasterPortId master_port,
                                                                          uint32_t group_index) const;
  [[nodiscard]] bool isCandidateGroupReferenced(LibraryMasterPortId master_port, uint32_t group_index) const;
  [[nodiscard]] bool isCandidateGroupSelectedByInstance(LibraryMasterPortId master_port, uint32_t group_index) const;
  [[nodiscard]] bool instanceHasCandidateGroup(DesignInstanceId instance, uint32_t group_index) const;
  [[nodiscard]] bool masterPortBelongsToInstancePin(LibraryMasterPortId master_port, DesignInstancePinId pin) const;

  registry_type& _registry;
  const TechRegistry& _tech_registry;
  const LibraryRegistry& _library_registry;
  DesignRootId _root;
};

}  // namespace eccdb
