// SPDX-License-Identifier: Apache-2.0
#include "service/context/RoutingContextService.h"

#include <stdexcept>
#include <utility>

#include "design/DesignStore.h"

namespace eccdb {

RoutingContextService::RoutingContextService(DesignStore& design) : _design(design)
{
  if (!design.rootId() || design.pinAccessStorage().rootId() != design.rootId()) {
    throw std::invalid_argument("routing context service has inconsistent design-root ownership");
  }
}

bool RoutingContextService::hasGuides(DesignNetId net) const
{
  return _design.guideStorage().hasGuides(net);
}

std::span<const DesignRouteGuide> RoutingContextService::guides(DesignNetId net) const
{
  return _design.guideStorage().guides(net);
}

void RoutingContextService::replaceGuides(DesignNetId net, std::vector<DesignRouteGuide> guides)
{
  _design.guideStorage().replaceGuides(net, std::move(guides));
}

void RoutingContextService::clearGuides(DesignNetId net)
{
  _design.guideStorage().clearGuides(net);
}

bool RoutingContextService::hasTrackAssignments(DesignNetId net) const
{
  return _design.guideStorage().hasTrackAssignments(net);
}

std::span<const DesignTrackAssignmentShape> RoutingContextService::trackAssignments(DesignNetId net) const
{
  return _design.guideStorage().trackAssignments(net);
}

void RoutingContextService::replaceTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> assignments)
{
  _design.guideStorage().replaceTrackAssignments(net, std::move(assignments));
}

void RoutingContextService::clearTrackAssignments(DesignNetId net)
{
  _design.guideStorage().clearTrackAssignments(net);
}

bool RoutingContextService::hasMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index) const
{
  return _design.pinAccessStorage().hasMasterPortCandidateGroup(master_port, group_index);
}

std::span<const DesignPinAccessCandidateGroup> RoutingContextService::masterPortCandidateGroups(
    LibraryMasterPortId master_port) const
{
  return _design.pinAccessStorage().masterPortCandidateGroups(master_port);
}

const DesignPinAccessCandidateGroup& RoutingContextService::masterPortCandidateGroup(LibraryMasterPortId master_port,
                                                                                        uint32_t group_index) const
{
  return _design.pinAccessStorage().masterPortCandidateGroup(master_port, group_index);
}

void RoutingContextService::setMasterPortCandidateGroup(LibraryMasterPortId master_port,
                                                         uint32_t group_index,
                                                         std::vector<DesignPinAccessPoint> points)
{
  _design.pinAccessStorage().setMasterPortCandidateGroup(master_port, group_index, std::move(points));
}

bool RoutingContextService::clearMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index)
{
  return _design.pinAccessStorage().clearMasterPortCandidateGroup(master_port, group_index);
}

bool RoutingContextService::clearMasterPortCandidateGroups(LibraryMasterPortId master_port)
{
  return _design.pinAccessStorage().clearMasterPortCandidateGroups(master_port);
}

bool RoutingContextService::hasIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index) const
{
  return _design.pinAccessStorage().hasIoPinPortCandidateGroup(pin, port_index, group_index);
}

std::span<const DesignPinAccessCandidateGroup> RoutingContextService::ioPinPortCandidateGroups(DesignIoPinId pin,
                                                                                                   uint32_t port_index) const
{
  return _design.pinAccessStorage().ioPinPortCandidateGroups(pin, port_index);
}

const DesignPinAccessCandidateGroup& RoutingContextService::ioPinPortCandidateGroup(DesignIoPinId pin,
                                                                                       uint32_t port_index,
                                                                                       uint32_t group_index) const
{
  return _design.pinAccessStorage().ioPinPortCandidateGroup(pin, port_index, group_index);
}

void RoutingContextService::setIoPinPortCandidateGroup(DesignIoPinId pin,
                                                        uint32_t port_index,
                                                        uint32_t group_index,
                                                        std::vector<DesignPinAccessPoint> points)
{
  _design.pinAccessStorage().setIoPinPortCandidateGroup(pin, port_index, group_index, std::move(points));
}

bool RoutingContextService::clearIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index)
{
  return _design.pinAccessStorage().clearIoPinPortCandidateGroup(pin, port_index, group_index);
}

bool RoutingContextService::clearIoPinPortCandidateGroups(DesignIoPinId pin, uint32_t port_index)
{
  return _design.pinAccessStorage().clearIoPinPortCandidateGroups(pin, port_index);
}

void RoutingContextService::setInstancePinAccessGroup(DesignInstanceId instance, uint32_t group_index)
{
  _design.pinAccessStorage().setInstancePinAccessGroup(instance, group_index);
}

std::optional<uint32_t> RoutingContextService::instancePinAccessGroup(DesignInstanceId instance) const
{
  return _design.pinAccessStorage().instancePinAccessGroup(instance);
}

bool RoutingContextService::clearInstancePinAccessGroup(DesignInstanceId instance)
{
  return _design.pinAccessStorage().clearInstancePinAccessGroup(instance);
}

void RoutingContextService::setPreferredInstancePinAccess(DesignInstancePinId pin,
                                                           std::vector<DesignPreferredPinAccessRef> preferences)
{
  _design.pinAccessStorage().setPreferredInstancePinAccess(pin, std::move(preferences));
}

std::span<const DesignPreferredPinAccessRef> RoutingContextService::preferredInstancePinAccess(DesignInstancePinId pin) const
{
  return _design.pinAccessStorage().preferredInstancePinAccess(pin);
}

bool RoutingContextService::clearPreferredInstancePinAccess(DesignInstancePinId pin)
{
  return _design.pinAccessStorage().clearPreferredInstancePinAccess(pin);
}

}  // namespace eccdb
