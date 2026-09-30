// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>
#include <vector>

#include "DesignTestFixture.h"
#include "design/guide/storage/DesignGuideStorage.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, StoresNetOwnedRouteGuidesAndTrackAssignments)
{
  DesignGuideStorage guide_storage(design.designRegistry(), tech);
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "signal"});

  const std::vector<DesignRouteGuide> expected_guides{
      {.box = {0, 0, 100, 100},
       .layer = routing_layer,
       .via_layer = routing_layer,
       .flags = DesignRouteGuideFlag::kCongested | DesignRouteGuideFlag::kJumper | DesignRouteGuideFlag::kConnectedToTerm},
      {.box = {100, 0, 200, 100}, .layer = routing_layer}};
  guide_storage.replaceGuides(net, expected_guides);

  EXPECT_TRUE(guide_storage.hasGuides(net));
  ASSERT_EQ(guide_storage.guides(net).size(), 2u);
  EXPECT_EQ(guide_storage.guides(net)[0].box, expected_guides[0].box);
  EXPECT_EQ(guide_storage.guides(net)[0].via_layer, routing_layer);
  EXPECT_EQ(guide_storage.guides(net)[0].flags, expected_guides[0].flags);

  std::vector<Rect> visited_guides;
  guide_storage.forEachGuide(net, [&](const DesignRouteGuide& guide) { visited_guides.push_back(guide.box); });
  EXPECT_EQ(visited_guides, (std::vector<Rect>{{0, 0, 100, 100}, {100, 0, 200, 100}}));

  const std::vector<DesignTrackAssignmentShape> expected_assignments{
      {.layer = routing_layer, .box = {0, 50, 200, 50}},
      {.layer = routing_layer, .box = {200, 50, 200, 50}},
  };
  guide_storage.replaceTrackAssignments(net, expected_assignments);

  EXPECT_TRUE(guide_storage.hasTrackAssignments(net));
  ASSERT_EQ(guide_storage.trackAssignments(net).size(), 2u);
  EXPECT_EQ(guide_storage.trackAssignments(net)[0].box, expected_assignments[0].box);
  EXPECT_EQ(guide_storage.trackAssignments(net)[1].box, expected_assignments[1].box);

  std::vector<Rect> visited_assignments;
  guide_storage.forEachTrackAssignment(net,
                                       [&](const DesignTrackAssignmentShape& assignment) { visited_assignments.push_back(assignment.box); });
  EXPECT_EQ(visited_assignments, (std::vector<Rect>{{0, 50, 200, 50}, {200, 50, 200, 50}}));

  guide_storage.clearGuides(net);
  guide_storage.clearTrackAssignments(net);
  EXPECT_FALSE(guide_storage.hasGuides(net));
  EXPECT_FALSE(guide_storage.hasTrackAssignments(net));
  EXPECT_TRUE(guide_storage.guides(net).empty());
  EXPECT_TRUE(guide_storage.trackAssignments(net).empty());
}

TEST_F(DesignStorageTest, ReplacesEmptyGuideAndTrackAssignmentSetsByRemovingComponents)
{
  DesignGuideStorage guide_storage(design.designRegistry(), tech);
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "signal"});

  guide_storage.replaceGuides(net, {{.box = {0, 0, 10, 10}, .layer = routing_layer}});
  guide_storage.replaceTrackAssignments(net, {{.layer = routing_layer, .box = {0, 0, 10, 0}}});
  guide_storage.replaceGuides(net, {});
  guide_storage.replaceTrackAssignments(net, {});

  EXPECT_FALSE(guide_storage.hasGuides(net));
  EXPECT_FALSE(guide_storage.hasTrackAssignments(net));
}

TEST_F(DesignStorageTest, AppendsTrackAssignmentsInWriterCallOrderWithoutDeduplication)
{
  DesignGuideStorage guide_storage(design.designRegistry(), tech);
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "signal"});
  const DesignTrackAssignmentShape first{.layer = routing_layer, .box = {0, 50, 100, 50}};
  const DesignTrackAssignmentShape second{.layer = routing_layer, .box = {100, 50, 200, 50}};

  guide_storage.appendTrackAssignments(net, {first, second});
  guide_storage.appendTrackAssignments(net, {first});
  guide_storage.appendTrackAssignments(net, {});

  ASSERT_TRUE(guide_storage.hasTrackAssignments(net));
  ASSERT_EQ(guide_storage.trackAssignments(net).size(), 3u);
  EXPECT_EQ(guide_storage.trackAssignments(net)[0].box, first.box);
  EXPECT_EQ(guide_storage.trackAssignments(net)[1].box, second.box);
  EXPECT_EQ(guide_storage.trackAssignments(net)[2].box, first.box);

  EXPECT_THROW(guide_storage.appendTrackAssignments(net, {{.layer = {}, .box = {0, 0, 10, 0}}}), std::invalid_argument);
  ASSERT_EQ(guide_storage.trackAssignments(net).size(), 3u);
  EXPECT_EQ(guide_storage.trackAssignments(net)[2].box, first.box);
}

TEST_F(DesignStorageTest, ValidatesNetGuideAndTrackAssignmentReferences)
{
  DesignGuideStorage guide_storage(design.designRegistry(), tech);
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "signal"});
  const auto non_routing_layer = tech.registry().create();
  tech.registry().emplace<TechLayerInfo>(non_routing_layer, TechLayerInfo{.name = "CUT"});
  const auto invalid_layer = TechRoutingLayerId{non_routing_layer};

  EXPECT_THROW(guide_storage.replaceGuides({}, {{.box = {0, 0, 10, 10}, .layer = routing_layer}}), std::out_of_range);
  EXPECT_THROW(guide_storage.replaceGuides(net, {{.box = {10, 0, 0, 10}, .layer = routing_layer}}), std::invalid_argument);
  EXPECT_THROW(guide_storage.replaceGuides(net, {{.box = {0, 0, 0, 10}, .layer = routing_layer}}), std::invalid_argument);
  EXPECT_THROW(guide_storage.replaceGuides(net, {{.box = {0, 0, 10, 10}, .layer = invalid_layer}}), std::invalid_argument);
  EXPECT_THROW(guide_storage.replaceGuides(net,
                                           {{.box = {0, 0, 10, 10}, .layer = routing_layer, .via_layer = invalid_layer}}),
               std::invalid_argument);
  EXPECT_THROW(guide_storage.replaceGuides(net,
                                           {{.box = {0, 0, 10, 10}, .layer = routing_layer, .flags = 1u << 31}}),
               std::invalid_argument);

  EXPECT_THROW(guide_storage.replaceTrackAssignments({}, {{.layer = routing_layer, .box = {0, 0, 10, 0}}}), std::out_of_range);
  EXPECT_THROW(guide_storage.replaceTrackAssignments(net, {{.layer = invalid_layer, .box = {0, 0, 10, 0}}}),
               std::invalid_argument);
  EXPECT_THROW(guide_storage.replaceTrackAssignments(net, {{.layer = routing_layer, .box = {10, 0, 0, 0}}}),
               std::invalid_argument);
}

TEST_F(DesignStorageTest, ValidatesGuideComponentsAfterDirectRegistryRestore)
{
  DesignGuideStorage guide_storage(design.designRegistry(), tech);
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "signal"});
  auto& registry = design.designRegistry().registry();
  registry.emplace<DesignNetRouteGuides>(net.entity(), DesignNetRouteGuides{});

  EXPECT_THROW(guide_storage.validateRestoredState(), std::runtime_error);
}

}  // namespace
}  // namespace eccdb
