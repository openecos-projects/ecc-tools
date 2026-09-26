// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>
#include <vector>

#include "DesignTestFixture.h"
#include "service/context/RoutingContextService.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, RoutingContextServiceReadsAndWritesNetRoutingContext)
{
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "signal"});
  RoutingContextService service(design);

  service.replaceGuides(net,
                        {{.box = {0, 0, 100, 100},
                          .layer = routing_layer,
                          .via_layer = routing_layer,
                          .flags = DesignRouteGuideFlag::kCongested | DesignRouteGuideFlag::kConnectedToTerm}});
  service.replaceTrackAssignments(net,
                                  {{.layer = routing_layer, .box = {0, 50, 100, 50}},
                                   {.layer = routing_layer, .box = {100, 50, 100, 50}}});

  EXPECT_TRUE(service.hasGuides(net));
  ASSERT_EQ(service.guides(net).size(), 1u);
  EXPECT_EQ(service.guides(net).front().box, (Rect{0, 0, 100, 100}));
  EXPECT_EQ(service.guides(net).front().flags,
            DesignRouteGuideFlag::kCongested | DesignRouteGuideFlag::kConnectedToTerm);
  EXPECT_TRUE(service.hasTrackAssignments(net));
  ASSERT_EQ(service.trackAssignments(net).size(), 2u);
  EXPECT_EQ(service.trackAssignments(net).front().box, (Rect{0, 50, 100, 50}));

  service.clearGuides(net);
  service.clearTrackAssignments(net);
  EXPECT_FALSE(service.hasGuides(net));
  EXPECT_FALSE(service.hasTrackAssignments(net));
  EXPECT_TRUE(service.guides(net).empty());
  EXPECT_TRUE(service.trackAssignments(net).empty());
}

TEST_F(DesignStorageTest, RoutingContextServiceManagesCandidateGroupsSelectionsAndPreferences)
{
  const auto master_port = library.masterPortStorage().createMasterPort(input_term);
  const auto instance = design.netlistStorage().createInstance(createInstance("u1", {100, 200}));
  const auto instance_pin = design.netlistStorage().findInstancePin(instance, input_term);
  const auto io_pin = design.netlistStorage().createIoPin(
      DesignIoPin{.name = "IO", .ports = {{.flags = DesignIoPinPortFlag::kExplicit}}});
  RoutingContextService service(design);

  const auto master_candidate = DesignPinAccessPoint{.point = {10, 20},
                                                       .layer = routing_layer,
                                                       .access_directions = DesignPinAccessDirection::kNorth
                                                                            | DesignPinAccessDirection::kUp};
  service.setMasterPortCandidateGroup(master_port, 7u, {master_candidate});
  EXPECT_TRUE(service.hasMasterPortCandidateGroup(master_port, 7u));
  ASSERT_EQ(service.masterPortCandidateGroups(master_port).size(), 1u);
  EXPECT_EQ(service.masterPortCandidateGroup(master_port, 7u).points.front().point, (Point{10, 20}));

  const auto io_candidate = DesignPinAccessPoint{.point = {500, 600}, .layer = routing_layer};
  service.setIoPinPortCandidateGroup(io_pin, 0u, 3u, {io_candidate});
  EXPECT_TRUE(service.hasIoPinPortCandidateGroup(io_pin, 0u, 3u));
  ASSERT_EQ(service.ioPinPortCandidateGroups(io_pin, 0u).size(), 1u);
  EXPECT_EQ(service.ioPinPortCandidateGroup(io_pin, 0u, 3u).points.front().point, (Point{500, 600}));

  EXPECT_THROW(service.setInstancePinAccessGroup(instance, 8u), std::invalid_argument);
  service.setInstancePinAccessGroup(instance, 7u);
  EXPECT_EQ(service.instancePinAccessGroup(instance), std::optional<uint32_t>{7u});
  service.setPreferredInstancePinAccess(
      instance_pin, {{.master_port = master_port, .group_index = 7u, .point_index = 0u}});
  ASSERT_EQ(service.preferredInstancePinAccess(instance_pin).size(), 1u);
  EXPECT_EQ(service.preferredInstancePinAccess(instance_pin).front().master_port, master_port);

  EXPECT_FALSE(service.clearMasterPortCandidateGroup(master_port, 7u));
  EXPECT_FALSE(service.clearInstancePinAccessGroup(instance));
  EXPECT_TRUE(service.clearPreferredInstancePinAccess(instance_pin));
  EXPECT_TRUE(service.clearInstancePinAccessGroup(instance));
  EXPECT_TRUE(service.clearMasterPortCandidateGroup(master_port, 7u));
  EXPECT_TRUE(service.clearIoPinPortCandidateGroups(io_pin, 0u));
  EXPECT_FALSE(service.hasMasterPortCandidateGroup(master_port, 7u));
  EXPECT_FALSE(service.hasIoPinPortCandidateGroup(io_pin, 0u, 3u));
}

}  // namespace
}  // namespace eccdb
