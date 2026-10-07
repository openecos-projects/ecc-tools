#include <gtest/gtest.h>
#include <array>

#include "DesignTestFixture.h"
#include "DatabaseTestAccess.h"
#include "query/GeometryEditService.h"
#include "query/GeometryQueryService.h"

namespace eccdb {
namespace {

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
  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryEditService edit(database);
  GeometryQueryService query(database);
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
  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryEditService edit(database);
  GeometryQueryService query(database);
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
  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryQueryService query(database);
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

  GeometryEditService edit(database);
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
  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryQueryService query(database);
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
  auto database = detail::DatabaseTestAccess::borrow(design);
  {
    GeometryEditService edit(database);
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
  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryQueryService query(database);
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
  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryEditService edit(database);
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
  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryEditService edit(database);
  GeometryQueryService query(database);
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

TEST_F(DesignStorageTest, PermanentRemovalClosesJournalOrdinalAndReleasesIndexRestoreSlot)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto create = [&](int32_t x) {
    return design.geometryStorage().appendNetShape(net,
        DesignNetRectangle{.layer = routing_layer, .rectangle = {x,0,x+10,10}});
  };
  const auto a = create(0), b = create(20), c = create(40);
  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryEditService edit(database);
  GeometryQueryService query(database);
  query.warmupIndex();
  EXPECT_TRUE(edit.removeShape(b.packed()));
  EXPECT_TRUE(edit.destroyShape(a.packed()));
  EXPECT_TRUE(edit.restoreShape(b.packed()));
  EXPECT_EQ(design.routingStorage().netGeometry(net)->shapes, (std::vector<GeometryShapeId>{b,c}));
  EXPECT_EQ(design.geometryIndex().pendingRestoreSlotCount(), 0u);
  EXPECT_TRUE(query.indexReady());
}

}  // namespace
}  // namespace eccdb
