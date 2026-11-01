// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>
#include <array>

#include "DesignTestFixture.h"
#include "context/DatabaseContext.h"
#include "geometry/GeometryEditService.h"
#include "geometry/GeometryQueryService.h"
#include "connectivity/ConnectivityService.h"
#include "instance/InstanceService.h"
#include "constraint/ConstraintService.h"
#include "floorplan/FloorplanService.h"
#include "routing/RoutingService.h"

namespace eccdb {
namespace {

struct CountingObserver final : DatabaseObserver
{
  void afterChange(const DatabaseChange&) noexcept override { ++after_count; }
  uint32_t after_count = 0;
};

TEST_F(DesignStorageTest, SessionDeleteRestoresIdentityOwnerOrderAndDefAttributes)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "N"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto create = [&](int32_t x) {
    return design.geometryStorage().createRouteShape(wire,
        DesignRouteShape{.layer = routing_layer, .flags = DesignWirePathFlag::kHasWidth,
                        .geometry = DesignRouteSegment{.begin = {.position = {x, 0}}, .end = {.position = {x + 10, 0}}}},
        DesignRouteShapeAttributes{.width = 6});
  };
  const auto a = create(0), b = create(20), c = create(40);
  DatabaseContext context{design};
  GeometryEditService edit(context);
  GeometryQueryService query(context);
  query.warmupIndex();
  EXPECT_TRUE(edit.removeShape(b.packed()));
  EXPECT_FALSE(query.contains(b.packed()));
  EXPECT_TRUE(design.geometryRegistry().registry().valid(b.entity()));
  EXPECT_EQ(design.routingStorage().wire(wire).shapes, (std::vector<GeometryShapeId>{a,c}));
  EXPECT_TRUE(query.indexReady());
  EXPECT_EQ(query.indexedShapeCount(), 2u);
  EXPECT_TRUE(edit.restoreShape(b.packed()));
  EXPECT_TRUE(query.contains(b.packed()));
  EXPECT_EQ(design.routingStorage().wire(wire).shapes, (std::vector<GeometryShapeId>{a,b,c}));
  ASSERT_NE(design.routingStorage().shapeAttributes(b), nullptr);
  EXPECT_EQ(design.routingStorage().shapeAttributes(b)->width, 6);
  EXPECT_TRUE(query.indexReady());
  EXPECT_EQ(query.indexedShapeCount(), 3u);
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
  EXPECT_TRUE(edit.removeShape(b.packed()));
  EXPECT_TRUE(edit.restoreShape(b.packed()));
}

TEST_F(DesignStorageTest, SessionMoveMaintainsWarmIndexAndUntouchedOverviewTiles)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto id = design.geometryStorage().appendNetShape(net,
      DesignNetRectangle{.layer = routing_layer, .rectangle = {10,10,20,20}});
  DatabaseContext context{design};
  GeometryEditService edit(context);
  GeometryQueryService query(context);
  query.warmupIndex();
  static_cast<void>(design.geometryOverview().tile({0,0,0}));
  static_cast<void>(design.geometryOverview().tile({0,3,3}));
  EXPECT_EQ(design.geometryOverview().cachedTileCount(), 2u);
  edit.moveShape(id.packed(), 100, 0);
  EXPECT_TRUE(query.indexReady());
  EXPECT_EQ(design.geometryOverview().cachedTileCount(), 1u);
  EXPECT_TRUE(query.queryRefs({0,0,30,30}).records.empty());
  ASSERT_EQ(query.queryRefs({100,0,130,30}).records.size(), 1u);
  EXPECT_EQ(query.queryRefs({100,0,130,30}).records.front().id, id.packed());
}

TEST_F(DesignStorageTest, SidecarOverviewSeedRequiresCurrentEpochAndSurvivesEditMaintenance)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto shape = design.geometryStorage().appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {10, 10, 20, 20}});
  DatabaseContext context{design};
  GeometryQueryService query(context);
  const auto snapshot = query.queryOverview({0, 0, 128, 128});
  ASSERT_FALSE(snapshot.tiles.empty());

  // Simulate a sidecar hit without first asking the native overview to build
  // its own tiles.  Metadata seeding must be accepted only for this epoch.
  design.geometryOverview().invalidate();
  EXPECT_TRUE(query.seedOverview(snapshot));
  EXPECT_EQ(design.geometryOverview().cachedTileCount(), snapshot.tiles.size());
  auto stale = snapshot;
  ++stale.geometry_epoch;
  EXPECT_FALSE(query.seedOverview(stale));

  GeometryEditService edit(context);
  edit.moveShape(shape.packed(), 5'000, 0);
  // The edit path keeps unrelated seeded tiles and invalidates only the
  // affected tile; a subsequent query therefore does not rebuild the whole
  // overview from the canonical shape registry.
  const auto old_tile = query.queryOverview({0, 0, 128, 128});
  ASSERT_EQ(old_tile.tiles.size(), 1u);
  EXPECT_EQ(old_tile.tiles.front().shape_count, 0u);
  const auto new_tile = query.queryOverview({5'000, 0, 5'128, 128});
  ASSERT_EQ(new_tile.tiles.size(), 1u);
  EXPECT_EQ(new_tile.tiles.front().shape_count, 1u);
}

TEST_F(DesignStorageTest, ExactHitRejectsPolygonBoundingBoxEmptySpace)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto id = design.geometryStorage().appendNetShape(net,
      DesignNetPolygon{.layer = routing_layer, .points = {{0,0},{100,0},{0,100}}});
  DatabaseContext context{design};
  GeometryQueryService query(context);
  EXPECT_EQ(query.hitTest({10,10}), id.packed());
  EXPECT_EQ(query.hitTest({90,90}), std::nullopt);
  EXPECT_EQ(query.hitTest({50,51}, 1), id.packed());
  EXPECT_TRUE(query.intersectShapes({80,80,90,90}).empty());
  EXPECT_EQ(query.intersectShapes({40,40,60,60}), (std::vector<uint64_t>{id.packed()}));
  GeometryQueryFilter visible;
  visible.layers = {UINT64_MAX};
  EXPECT_TRUE(query.intersectShapes({0,0,100,100}, visible).empty());
  visible.layers = {routing_layer.packed()};
  EXPECT_EQ(query.hitTest({10,10}, 0, visible), id.packed());
  visible.kind_owner_masks = {{1u << static_cast<uint32_t>(GeometryShapeKind::kPolygon),
                              1u << static_cast<uint32_t>(GeometryOwnerDomain::kDesignBlockPin)}};
  EXPECT_FALSE(query.hitTest({10,10}, 0, visible));
  visible.kind_owner_masks.push_back({1u << static_cast<uint32_t>(GeometryShapeKind::kPolygon),
                                     1u << static_cast<uint32_t>(GeometryOwnerDomain::kDesignNet)});
  EXPECT_EQ(query.hitTest({10,10}, 0, visible), id.packed());
}

TEST_F(DesignStorageTest, SessionJournalReleasesOnlyStillRemovedEntities)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto a = design.geometryStorage().appendNetShape(net,
      DesignNetRectangle{.layer = routing_layer, .rectangle = {0,0,10,10}});
  const auto b = design.geometryStorage().appendNetShape(net,
      DesignNetRectangle{.layer = routing_layer, .rectangle = {20,0,30,10}});
  DatabaseContext context{design};
  {
    GeometryEditService edit(context);
    EXPECT_TRUE(edit.removeShape(a.packed()));
    EXPECT_TRUE(edit.removeShape(b.packed()));
    EXPECT_TRUE(edit.restoreShape(b.packed()));
  }
  EXPECT_FALSE(design.geometryRegistry().registry().valid(a.entity()));
  EXPECT_TRUE(design.geometryStorage().contains(b));
}

TEST_F(DesignStorageTest, ExactSegmentHitUsesResolvedCapsAndCanonicalOddWidth)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "N"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto make = [&](Point a, Point b, int32_t width, std::string shape = {}) {
    const auto flags = DesignWirePathFlag::kHasWidth | (shape.empty() ? 0u : DesignWirePathFlag::kHasShape);
    return design.geometryStorage().createRouteShape(wire,
        DesignRouteShape{.layer = routing_layer,
                        .flags = flags,
                        .geometry = DesignRouteSegment{.begin = {.position = a}, .end = {.position = b}}},
        DesignRouteShapeAttributes{.width = width, .shape = std::move(shape)});
  };
  const auto diagonal = make({0,0}, {10,10}, 10);
  const auto truncated = make({100,0}, {110,0}, 10, "STRIPE");
  const auto patch = make({200,0}, {200,0}, 5);
  DatabaseContext context{design};
  GeometryQueryService query(context);
  EXPECT_EQ(query.hitTest({-6,0}), diagonal.packed());
  EXPECT_EQ(query.hitTest({96,0}), std::nullopt);
  EXPECT_EQ(query.hitTest({100,0}), truncated.packed());
  EXPECT_EQ(query.hitTest({203,3}), patch.packed());
  EXPECT_EQ(query.hitTest({197,0}), std::nullopt);
}

TEST_F(DesignStorageTest, AdjacentBatchDeletionRestoresOrderAndPrunesEmptyNetRelation)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto a = design.geometryStorage().appendNetShape(net,
      DesignNetRectangle{.layer = routing_layer, .rectangle = {0,0,10,10}});
  const auto b = design.geometryStorage().appendNetShape(net,
      DesignNetRectangle{.layer = routing_layer, .rectangle = {20,0,30,10}});
  const std::array<uint64_t,2> ids{a.packed(), b.packed()};
  DatabaseContext context{design};
  GeometryEditService edit(context);
  EXPECT_TRUE(edit.removeShapes(ids));
  EXPECT_EQ(design.routingStorage().netGeometry(net), nullptr);
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
  const std::array<uint64_t,2> reversed_ids{b.packed(), a.packed()};
  EXPECT_TRUE(edit.restoreShapes(reversed_ids));
  ASSERT_NE(design.routingStorage().netGeometry(net), nullptr);
  EXPECT_EQ(design.routingStorage().netGeometry(net)->shapes, (std::vector<GeometryShapeId>{a,b}));
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
  const std::array<uint64_t,2> duplicate_ids{a.packed(), a.packed()};
  EXPECT_FALSE(edit.removeShapes(duplicate_ids));
  EXPECT_EQ(design.routingStorage().netGeometry(net)->shapes, (std::vector<GeometryShapeId>{a,b}));
}

TEST_F(DesignStorageTest, IndividualAndSubsetRestoresKeepOwnerOrder)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto create = [&](int32_t x) {
    return design.geometryStorage().appendNetShape(net,
        DesignNetRectangle{.layer = routing_layer, .rectangle = {x,0,x+10,10}});
  };
  const auto a = create(0), b = create(20), c = create(40);
  DatabaseContext context{design};
  GeometryEditService edit(context);
  GeometryQueryService query(context);
  query.warmupIndex();
  for (const auto order : {std::array<uint64_t,2>{a.packed(), b.packed()},
                           std::array<uint64_t,2>{b.packed(), a.packed()}}) {
    EXPECT_TRUE(edit.removeShapes(order));
    EXPECT_TRUE(edit.restoreShape(a.packed()));
    EXPECT_EQ(design.routingStorage().netGeometry(net)->shapes, (std::vector<GeometryShapeId>{a,c}));
    EXPECT_TRUE(edit.restoreShapes(std::array<uint64_t,1>{b.packed()}));
    EXPECT_EQ(design.routingStorage().netGeometry(net)->shapes, (std::vector<GeometryShapeId>{a,b,c}));
    EXPECT_TRUE(edit.removeShapes(order));
    EXPECT_TRUE(edit.restoreShape(b.packed()));
    EXPECT_EQ(design.routingStorage().netGeometry(net)->shapes, (std::vector<GeometryShapeId>{b,c}));
    EXPECT_TRUE(edit.restoreShape(a.packed()));
    EXPECT_EQ(design.routingStorage().netGeometry(net)->shapes, (std::vector<GeometryShapeId>{a,b,c}));
  }
  EXPECT_TRUE(query.indexReady());
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
}

TEST_F(DesignStorageTest, RestoringDetachedNetGeometryUsesGeometricCapacityAndKeepsStableIds)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  std::vector<GeometryShapeId> original;
  original.reserve(17);
  for (int32_t index = 0; index < 17; ++index) {
    original.push_back(design.geometryStorage().appendNetShape(
        net, DesignNetRectangle{.layer = routing_layer, .rectangle = {index * 20, 0, index * 20 + 10, 10}}));
  }

  DatabaseContext context(design);
  GeometryEditService edit(context);
  std::vector<uint64_t> ids;
  ids.reserve(original.size());
  for (const auto shape : original) ids.push_back(shape.packed());
  ASSERT_TRUE(edit.removeShapes(ids));
  EXPECT_EQ(design.routingStorage().netGeometry(net), nullptr);

  for (const auto shape : original) {
    const auto* before_relation = design.routingStorage().netGeometry(net);
    const auto old_size = before_relation == nullptr ? 0u : before_relation->shapes.size();
    const auto old_capacity = before_relation == nullptr ? 0u : before_relation->shapes.capacity();
    ASSERT_TRUE(edit.restoreShape(shape.packed()));
    const auto* relation = design.routingStorage().netGeometry(net);
    ASSERT_NE(relation, nullptr);
    ASSERT_EQ(relation->shapes.size(), old_size + 1u);
    if (old_size == old_capacity) {
      EXPECT_GE(relation->shapes.capacity(), old_capacity == 0u ? 1u : old_capacity * 2u);
    } else {
      EXPECT_EQ(relation->shapes.capacity(), old_capacity);
    }
    EXPECT_TRUE(design.geometryStorage().contains(shape));
  }

  ASSERT_NE(design.routingStorage().netGeometry(net), nullptr);
  EXPECT_EQ(design.routingStorage().netGeometry(net)->shapes, original);
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
}

TEST_F(DesignStorageTest, PermanentRemovalClosesJournalOrdinalAndReleasesIndexRestoreSlot)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto create = [&](int32_t x) {
    return design.geometryStorage().appendNetShape(net,
        DesignNetRectangle{.layer = routing_layer, .rectangle = {x,0,x+10,10}});
  };
  const auto a = create(0), b = create(20), c = create(40);
  DatabaseContext context{design};
  GeometryEditService edit(context);
  GeometryQueryService query(context);
  query.warmupIndex();
  EXPECT_TRUE(edit.removeShape(b.packed()));
  EXPECT_TRUE(edit.destroyShape(a.packed()));
  EXPECT_TRUE(edit.restoreShape(b.packed()));
  EXPECT_EQ(design.routingStorage().netGeometry(net)->shapes, (std::vector<GeometryShapeId>{b,c}));
  EXPECT_EQ(design.geometryIndex().pendingRestoreSlotCount(), 0u);
  EXPECT_TRUE(query.indexReady());
}

TEST_F(DesignStorageTest, ServiceParentDeletionReleasesDetachedJournalSlots)
{
  auto& netlist = design.netlistStorage();
  const auto net = netlist.createNet(DesignNet{.name = "signal"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto shape = design.geometryStorage().createRouteShape(wire,
      DesignRouteShape{.layer = routing_layer,
                      .geometry = DesignRouteSegment{.begin = {.position = {0, 0}},
                                                     .end = {.position = {100, 0}}}});
  DatabaseContext context(design);
  GeometryEditService edit(context);
  GeometryQueryService query(context);
  ConnectivityService connectivity(context);
  query.warmupIndex();
  ASSERT_TRUE(edit.removeShape(shape.packed()));
  ASSERT_TRUE(design.geometryRegistry().registry().valid(shape.entity()));
  ASSERT_TRUE(connectivity.deleteNet(net));
  EXPECT_FALSE(design.routingStorage().contains(wire));
  EXPECT_FALSE(design.geometryRegistry().registry().valid(shape.entity()));
  EXPECT_FALSE(edit.restoreShape(shape.packed()));
  EXPECT_EQ(design.geometryIndex().pendingRestoreSlotCount(), 0u);
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
}

TEST_F(DesignStorageTest, BulkEditNotifiesOnceAndFailedBatchKeepsRevision)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto a = design.geometryStorage().appendNetShape(net,
      DesignNetRectangle{.layer = routing_layer, .rectangle = {0, 0, 10, 10}});
  const auto b = design.geometryStorage().appendNetShape(net,
      DesignNetRectangle{.layer = routing_layer, .rectangle = {20, 0, 30, 10}});
  DatabaseContext context(design);
  GeometryEditService edit(context);
  const auto start_revision = context.revision();
  ASSERT_TRUE(edit.removeShapes(std::array{a.packed(), b.packed()}));
  EXPECT_EQ(context.revision(), start_revision + 1);
  ASSERT_TRUE(edit.restoreShapes(std::array{b.packed(), a.packed()}));
  EXPECT_EQ(context.revision(), start_revision + 2);
  EXPECT_FALSE(edit.removeShapes(std::array{a.packed(), a.packed()}));
  EXPECT_EQ(context.revision(), start_revision + 2);
  EXPECT_EQ(design.routingStorage().netGeometry(net)->shapes, (std::vector<GeometryShapeId>{a, b}));
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
}

TEST_F(DesignStorageTest, InstanceRegionReplacementDiscardsOldDetachedGeometry)
{
  DatabaseContext context(design);
  InstanceService instances(context);
  GeometryEditService edit(context);
  const std::array regions{Rect{0, 0, 10, 10}, Rect{20, 0, 30, 10}};
  auto value = createInstance("u1");
  value.flags |= DesignInstanceFlag::kHasRegionBounds;
  const auto instance = instances.create(std::move(value), regions);
  const auto old = design.netlistStorage().instance(instance).region_bounds.front();
  ASSERT_TRUE(edit.removeShape(old.packed()));
  const std::array replacement{Rect{100, 100, 120, 120}};
  instances.update(instance, design.netlistStorage().instance(instance), replacement);
  EXPECT_FALSE(edit.restoreShape(old.packed()));
  EXPECT_FALSE(design.geometryRegistry().registry().valid(old.entity()));
  ASSERT_EQ(design.netlistStorage().instance(instance).region_bounds.size(), 1u);
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
}

TEST_F(DesignStorageTest, RegionReplacementDiscardsOldJournalUnderSurvivingOwner)
{
  DatabaseContext context(design);
  ConstraintService constraints(context);
  GeometryEditService edit(context);
  const auto region = constraints.createRegion(
      DesignRegionInput{.name = "R", .rectangles = {{0, 0, 10, 10}, {20, 0, 30, 10}}});
  const auto old = design.constraintStorage().region(region).rectangles.front();
  ASSERT_TRUE(edit.removeShape(old.packed()));
  constraints.updateRegion(region, DesignRegionInput{.name = "R", .rectangles = {{100, 100, 120, 120}}});
  EXPECT_FALSE(edit.restoreShape(old.packed()));
  EXPECT_FALSE(design.geometryRegistry().registry().valid(old.entity()));
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
}

TEST_F(DesignStorageTest, BlockPinPlacementDiscardsDetachedGeometryAtPreviousPlacement)
{
  DatabaseContext context(design);
  FloorplanService floorplan(context);
  GeometryEditService edit(context);
  const auto term = design.netlistStorage().createBlockTerm(
      DesignBlockTerm{.name = "IN"},
      {DesignBlockPinInput{.rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}},
                                        {.layer = layer, .rectangle = {20, 0, 30, 10}}}}});
  const auto pin = design.netlistStorage().blockPins(term).front();
  const auto old = design.netlistStorage().blockPin(pin).rectangles.front();
  ASSERT_TRUE(edit.removeShape(old.packed()));
  floorplan.placeBlockPin(pin, {100, 200}, DesignOrientation::kN, DesignPlacementStatus::kPlaced);
  EXPECT_FALSE(edit.restoreShape(old.packed()));
  EXPECT_FALSE(design.geometryRegistry().registry().valid(old.entity()));
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
}

TEST_F(DesignStorageTest, GenericAndTypedRouteEditsKeepWarmSpatialAndOverviewViewsCurrent)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto shape = design.geometryStorage().createRouteShape(
      wire,
      DesignRouteShape{.layer = routing_layer, .flags = DesignWirePathFlag::kHasWidth,
                       .geometry = DesignRouteSegment{.begin = {.position = {0, 0}},
                                                      .end = {.position = {100, 0}}}},
      DesignRouteShapeAttributes{.width = 10});
  DatabaseContext context(design);
  GeometryEditService edit(context);
  RoutingService routing(context);
  GeometryQueryService query(context);
  query.warmupIndex();
  static_cast<void>(design.geometryOverview().tile({0, 0, 0}));

  edit.moveShape(shape.packed(), 5'000, 0);
  EXPECT_TRUE(query.indexReady());
  EXPECT_TRUE(query.queryRefs({0, 0, 256, 256}).records.empty());
  ASSERT_EQ(query.queryRefs({4'900, -64, 5'200, 64}).records.size(), 1u);
  EXPECT_EQ(query.queryRefs({4'900, -64, 5'200, 64}).records.front().id, shape.packed());

  // Cache two affected tiles and two known-unaffected tiles.  A typed Routing
  // edit must share the generic edit post-step: it evicts only tiles meeting
  // the old/new BBoxes, while preserving cache entries elsewhere.
  static_cast<void>(design.geometryOverview().tile({0, 0, 0}));
  static_cast<void>(design.geometryOverview().tile({0, 1, 0}));
  static_cast<void>(design.geometryOverview().tile({0, 2, 0}));
  static_cast<void>(design.geometryOverview().tile({0, 3, 3}));
  EXPECT_EQ(design.geometryOverview().cachedTileCount(), 4u);
  routing.updateShape(shape,
                      DesignRouteShape{.layer = routing_layer, .flags = DesignWirePathFlag::kHasWidth,
                                       .geometry = DesignRouteSegment{.begin = {.position = {9'000, 0}},
                                                                      .end = {.position = {9'100, 0}}}},
                      DesignRouteShapeAttributes{.width = 10});
  EXPECT_TRUE(query.indexReady());
  EXPECT_TRUE(query.queryRefs({4'900, -64, 5'200, 64}).records.empty());
  ASSERT_EQ(query.queryRefs({8'900, -64, 9'200, 64}).records.size(), 1u);
  EXPECT_EQ(query.queryRefs({8'900, -64, 9'200, 64}).records.front().id, shape.packed());
  EXPECT_EQ(design.geometryOverview().cachedTileCount(), 2u);
}

TEST_F(DesignStorageTest, FillOwnerCrudInvalidatesDerivedViewsAndDetachedChildren)
{
  DatabaseContext context(design);
  GeometryEditService edit(context);
  GeometryQueryService query(context);
  CountingObserver observer;
  context.addObserver(observer);

  const auto fill = edit.createFill(
      DesignFillInput{.layer = layer, .rectangles = {{0, 0, 10, 10}, {20, 0, 30, 10}}});
  EXPECT_EQ(observer.after_count, 1u);
  const auto old = design.fillStorage().fill(fill).rectangles.front();
  query.warmupIndex();
  static_cast<void>(query.queryOverview({0, 0, 128, 128}));
  ASSERT_TRUE(edit.removeShape(old.packed()));
  EXPECT_TRUE(design.geometryRegistry().registry().valid(old.entity()));
  const auto after_session_remove = context.revision();

  edit.updateFill(fill, DesignFillInput{.layer = layer, .rectangles = {{100, 100, 120, 120}}});
  EXPECT_EQ(context.revision(), after_session_remove + 1);
  EXPECT_EQ(observer.after_count, 3u);
  EXPECT_FALSE(edit.restoreShape(old.packed()));
  EXPECT_FALSE(design.geometryRegistry().registry().valid(old.entity()));
  EXPECT_TRUE(query.queryRefs({0, 0, 32, 32}).records.empty());
  ASSERT_EQ(query.queryRefs({96, 96, 128, 128}).records.size(), 1u);

  const auto before_failed_update = context.revision();
  EXPECT_THROW(edit.updateFill(fill, DesignFillInput{.layer = layer, .rectangles = {{0, 0, 0, 10}}}),
               std::invalid_argument);
  EXPECT_EQ(context.revision(), before_failed_update);
  ASSERT_EQ(design.fillStorage().fill(fill).rectangles.size(), 1u);
  EXPECT_TRUE(edit.destroyFill(fill));
  EXPECT_EQ(observer.after_count, 4u);
  EXPECT_FALSE(design.fillStorage().contains(fill));
  EXPECT_TRUE(query.queryRefs({96, 96, 128, 128}).records.empty());
  EXPECT_FALSE(edit.destroyFill(fill));
  EXPECT_EQ(observer.after_count, 4u);
  context.removeObserver(observer);
}

TEST_F(DesignStorageTest, TypedRoutingDeletionCommitsPendingSessionOrdinalOnlyAfterSuccess)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "N"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto create = [&](int32_t x) {
    return design.geometryStorage().createRouteShape(
        wire,
        DesignRouteShape{.layer = routing_layer, .flags = DesignWirePathFlag::kHasWidth,
                         .geometry = DesignRouteSegment{.begin = {.position = {x, 0}},
                                                        .end = {.position = {x + 10, 0}}}},
        DesignRouteShapeAttributes{.width = 10});
  };
  const auto a = create(0);
  const auto b = create(20);
  const auto c = create(40);
  DatabaseContext context(design);
  GeometryEditService edit(context);
  RoutingService routing(context);
  GeometryQueryService query(context);
  query.warmupIndex();

  ASSERT_TRUE(edit.removeShape(b.packed()));
  ASSERT_TRUE(routing.destroyShape(a));
  EXPECT_TRUE(query.indexReady());
  ASSERT_TRUE(edit.restoreShape(b.packed()));
  EXPECT_EQ(design.routingStorage().wire(wire).shapes, (std::vector<GeometryShapeId>{b, c}));
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
}

}  // namespace
}  // namespace eccdb
