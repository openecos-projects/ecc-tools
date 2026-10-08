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

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "service/routing/RoutingService.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

class RecordingObserver final : public DatabaseObserver
{
 public:
  struct Event
  {
    bool before = false;
    DatabaseChangeDomain domains = DatabaseChangeDomain::kNone;
    std::vector<DesignNetId> nets;
    std::vector<GeometryShapeId> shapes;
    std::vector<GeometryOwner> geometry_replacements;
  };

  void beforeChange(const DatabaseChange& change) noexcept override { record(true, change); }
  void afterChange(const DatabaseChange& change) noexcept override { record(false, change); }

  std::vector<Event> events;

 private:
  void record(bool before, const DatabaseChange& change) noexcept
  {
    Event event;
    event.before = before;
    event.domains = change.domains;
    event.nets.assign(change.nets.begin(), change.nets.end());
    event.shapes.assign(change.shapes.begin(), change.shapes.end());
    event.geometry_replacements.assign(change.geometry_replacements.begin(), change.geometry_replacements.end());
    events.push_back(std::move(event));
  }
};

class RoutingServiceTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    m1 = technology.createRoutingLayer(TechLayerInfo{.name = "M1"}, TechRoutingLayer{.width = 10});
    net = design.netlistStorage().createNet(DesignNet{.name = "signal"});
  }

  TechStore technology;
  LibraryStore library{technology.techRegistry()};
  DesignStore design{technology.techRegistry(), library.libraryRegistry()};
  DatabaseContext context{technology, library, design};
  TechRoutingLayerId m1;
  DesignNetId net;
};

TEST_F(RoutingServiceTest, RawShapeEditsKeepMetadataInSyncAndNotifyOnce)
{
  RoutingService service{context};
  RecordingObserver observer;
  context.addObserver(observer);

  const auto wire = service.createWire(DesignWire{.net = net});
  observer.events.clear();

  const auto shape = service.createShape(
      wire,
      DesignRouteShape{.layer = m1,
                       .flags = DesignWirePathFlag::kTaper,
                       .source_path = 7u,
                       .geometry = DesignRouteSegment{.begin = {.position = {10, 20}}, .end = {.position = {30, 20}}}});
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_TRUE(observer.events.front().before);
  EXPECT_FALSE(observer.events.back().before);
  EXPECT_TRUE(includes(observer.events.back().domains, DatabaseChangeDomain::kRouting));
  EXPECT_TRUE(includes(observer.events.back().domains, DatabaseChangeDomain::kGeometry));
  EXPECT_EQ(observer.events.back().nets, (std::vector<DesignNetId>{net}));
  EXPECT_EQ(observer.events.back().shapes, (std::vector<GeometryShapeId>{shape}));

  observer.events.clear();
  auto edited = service.shape(shape);
  edited.flags = 0u;
  edited.geometry = DesignRoutePatch{.anchor = {.position = {22, 22}}, .rectangle = {20, 20, 24, 24}};
  service.updateShape(shape, std::move(edited));

  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(service.shape(shape).source_path, 7u);
  EXPECT_TRUE(std::holds_alternative<DesignRoutePatch>(service.shape(shape).geometry));
  const auto& metadata = design.geometryStorage().registry().shape(shape);
  EXPECT_EQ(metadata.kind, GeometryShapeKind::kPatch);
  EXPECT_EQ(metadata.layer, m1);
  EXPECT_EQ(metadata.flags, 0u);
  EXPECT_EQ(metadata.source_path, 7u);
}

TEST_F(RoutingServiceTest, NativeWireAndShapeCrudPublishesOneChangePerEdit)
{
  RoutingService service{context};
  RecordingObserver observer;
  context.addObserver(observer);

  const auto wire = service.createWire(DesignWire{.net = net});
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_TRUE(observer.events.front().before);
  EXPECT_FALSE(observer.events.back().before);
  EXPECT_EQ(observer.events.back().nets, (std::vector<DesignNetId>{net}));

  observer.events.clear();
  const auto shape = service.createShape(
      wire,
      DesignRouteShape{.layer = m1,
                       .geometry = DesignRouteSegment{.begin = {.position = {0, 0}}, .end = {.position = {10, 0}}}});
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(observer.events.back().shapes, (std::vector<GeometryShapeId>{shape}));

  observer.events.clear();
  EXPECT_TRUE(service.destroyShape(shape));
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(observer.events.front().shapes, (std::vector<GeometryShapeId>{shape}));
  EXPECT_EQ(observer.events.back().shapes, (std::vector<GeometryShapeId>{shape}));

  observer.events.clear();
  EXPECT_TRUE(service.destroyWire(wire));
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(observer.events.back().nets, (std::vector<DesignNetId>{net}));
}

TEST_F(RoutingServiceTest, ReplacementOperationsInvalidateOnlyTheirSurvivingGeometryOwners)
{
  RoutingService service{context};
  RecordingObserver observer;
  context.addObserver(observer);

  const auto via = service.createVia(
      DesignViaInput{.name = "LOCAL_VIA", .rectangles = {{.layer = TechLayerId{m1.entity()}, .rectangle = {-2, -2, 2, 2}}}});
  observer.events.clear();
  service.updateVia(
      via,
      DesignViaInput{.name = "LOCAL_VIA", .rectangles = {{.layer = TechLayerId{m1.entity()}, .rectangle = {-3, -2, 3, 2}}}});
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(observer.events.back().geometry_replacements, (std::vector<GeometryOwner>{designViaOwner(via)}));

  observer.events.clear();
  service.replaceGuides(net, {{.box = {0, 0, 10, 10}, .layer = m1}});
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(observer.events.back().geometry_replacements,
            (std::vector<GeometryOwner>{designTrackAssignmentOwner(net), designGuideOwner(net)}));

  observer.events.clear();
  service.clearGuides(net);
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(observer.events.back().geometry_replacements,
            (std::vector<GeometryOwner>{designTrackAssignmentOwner(net), designGuideOwner(net)}));

  observer.events.clear();
  service.replaceTrackAssignments(net, {{.layer = m1, .box = {0, 10, 10, 10}}});
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(observer.events.back().geometry_replacements,
            (std::vector<GeometryOwner>{designTrackAssignmentOwner(net)}));

  observer.events.clear();
  service.clearTrackAssignments(net);
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(observer.events.back().geometry_replacements,
            (std::vector<GeometryOwner>{designTrackAssignmentOwner(net)}));
}

TEST_F(RoutingServiceTest, RoutedReplacementRequiresTechnologyButSnapshotDoesNot)
{
  DatabaseContext design_only_context{design};
  RoutingService design_only{design_only_context};
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  static_cast<void>(design.routingStorage().createShape(
      wire,
      DesignRouteShape{.layer = m1,
                       .geometry = DesignRouteSegment{.begin = {.position = {0, 0}}, .end = {.position = {10, 0}}}}));

  ASSERT_EQ(design_only.snapshot(net).wires.size(), 1u);
  RecordingObserver observer;
  design_only_context.addObserver(observer);
  const std::vector<RouteShape> replacement{RouteSegment{.layer = m1, .begin = {10, 0}, .end = {20, 0}}};
  EXPECT_THROW((void) design_only.replaceRouted(net, replacement), std::logic_error);
  EXPECT_TRUE(observer.events.empty());
  EXPECT_TRUE(design.routingStorage().contains(wire));
}

TEST_F(RoutingServiceTest, FailedRoutedReplacementKeepsPublishedRouteAndEmitsNoAfterChange)
{
  RoutingService service{context};
  const auto fixed = service.createWire(DesignWire{.net = net, .status = DesignWireStatus::kFixed});
  const auto routed = service.createWire(DesignWire{.net = net});
  static_cast<void>(service.createShape(
      fixed,
      DesignRouteShape{.layer = m1,
                       .geometry = DesignRouteSegment{.begin = {.position = {0, 10}}, .end = {.position = {20, 10}}}}));
  const auto old_shape = service.createShape(
      routed,
      DesignRouteShape{.layer = m1,
                       .geometry = DesignRouteSegment{.begin = {.position = {0, 0}}, .end = {.position = {20, 0}}}});

  RecordingObserver observer;
  context.addObserver(observer);
  const std::vector<RouteShape> replacement{
      RouteSegment{.layer = m1, .begin = {20, 0}, .end = {40, 0}},
      RoutePatch{.layer = m1, .rectangle = {4, 3, 3, 8}}};
  EXPECT_THROW((void) service.replaceRouted(net, replacement), std::invalid_argument);

  ASSERT_EQ(observer.events.size(), 1u);
  EXPECT_TRUE(observer.events.front().before);
  EXPECT_TRUE(service.contains(fixed));
  EXPECT_TRUE(service.contains(routed));
  ASSERT_EQ(service.wire(routed).shapes.size(), 1u);
  EXPECT_EQ(service.wire(routed).shapes.front(), old_shape);
  EXPECT_TRUE(service.contains(old_shape));
}

TEST_F(RoutingServiceTest, ReplacesOnlyRoutedWiresAndPublishesOneLogicalChange)
{
  RoutingService service{context};
  const auto fixed = service.createWire(DesignWire{.net = net, .status = DesignWireStatus::kFixed});
  static_cast<void>(service.createShape(
      fixed,
      DesignRouteShape{.layer = m1,
                       .geometry = DesignRouteSegment{.begin = {.position = {0, 10}}, .end = {.position = {20, 10}}}}));
  const auto routed = service.createWire(DesignWire{.net = net, .status = DesignWireStatus::kRouted});
  static_cast<void>(service.createShape(
      routed,
      DesignRouteShape{.layer = m1,
                       .geometry = DesignRouteSegment{.begin = {.position = {0, 0}}, .end = {.position = {20, 0}}}}));

  RecordingObserver observer;
  context.addObserver(observer);
  const std::vector<RouteShape> replacement{RouteSegment{.layer = m1, .begin = {20, 0}, .end = {80, 0}}};
  const auto replacement_wire = service.replaceRouted(net, replacement);

  ASSERT_TRUE(replacement_wire);
  EXPECT_FALSE(service.contains(routed));
  EXPECT_TRUE(service.contains(fixed));
  EXPECT_EQ(service.wire(fixed).status, DesignWireStatus::kFixed);
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_TRUE(observer.events.front().before);
  EXPECT_FALSE(observer.events.back().before);
  EXPECT_EQ(observer.events.back().nets, (std::vector<DesignNetId>{net}));

  const auto snapshot = service.snapshot(net);
  ASSERT_EQ(snapshot.wires.size(), 2u);
  EXPECT_EQ(snapshot.wires[0].wire, fixed);
  EXPECT_EQ(snapshot.wires[0].status, DesignWireStatus::kFixed);
  EXPECT_EQ(snapshot.wires[1].wire, replacement_wire);
  EXPECT_EQ(snapshot.wires[1].status, DesignWireStatus::kRouted);
}

TEST_F(RoutingServiceTest, GuideStageReplacesDependentAssignmentTogetherAndPreservesOnFailure)
{
  RoutingService service{context};
  service.replaceGuideStage(net, std::vector<DesignRouteGuide>{{.box = {0, 0, 20, 20}, .layer = m1}},
                           {{.layer = m1, .box = {0, 10, 20, 10}}});
  const auto old_guide = service.guides(net).front();
  const auto old_assignment = service.trackAssignments(net).front();
  const auto revision = context.revision();
  EXPECT_THROW(service.replaceGuideStage(net,
                                         std::vector<DesignRouteGuide>{{.box = {30, 30, 50, 50}, .layer = m1}},
                                         {{.layer = m1, .box = {10, 0, 0, 0}}}), std::invalid_argument);
  EXPECT_EQ(context.revision(), revision);
  EXPECT_EQ(service.guides(net).front(), old_guide);
  EXPECT_EQ(service.trackAssignments(net).front(), old_assignment);

  RecordingObserver observer;
  context.addObserver(observer);
  service.replaceGuideStage(net, std::vector<DesignRouteGuide>{{.box = {30, 30, 50, 50}, .layer = m1}},
                           {{.layer = m1, .box = {30, 40, 50, 40}}});
  EXPECT_EQ(context.revision(), revision + 1u);
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(service.guide(service.guides(net).front()).box, (Rect{30, 30, 50, 50}));
  EXPECT_EQ(service.trackAssignment(service.trackAssignments(net).front()).box, (Rect{30, 40, 50, 40}));
  const auto preserved_guide = service.guides(net).front();
  service.replaceGuideStage(net, std::nullopt, {{.layer = m1, .box = {40, 40, 50, 40}}});
  EXPECT_EQ(service.guides(net).front(), preserved_guide);
  service.replaceGuides(net, {{.box = {0, 0, 20, 20}, .layer = m1}});
  EXPECT_FALSE(service.hasTrackAssignments(net));
}

TEST_F(RoutingServiceTest, MultiNetResultsRetireOnlyExplicitRoutedOutputsAndNotifyOnce)
{
  RoutingService service{context};
  const auto clear_net = design.netlistStorage().createNet(DesignNet{.name = "clear"});
  const auto skipped_net = design.netlistStorage().createNet(DesignNet{.name = "skipped"});
  const auto failed_net = design.netlistStorage().createNet(DesignNet{.name = "failed"});
  const auto untouched_net = design.netlistStorage().createNet(DesignNet{.name = "untouched"});
  const auto special_net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto routed = service.createWire(DesignWire{.net = net});
  const auto fixed = service.createWire(DesignWire{.net = net, .status = DesignWireStatus::kFixed});
  const auto cover = service.createWire(DesignWire{.net = net, .status = DesignWireStatus::kCover});
  const auto cleared = service.createWire(DesignWire{.net = clear_net});
  const auto skipped = service.createWire(DesignWire{.net = skipped_net});
  const auto failed = service.createWire(DesignWire{.net = failed_net});
  const auto untouched = service.createWire(DesignWire{.net = untouched_net});
  const auto special = service.createWire(DesignWire{.net = special_net});
  const std::vector<DesignRouteShape> shapes{{.layer = m1, .flags = DesignWirePathFlag::kHasMask, .source_path = 12u,
       .geometry = DesignRouteSegment{.begin = {.position = {0, 0}}, .end = {.position = {20, 0}}}}};
  const std::vector<std::optional<DesignRouteShapeAttributes>> attributes{DesignRouteShapeAttributes{.mask = 2u}};
  const std::vector<RoutedNetResult> results{
      {.net = net, .outcome = RoutedResultOutcome::kReplace, .shapes = shapes, .attributes = attributes},
      {.net = clear_net, .outcome = RoutedResultOutcome::kClear},
      {.net = skipped_net, .outcome = RoutedResultOutcome::kSkipped},
      {.net = failed_net, .outcome = RoutedResultOutcome::kFailed}};
  RecordingObserver observer;
  context.addObserver(observer);
  const auto revision = context.revision();
  const auto replacements = service.commitRoutedResults(results);
  ASSERT_EQ(replacements.size(), results.size());
  EXPECT_TRUE(replacements.front());
  EXPECT_FALSE(replacements[1]);
  EXPECT_FALSE(service.contains(routed));
  EXPECT_FALSE(service.contains(cleared));
  for (const auto wire : {fixed, cover, skipped, failed, untouched, special}) EXPECT_TRUE(service.contains(wire));
  EXPECT_EQ(service.shape(service.wire(replacements.front()).shapes.front()).source_path, 12u);
  ASSERT_NE(service.shapeAttributes(service.wire(replacements.front()).shapes.front()), nullptr);
  EXPECT_EQ(service.shapeAttributes(service.wire(replacements.front()).shapes.front())->mask, 2u);
  EXPECT_EQ(context.revision(), revision + 1u);
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(observer.events.back().nets, (std::vector<DesignNetId>{net, clear_net}));
}

TEST_F(RoutingServiceTest, MultiNetStageFailureLeavesEveryOldRouteAndNoCommittedChange)
{
  RoutingService service{context};
  const auto other_net = design.netlistStorage().createNet(DesignNet{.name = "other"});
  const auto old_first = service.createWire(DesignWire{.net = net});
  const auto old_second = service.createWire(DesignWire{.net = other_net});
  const std::vector<DesignRouteShape> valid{{.layer = m1,
      .geometry = DesignRouteSegment{.begin = {.position = {0, 0}}, .end = {.position = {20, 0}}}}};
  const std::vector<DesignRouteShape> invalid{{.layer = m1,
      .geometry = DesignRoutePatch{.anchor = {.position = {10, 0}}, .rectangle = {10, 0, 0, 10}}}};
  const std::vector<RoutedNetResult> results{
      {.net = net, .outcome = RoutedResultOutcome::kReplace, .shapes = valid},
      {.net = other_net, .outcome = RoutedResultOutcome::kReplace, .shapes = invalid}};
  RecordingObserver observer;
  context.addObserver(observer);
  const auto revision = context.revision();
  EXPECT_THROW((void)service.commitRoutedResults(results), std::invalid_argument);
  EXPECT_TRUE(observer.events.empty());
  EXPECT_EQ(context.revision(), revision);
  ASSERT_EQ(design.routingStorage().wireIds(net).size(), 1u);
  ASSERT_EQ(design.routingStorage().wireIds(other_net).size(), 1u);
  EXPECT_EQ(design.routingStorage().wireIds(net).front(), old_first);
  EXPECT_EQ(design.routingStorage().wireIds(other_net).front(), old_second);
  EXPECT_THROW((void)service.commitRoutedResults(std::vector<RoutedNetResult>{
      {.net = net, .outcome = RoutedResultOutcome::kReplace}}), std::invalid_argument);
  EXPECT_TRUE(service.contains(old_first));
}

}  // namespace
}  // namespace eccdb
