// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <stdexcept>
#include <vector>

#include "DesignTestFixture.h"
#include "service/routing/RoutingService.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, RoutingServiceReadsAndWritesNetRoutingContext)
{
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "signal"});
  DatabaseContext context{design};
  RoutingService service{context};

  service.replaceGuides(net,
                        {{.box = {0, 0, 100, 100},
                          .layer = routing_layer,
                          .via_layer = routing_layer,
                          .flags = DesignRouteGuideFlag::kCongested | DesignRouteGuideFlag::kConnectedToTerm}});
  service.replaceTrackAssignments(net,
                                  {{.layer = routing_layer, .box = {0, 50, 100, 50}},
                                   {.layer = routing_layer, .box = {100, 50, 100, 50}}});
  service.appendTrackAssignments(net, {{.layer = routing_layer, .box = {0, 50, 100, 50}}});

  EXPECT_TRUE(service.hasGuides(net));
  const auto guides = service.guides(net);
  ASSERT_EQ(guides.size(), 1u);
  const auto& guide = service.guide(guides.front());
  EXPECT_EQ(guide.box, (Rect{0, 0, 100, 100}));
  EXPECT_EQ(guide.flags,
            DesignRouteGuideFlag::kCongested | DesignRouteGuideFlag::kConnectedToTerm);
  EXPECT_TRUE(service.hasTrackAssignments(net));
  const auto assignments = service.trackAssignments(net);
  ASSERT_EQ(assignments.size(), 3u);
  EXPECT_EQ(service.trackAssignment(assignments.front()).box, (Rect{0, 50, 100, 50}));
  EXPECT_EQ(service.trackAssignment(assignments.back()).box, (Rect{0, 50, 100, 50}));

  service.clearGuides(net);
  service.clearTrackAssignments(net);
  EXPECT_FALSE(service.hasGuides(net));
  EXPECT_FALSE(service.hasTrackAssignments(net));
  EXPECT_TRUE(service.guides(net).empty());
  EXPECT_TRUE(service.trackAssignments(net).empty());
}

TEST_F(DesignStorageTest, RoutingServiceManagesCandidateGroupsSelectionsAndPreferences)
{
  const auto master_port = library.masterPortStorage().createMasterPort(input_term);
  const auto instance = design.netlistStorage().createInstance(createInstance("u1", {100, 200}));
  const auto instance_pin = design.netlistStorage().findInstancePin(instance, input_term);
  const auto io_pin = design.netlistStorage().createIoPin(
      DesignBlockTerm{.name = "IO"}, {{.flags = DesignIoPinPortFlag::kExplicit}});
  DatabaseContext context{design};
  RoutingService service{context};

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
  service.appendIoPinPortCandidatePoints(io_pin, 0u, 3u, {io_candidate});
  EXPECT_TRUE(service.hasIoPinPortCandidateGroup(io_pin, 0u, 3u));
  ASSERT_EQ(service.ioPinPortCandidateGroups(io_pin, 0u).size(), 1u);
  ASSERT_EQ(service.ioPinPortCandidateGroup(io_pin, 0u, 3u).points.size(), 2u);
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

TEST_F(DesignStorageTest, PinAccessStageReplacesCatalogSelectionAndPreferenceAsOneChange)
{
  const auto port = library.masterPortStorage().createMasterPort(input_term);
  const auto instance = design.netlistStorage().createInstance(createInstance("u1"));
  const auto pin = design.netlistStorage().findInstancePin(instance, input_term);
  DatabaseContext context{design};
  RoutingService service{context};
  service.setMasterPortCandidateGroup(port, 7u, {{.point = {10, 20}, .layer = routing_layer}});
  service.setInstancePinAccessGroup(instance, 7u);
  service.setPreferredInstancePinAccess(pin, {{.master_port = port, .group_index = 7u, .point_index = 0u}});
  const auto revision = context.revision();
  DesignPinAccessUpdate update;
  update.master_ports = {{.master_port = port,
                         .groups = {{.group_index = 9u, .points = {{.point = {30, 40}, .layer = routing_layer}}}}}};
  update.instances = {{.instance = instance, .group_index = 9u,
                      .preferences = {.entries = {{.pin = pin, .values = {{.master_port = port,
                                                           .group_index = 9u, .point_index = 0u}}}}}}};
  service.replacePinAccess(std::move(update));
  EXPECT_EQ(context.revision(), revision + 1u);
  EXPECT_FALSE(service.hasMasterPortCandidateGroup(port, 7u));
  EXPECT_EQ(service.instancePinAccessGroup(instance), std::optional<uint32_t>{9u});
  EXPECT_EQ(service.preferredInstancePinAccess(pin).front().group_index, 9u);
  EXPECT_EQ(service.masterPortCandidateGroup(port, 9u).points.front().point, (Point{30, 40}));
}

TEST_F(DesignStorageTest, PinAccessStageRejectsDanglingReferencesWithoutClearingExistingState)
{
  const auto port = library.masterPortStorage().createMasterPort(input_term);
  const auto instance = design.netlistStorage().createInstance(createInstance("u1"));
  const auto pin = design.netlistStorage().findInstancePin(instance, input_term);
  const auto term = design.netlistStorage().createBlockTerm(DesignBlockTerm{.name = "IO"});
  const auto block_pin = design.netlistStorage().createBlockPin(term, DesignBlockPinInput{});
  DatabaseContext context{design};
  RoutingService service{context};
  service.setMasterPortCandidateGroup(port, 7u, {{.point = {10, 20}, .layer = routing_layer}});
  service.setInstancePinAccessGroup(instance, 7u);
  service.setPreferredInstancePinAccess(pin, {{.master_port = port, .group_index = 7u, .point_index = 0u}});
  service.setBlockPinCandidateGroup(block_pin, 0u, {{.point = {0, 0}, .layer = routing_layer}});
  const auto revision = context.revision();
  DesignPinAccessUpdate invalid;
  invalid.master_ports = {{.master_port = port, .groups = {}}};
  invalid.block_pins = {{.pin = block_pin, .candidates = {}}};
  EXPECT_THROW(service.replacePinAccess(std::move(invalid)), std::invalid_argument);
  EXPECT_EQ(context.revision(), revision);
  EXPECT_TRUE(service.hasMasterPortCandidateGroup(port, 7u));
  EXPECT_EQ(service.instancePinAccessGroup(instance), std::optional<uint32_t>{7u});
  EXPECT_EQ(service.preferredInstancePinAccess(pin).size(), 1u);
  EXPECT_TRUE(service.hasBlockPinCandidateGroup(block_pin, 0u));

  DesignPinAccessUpdate cleared;
  cleared.master_ports = {{.master_port = port, .groups = {}}};
  cleared.instances = {{.instance = instance}};
  service.replacePinAccess(std::move(cleared));
  EXPECT_FALSE(service.hasMasterPortCandidateGroup(port, 7u));
  EXPECT_FALSE(service.instancePinAccessGroup(instance));
  EXPECT_TRUE(service.preferredInstancePinAccess(pin).empty());
  EXPECT_TRUE(service.hasBlockPinCandidateGroup(block_pin, 0u));
}

}  // namespace
}  // namespace eccdb
