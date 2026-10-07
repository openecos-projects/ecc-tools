// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "design/guide/model/GuideComponents.h"
#include "design/pin_access/model/PinAccessComponents.h"

namespace eccdb {

class DesignStore;

// Adapter-facing ownership facade for global-routing context and pin-access
// state. It deliberately exposes EccDB value types and stable IDs rather than
// ODB objects; consistency rules remain centralized in DesignStore storage.
class RoutingContextService
{
 public:
  explicit RoutingContextService(DesignStore& design);

  [[nodiscard]] DesignStore& design() noexcept { return _design; }
  [[nodiscard]] const DesignStore& design() const noexcept { return _design; }

  [[nodiscard]] bool hasGuides(DesignNetId net) const;
  [[nodiscard]] std::span<const GeometryShapeId> guides(DesignNetId net) const;
  [[nodiscard]] const DesignRouteGuide& guide(GeometryShapeId id) const;
  void replaceGuides(DesignNetId net, std::vector<DesignRouteGuide> guides);
  void clearGuides(DesignNetId net);

  [[nodiscard]] bool hasTrackAssignments(DesignNetId net) const;
  [[nodiscard]] std::span<const GeometryShapeId> trackAssignments(DesignNetId net) const;
  [[nodiscard]] const DesignTrackAssignmentShape& trackAssignment(GeometryShapeId id) const;
  void replaceTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> assignments);
  void appendTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> assignments);
  void clearTrackAssignments(DesignNetId net);

  [[nodiscard]] bool hasMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index) const;
  [[nodiscard]] std::span<const DesignPinAccessCandidateGroup> masterPortCandidateGroups(LibraryMasterPortId master_port) const;
  [[nodiscard]] const DesignPinAccessCandidateGroup& masterPortCandidateGroup(LibraryMasterPortId master_port,
                                                                                 uint32_t group_index) const;
  void setMasterPortCandidateGroup(LibraryMasterPortId master_port,
                                   uint32_t group_index,
                                   std::vector<DesignPinAccessPoint> points);
  [[nodiscard]] bool clearMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index);
  [[nodiscard]] bool clearMasterPortCandidateGroups(LibraryMasterPortId master_port);

  // Candidate groups belong to an independently identified physical dbBPin.
  [[nodiscard]] bool hasBlockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index) const;
  [[nodiscard]] std::span<const DesignPinAccessCandidateGroup> blockPinCandidateGroups(DesignBlockPinId pin) const;
  [[nodiscard]] const DesignPinAccessCandidateGroup& blockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index) const;
  void setBlockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index, std::vector<DesignPinAccessPoint> points);
  void appendBlockPinCandidatePoints(DesignBlockPinId pin, uint32_t group_index, std::vector<DesignPinAccessPoint> points);
  [[nodiscard]] bool clearBlockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index);
  [[nodiscard]] bool clearBlockPinCandidateGroups(DesignBlockPinId pin);

  // Compatibility bridge for traces that identify BPin by BTerm + ordinal.
  [[nodiscard]] bool hasIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index) const;
  [[nodiscard]] std::span<const DesignPinAccessCandidateGroup> ioPinPortCandidateGroups(DesignIoPinId pin,
                                                                                            uint32_t port_index) const;
  [[nodiscard]] const DesignPinAccessCandidateGroup& ioPinPortCandidateGroup(DesignIoPinId pin,
                                                                                uint32_t port_index,
                                                                                uint32_t group_index) const;
  void setIoPinPortCandidateGroup(DesignIoPinId pin,
                                  uint32_t port_index,
                                  uint32_t group_index,
                                  std::vector<DesignPinAccessPoint> points);
  void appendIoPinPortCandidatePoints(DesignIoPinId pin,
                                      uint32_t port_index,
                                      uint32_t group_index,
                                      std::vector<DesignPinAccessPoint> points);
  [[nodiscard]] bool clearIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index);
  [[nodiscard]] bool clearIoPinPortCandidateGroups(DesignIoPinId pin, uint32_t port_index);

  void setInstancePinAccessGroup(DesignInstanceId instance, uint32_t group_index);
  [[nodiscard]] std::optional<uint32_t> instancePinAccessGroup(DesignInstanceId instance) const;
  [[nodiscard]] bool clearInstancePinAccessGroup(DesignInstanceId instance);

  void setPreferredInstancePinAccess(DesignInstancePinId pin, std::vector<DesignPreferredPinAccessRef> preferences);
  [[nodiscard]] std::span<const DesignPreferredPinAccessRef> preferredInstancePinAccess(DesignInstancePinId pin) const;
  [[nodiscard]] bool clearPreferredInstancePinAccess(DesignInstancePinId pin);

 private:
  DesignStore& _design;
};

}  // namespace eccdb
