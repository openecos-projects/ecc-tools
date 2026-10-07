// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "DesignTestFixture.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/geometry/DesignGeometrySpatialIndex.h"
#include "design/geometry/DesignGeometryOverview.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "service/routing/NetRouteService.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

const DesignGeometryOverview::LayerSummary* overviewLayer(const DesignGeometryOverview::Tile& tile, uint64_t layer)
{
  const auto found = std::find_if(tile.layers.begin(), tile.layers.end(), [layer](const auto& summary) {
    return summary.layer == layer;
  });
  return found == tile.layers.end() ? nullptr : &*found;
}

bool hasOccupancy(const std::array<uint64_t, 4>& occupancy)
{
  return std::any_of(occupancy.begin(), occupancy.end(), [](uint64_t word) { return word != 0u; });
}

TEST_F(DesignStorageTest, RouteShapeViewPointsAtNativePayload)
{
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "N1"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto handle = design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRouteSegment{.begin = {.position = {10, 20}},
                                                            .end = {.position = {30, 20}}}});

  const auto view = design.geometryStorage().shape(handle);
  ASSERT_TRUE(view.valid());
  const auto* payload = std::get<const DesignRouteShape*>(view.payload);
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload, &design.routingStorage().shape(handle));
  const auto* before_bounds = design.geometryStorage().registry().tryBounds(handle);
  EXPECT_EQ(before_bounds, nullptr);
  EXPECT_EQ(design.geometryStorage().bounds(handle), (Rect{10, 20, 30, 20}));
  const auto* cached_bounds = design.geometryStorage().registry().tryBounds(handle);
  ASSERT_NE(cached_bounds, nullptr);
  EXPECT_TRUE(cached_bounds->usable());
  EXPECT_EQ(cached_bounds->space, BoundsSpace::kLocal);
  EXPECT_EQ(cached_bounds->rect, (Rect{10, 20, 30, 20}));
}

TEST_F(DesignStorageTest, RouteEditKeepsTheIndependentGeometryEntity)
{
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "N1"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto old_handle = design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRouteSegment{.begin = {.position = {0, 0}},
                                                            .end = {.position = {10, 0}}}});
  auto edited = design.routingStorage().shape(old_handle);
  edited.geometry = DesignRoutePatch{.anchor = {.position = {5, 5}}, .rectangle = {4, 4, 6, 6}};

  design.geometryStorage().updateRouteShape(old_handle, std::move(edited));

  EXPECT_TRUE(design.geometryStorage().contains(old_handle));
  EXPECT_EQ(design.geometryStorage().registry().shape(old_handle).kind, GeometryShapeKind::kPatch);
  EXPECT_EQ(design.geometryStorage().bounds(old_handle), (Rect{4, 4, 6, 6}));
  EXPECT_TRUE(design.geometryStorage().registry().tryBounds(old_handle)->usable());
}

TEST_F(DesignStorageTest, StandaloneNetShapeKeepsIndependentEntityId)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto handle = design.geometryStorage().appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {10, 10, 20, 20}});

  const auto before = design.geometryStorage().shape(handle);
  ASSERT_TRUE(before.valid());
  const auto* before_payload = std::get<const DesignGeometryRectangle*>(before.payload);
  ASSERT_NE(before_payload, nullptr);
  EXPECT_EQ(before_payload->rectangle, (Rect{10, 10, 20, 20}));

  design.geometryStorage().replaceNetShape(handle, DesignNetRectangle{.layer = routing_layer, .rectangle = {30, 30, 40, 40}});
  EXPECT_TRUE(design.geometryStorage().contains(handle));
  const auto current = design.geometryStorage().shapes(net);
  ASSERT_EQ(current.size(), 1u);
  EXPECT_EQ(current.front(), handle);
  EXPECT_EQ(design.geometryStorage().bounds(current.front()), (Rect{30, 30, 40, 40}));
}

TEST_F(DesignStorageTest, StandaloneNetEditMaintainsOwnerRelationAndRevision)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto before = design.geometryStorage().geometryEpoch();
  const auto handle = design.geometryStorage().appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {1, 2, 3, 4}});

  EXPECT_TRUE(design.geometryStorage().dirtySince(before));
  EXPECT_EQ(design.geometryStorage().owner(handle).domain, GeometryOwnerDomain::kDesignNet);
  const auto* relation = design.routingStorage().netGeometry(net);
  ASSERT_NE(relation, nullptr);
  ASSERT_EQ(relation->shapes.size(), 1u);
  EXPECT_EQ(relation->shapes.front(), handle);

  const auto edited_epoch = design.geometryStorage().geometryEpoch();
  design.geometryStorage().replaceNetShape(handle,
                                           DesignNetRectangle{.layer = routing_layer, .rectangle = {5, 6, 7, 8}});
  EXPECT_TRUE(design.geometryStorage().dirtySince(edited_epoch));
  EXPECT_EQ(design.geometryStorage().bounds(handle), (Rect{5, 6, 7, 8}));

  const auto deleted_epoch = design.geometryStorage().geometryEpoch();
  EXPECT_TRUE(design.geometryStorage().destroy(handle));
  EXPECT_TRUE(design.geometryStorage().dirtySince(deleted_epoch));
  EXPECT_FALSE(design.geometryStorage().contains(handle));
  relation = design.routingStorage().netGeometry(net);
  EXPECT_EQ(relation, nullptr);
}

TEST_F(DesignStorageTest, NetEditRejectsRouteShapeId)
{
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "N1"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto route = design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRoutePatch{.anchor = {.position = {10, 10}},
                                                          .rectangle = {9, 9, 11, 11}}});

  EXPECT_THROW(design.geometryStorage().replaceNetShape(
                   route, DesignNetRectangle{.layer = routing_layer, .rectangle = {0, 0, 1, 1}}),
               std::invalid_argument);
}

TEST_F(DesignStorageTest, QueryReturnsRouteAndStandaloneGeometryIds)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "N1"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto route = design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer, .flags = DesignWirePathFlag::kHasWidth,
                             .geometry = DesignRoutePatch{.anchor = {.position = {100, 100}},
                                                          .rectangle = {90, 90, 110, 110}}},
      DesignRouteShapeAttributes{.width = 10});
  const auto standalone = design.geometryStorage().appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {10, 10, 20, 20}});

  const auto near_route = design.geometryStorage().query(net, Rect{80, 80, 120, 120});
  ASSERT_EQ(near_route.size(), 1u);
  EXPECT_EQ(near_route.front(), route);

  const auto all = design.geometryStorage().shapes(net);
  ASSERT_EQ(all.size(), 2u);
  EXPECT_TRUE(std::find(all.begin(), all.end(), standalone) != all.end());
}

TEST_F(DesignStorageTest, MoveShapePreservesHandleAndUpdatesBounds)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "N1"});
  const auto id = design.geometryStorage().appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {10, 20, 30, 40}});
  const auto before = design.geometryStorage().geometryEpoch();

  design.geometryStorage().moveShape(id, 7, -3);

  EXPECT_TRUE(design.geometryStorage().dirtySince(before));
  EXPECT_TRUE(design.geometryStorage().contains(id));
  EXPECT_EQ(design.geometryStorage().bounds(id), (Rect{17, 17, 37, 37}));
  EXPECT_EQ(design.geometryStorage().netShapeIds(net).front(), id);
}

TEST_F(DesignStorageTest, ReorderWireShapesPreservesEntityIds)
{
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "N1"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto first = design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRoutePoint{.point = {.position = {0, 0}}}});
  const auto second = design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRoutePoint{.point = {.position = {10, 0}}}});

  const std::array<GeometryShapeId, 2> order{second, first};
  design.geometryStorage().reorderWireShapes(wire, order);

  const auto ids = design.geometryStorage().wireShapeIds(wire);
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], second);
  EXPECT_EQ(ids[1], first);
  EXPECT_TRUE(design.geometryStorage().contains(first));
  EXPECT_TRUE(design.geometryStorage().contains(second));
}

TEST_F(DesignStorageTest, SpatialIndexRebuildsAfterCanonicalEdit)
{
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "N1"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto shape = design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRouteSegment{.begin = {.position = {10, 10}},
                                                            .end = {.position = {20, 10}}}});

  auto& index = design.geometryIndex();
  const auto first = index.query(net, Rect{0, 0, 32, 32});
  ASSERT_EQ(first.size(), 1u);
  EXPECT_EQ(first.front(), shape);
  EXPECT_EQ(index.indexedShapeCount(), 1u);

  design.geometryStorage().moveShape(shape, 100, 0);
  EXPECT_TRUE(index.query(net, Rect{0, 0, 32, 32}).empty());
  const auto moved = index.query(net, Rect{100, 0, 132, 32});
  ASSERT_EQ(moved.size(), 1u);
  EXPECT_EQ(moved.front(), shape);

  EXPECT_TRUE(design.geometryStorage().destroy(shape));
  EXPECT_TRUE(index.query(net, Rect{100, 0, 132, 32}).empty());
}

TEST_F(DesignStorageTest, GeometryOverviewBuildsSparseLodTilesWithoutShapeCopies)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "OVERVIEW"});
  const auto first = design.geometryStorage().appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {10, 20, 110, 120}});
  const auto second = design.geometryStorage().appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {2'000, 2'000, 2'100, 2'100}});
  EXPECT_EQ(design.geometryStorage().registry().tryBounds(first), nullptr);
  EXPECT_EQ(design.geometryStorage().registry().tryBounds(second), nullptr);

  auto& overview = design.geometryOverview();
  overview.setBaseTileSize(1'024);
  const auto coarse = overview.tiles(Rect{0, 0, 2'047, 2'047});
  ASSERT_EQ(coarse.size(), 4u);
  const auto first_tile = overview.tile({.level = 0, .x = 0, .y = 0});
  EXPECT_EQ(first_tile.shape_count, 1u);
  EXPECT_EQ(first_tile.layers.size(), 1u);
  EXPECT_TRUE(first_tile.occupiedCell(0, 0));
  EXPECT_GT(first_tile.covered_area, 0u);
  const auto second_tile = overview.tile({.level = 0, .x = 1, .y = 1});
  EXPECT_EQ(second_tile.shape_count, 1u);
  EXPECT_EQ(design.geometryStorage().registry().tryBounds(first), nullptr);
  EXPECT_EQ(design.geometryStorage().registry().tryBounds(second), nullptr);

  // The overview is a derived view. Exact editing still returns the original
  // EnTT handles, while the next tile request observes the new epoch.
  design.geometryStorage().moveShape(first, 2'000, 0);
  const auto edited = overview.tile({.level = 0, .x = 1, .y = 0});
  EXPECT_EQ(edited.shape_count, 1u);
  EXPECT_EQ(overview.exactShapes(Rect{1'900, 0, 2'200, 200}), std::vector<GeometryShapeId>{first});
  EXPECT_TRUE(design.geometryStorage().contains(second));
}

TEST_F(DesignStorageTest, StandaloneViaBoundsCoverItsMetalRatherThanJustItsOrigin)
{
  tech.registry().emplace<TechViaGeometry>(tech_via.entity(), TechViaGeometry{.bounding_box = {-3, -5, 7, 11}});
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto shape = design.geometryStorage().appendNetShape(
      net, DesignNetVia{.tech_via = tech_via, .origins = {{100, 100}, {200, 100}}, .orientation = DesignOrientation::kN});

  EXPECT_EQ(design.geometryStorage().bounds(shape), (Rect{97, 95, 207, 111}));
  const auto nearby = design.geometryIndex().query(Rect{96, 94, 99, 98});
  ASSERT_EQ(nearby.size(), 1u);
  EXPECT_EQ(nearby.front(), shape);
  EXPECT_FALSE(design.netlistStorage().destroyNet(net));
  EXPECT_TRUE(design.geometryStorage().destroy(shape));
  EXPECT_TRUE(design.netlistStorage().destroyNet(net));
}

TEST_F(DesignStorageTest, OverviewDoesNotInventTechViaLayersWithoutBoundTechGeometry)
{
  // The fixture has a raw TechRegistry, so its TechViaGeometry has no
  // separate TechStore geometry registry to dereference. The overview must
  // omit this unresolved VIA instead of projecting its union BBox into M1.
  tech.registry().emplace<TechViaGeometry>(tech_via.entity(), TechViaGeometry{.bounding_box = {-3, -5, 7, 11}});
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  static_cast<void>(design.geometryStorage().appendNetShape(
      net, DesignNetVia{.tech_via = tech_via, .origins = {{100, 100}}, .orientation = DesignOrientation::kN}));

  auto& overview = design.geometryOverview();
  overview.setBaseTileSize(256);
  const auto tile = overview.tile({.level = 0, .x = 0, .y = 0});
  EXPECT_EQ(tile.shape_count, 0u);
  EXPECT_TRUE(tile.layers.empty());
}

TEST(UnifiedGeometryOverview, ExpandsRotatedTechnologyViaIntoBottomCutAndTopLayers)
{
  TechStore technology;
  const auto m1 = technology.createRoutingLayer(TechLayerInfo{.name = "M1"});
  const auto cut = technology.createCutLayer(TechLayerInfo{.name = "V1"});
  const auto m2 = technology.createRoutingLayer(TechLayerInfo{.name = "M2"});
  const auto via = technology.viaMasterStorage().createFixedViaMaster(
      TechViaMaster{.name = "VIA12"},
      TechViaMasterShapeInput{.bottom_layer = m1,
                              .bottom_geometry = {.rects = {{-6, -4, 8, 6}}},
                              .cut_layer = cut,
                              .cut_geometry = {.rects = {{-2, -1, 2, 1}}},
                              .top_layer = m2,
                              .top_geometry = {.polygons = {{.points = {{-8, -7}, {10, -7}, {10, 5}, {-8, 5}}}}}});
  LibraryStore library(technology.techRegistry());
  DesignStore design(technology.techRegistry(), library.libraryRegistry(), &technology.geometryRegistry());
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  static_cast<void>(design.geometryStorage().createRouteShape(
      wire,
      DesignRouteShape{.layer = m1,
                       .flags = DesignWirePathFlag::kHasWidth,
                       .geometry = DesignRouteVia{.origin = {.position = {100, 100}},
                                                  .via = {.tech_via = via, .orientation = DesignOrientation::kE}}},
      DesignRouteShapeAttributes{.width = 0}));

  auto& overview = design.geometryOverview();
  overview.setBaseTileSize(256);
  const auto tile = overview.tile({.level = 0, .x = 0, .y = 0});
  ASSERT_EQ(tile.shape_count, 1u);
  ASSERT_EQ(tile.layers.size(), 3u);
  const auto* bottom = overviewLayer(tile, m1.packed());
  const auto* cut_summary = overviewLayer(tile, cut.packed());
  const auto* top = overviewLayer(tile, m2.packed());
  ASSERT_NE(bottom, nullptr);
  ASSERT_NE(cut_summary, nullptr);
  ASSERT_NE(top, nullptr);
  EXPECT_EQ(bottom->shape_count, 1u);
  EXPECT_EQ(bottom->bounds, (Rect{96, 92, 106, 106}));
  EXPECT_EQ(cut_summary->shape_count, 1u);
  EXPECT_EQ(cut_summary->bounds, (Rect{99, 98, 101, 102}));
  EXPECT_TRUE(hasOccupancy(cut_summary->occupancy));
  EXPECT_EQ(top->shape_count, 1u);
  EXPECT_EQ(top->bounds, (Rect{93, 90, 105, 108}));
  EXPECT_TRUE(std::none_of(tile.layers.begin(), tile.layers.end(), [](const auto& summary) {
    return summary.layer == GeometryQueryFilter::kUnspecified;
  }));
}

TEST(UnifiedGeometryOverview, ExpandsDesignViaArraysSpecialViasAndPlacedPinVias)
{
  TechStore technology;
  const auto m1 = technology.createRoutingLayer(TechLayerInfo{.name = "M1"});
  const auto cut = technology.createCutLayer(TechLayerInfo{.name = "V1"});
  const auto m2 = technology.createRoutingLayer(TechLayerInfo{.name = "M2"});
  const auto generated_rule = technology.viaRuleGenerateStorage().createViaRuleGenerate(
      TechViaRuleGenerate{.name = "GEN"},
      TechViaRuleGenerateBottomLayer{.layer = m1},
      TechViaRuleGenerateCutLayer{.layer = cut,
                                  .flags = TechViaRuleGenerateCutLayerFlag::kHasRect
                                           | TechViaRuleGenerateCutLayerFlag::kHasSpacing,
                                  .cut_rect = {-2, -2, 2, 2},
                                  .spacing_x = 2,
                                  .spacing_y = 2},
      TechViaRuleGenerateTopLayer{.layer = m2});
  LibraryStore library(technology.techRegistry());
  DesignStore design(technology.techRegistry(), library.libraryRegistry(), &technology.geometryRegistry());
  const auto fixed = design.routingStorage().createVia(
      DesignViaInput{.name = "FIXED",
                     .rectangles = {{.layer = TechLayerId{m1.entity()}, .rectangle = {-6, -4, 8, 6}},
                                    {.layer = TechLayerId{m2.entity()}, .rectangle = {-4, -8, 10, 5}}},
                     .polygons = {{.layer = TechLayerId{cut.entity()},
                                   .points = {{-2, -1}, {2, -1}, {2, 1}, {-2, 1}}}}});
  const auto generated = design.routingStorage().createVia(
      DesignViaInput{.name = "GENERATED",
                     .flags = DesignViaFlag::kGenerated,
                     .generated = {.via_rule = generated_rule,
                                   .bottom_layer = m1,
                                   .cut_layer = cut,
                                   .top_layer = m2,
                                   .flags = DesignGeneratedViaFlag::kHasRowCol | DesignGeneratedViaFlag::kHasOrigin
                                            | DesignGeneratedViaFlag::kHasOffset,
                                   .cut_size_x = 4,
                                   .cut_size_y = 6,
                                   .cut_spacing_x = 2,
                                   .cut_spacing_y = 4,
                                   .bottom_enclosure_x = 1,
                                   .bottom_enclosure_y = 2,
                                   .top_enclosure_x = 3,
                                   .top_enclosure_y = 4,
                                   .row_count = 1,
                                   .column_count = 2,
                                   .origin = {1, -1},
                                   .bottom_offset = {3, 5},
                                   .top_offset = {-2, 4}}});
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto route = design.geometryStorage().createRouteShape(
      wire,
      DesignRouteShape{.layer = m1,
                       .flags = DesignWirePathFlag::kHasWidth,
                       .geometry = DesignRouteVia{.origin = {.position = {1200, 1200}},
                                                  .via = {.design_via = fixed,
                                                          .orientation = DesignOrientation::kW,
                                                          .flags = DesignWireViaFlag::kHasArray,
                                                          .rows = 2,
                                                          .columns = 2,
                                                          .step_x = 60,
                                                          .step_y = 40}}},
      DesignRouteShapeAttributes{.width = 0});
  const auto special = design.geometryStorage().appendNetShape(
      net, DesignNetVia{.design_via = generated, .origins = {{1500, 1400}, {1700, 1400}}, .orientation = DesignOrientation::kS});
  const auto term = design.netlistStorage().createBlockTerm(DesignBlockTerm{.name = "IO"});
  const auto pin = design.netlistStorage().createBlockPin(
      term, DesignBlockPinInput{.flags = DesignIoPinPortFlag::kHasPlacement,
                                .placement_status = DesignPlacementStatus::kPlaced,
                                .origin = {1800, 1600},
                                .orientation = DesignOrientation::kE,
                                .vias = {{.design_via = fixed, .origin = {20, 10}}}});
  const auto pin_via = design.netlistStorage().blockPin(pin).vias.front();

  std::vector<std::pair<uint64_t, Rect>> pin_bounds;
  EXPECT_TRUE(design.geometryStorage().forEachViaLayerBounds(pin_via, [&](TechLayerId layer, Rect bounds) {
    pin_bounds.emplace_back(layer.packed(), bounds);
  }));
  ASSERT_EQ(pin_bounds.size(), 3u);
  EXPECT_NE(std::find(pin_bounds.begin(), pin_bounds.end(), std::pair<uint64_t, Rect>{m1.packed(), Rect{1806, 1572, 1816, 1586}}),
            pin_bounds.end());
  EXPECT_NE(std::find(pin_bounds.begin(), pin_bounds.end(), std::pair<uint64_t, Rect>{cut.packed(), Rect{1809, 1578, 1811, 1582}}),
            pin_bounds.end());
  EXPECT_NE(std::find(pin_bounds.begin(), pin_bounds.end(), std::pair<uint64_t, Rect>{m2.packed(), Rect{1802, 1570, 1815, 1584}}),
            pin_bounds.end());

  std::size_t route_pieces = 0;
  EXPECT_TRUE(design.geometryStorage().forEachViaLayerBounds(route, [&](TechLayerId, Rect) { ++route_pieces; }));
  EXPECT_EQ(route_pieces, 12u);
  std::size_t special_pieces = 0;
  EXPECT_TRUE(design.geometryStorage().forEachViaLayerBounds(special, [&](TechLayerId, Rect) { ++special_pieces; }));
  EXPECT_EQ(special_pieces, 8u);

  auto& overview = design.geometryOverview();
  overview.setBaseTileSize(1024);
  const auto tile = overview.tile({.level = 0, .x = 1, .y = 1});
  EXPECT_EQ(tile.shape_count, 3u);
  const auto* bottom = overviewLayer(tile, m1.packed());
  const auto* cut_summary = overviewLayer(tile, cut.packed());
  const auto* top = overviewLayer(tile, m2.packed());
  ASSERT_NE(bottom, nullptr);
  ASSERT_NE(cut_summary, nullptr);
  ASSERT_NE(top, nullptr);
  EXPECT_EQ(bottom->shape_count, 7u);
  EXPECT_EQ(cut_summary->shape_count, 9u);
  EXPECT_EQ(top->shape_count, 7u);
  EXPECT_TRUE(hasOccupancy(cut_summary->occupancy));
  EXPECT_TRUE(std::none_of(tile.layers.begin(), tile.layers.end(), [](const auto& summary) {
    return summary.layer == GeometryQueryFilter::kUnspecified;
  }));
}

TEST_F(DesignStorageTest, SpatialTreePrunesBranchesAndFiltersNetAndWire)
{
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "N1"});
  const auto other_net = design.netlistStorage().createNet(DesignNet{.name = "N2"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto other_wire = design.routingStorage().createWire(DesignWire{.net = other_net});
  std::vector<GeometryShapeId> expected;
  std::vector<GeometryShapeId> expected_net;
  const Rect window{-25, -5, 25, 5};
  for (int index = 0; index < 40; ++index) {
    const int x = (index - 20) * 10;
    const Rect box{x, -1, x + 2, 1};
    const auto owner = index % 2 == 0 ? wire : other_wire;
    const auto id = design.geometryStorage().createRouteShape(
        owner, DesignRouteShape{.layer = routing_layer,
                               .geometry = DesignRoutePatch{.anchor = {.position = {x, 0}}, .rectangle = box}});
    if (box.intersects(window)) {
      expected.push_back(id);
      if (owner == wire) expected_net.push_back(id);
    }
  }
  const auto sorted = [](std::vector<GeometryShapeId> values) {
    std::sort(values.begin(), values.end(), [](auto lhs, auto rhs) { return lhs.packed() < rhs.packed(); });
    return values;
  };
  auto& tree = design.geometryIndex();
  EXPECT_EQ(sorted(tree.query(window)), sorted(expected));
  EXPECT_EQ(sorted(tree.query(net, window)), sorted(expected_net));
  EXPECT_EQ(sorted(tree.query(wire, window)), sorted(expected_net));
  EXPECT_EQ(tree.indexedShapeCount(), 40u);
}

TEST_F(DesignStorageTest, SpatialTreeFiltersLayerAndGeometryKindWithoutSecondIndex)
{
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "N1"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto segment = design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRouteSegment{.begin = {.position = {0, 0}},
                                                            .end = {.position = {20, 0}}}});
  const auto patch = design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRoutePatch{.anchor = {.position = {5, 5}},
                                                          .rectangle = {4, 4, 8, 8}}});

  GeometryQueryFilter filter;
  filter.layer = routing_layer.packed();
  filter.kind_mask = uint32_t{1} << static_cast<uint8_t>(GeometryShapeKind::kSegment);
  const auto segments = design.geometryIndex().query(Rect{-10, -10, 30, 30}, filter);
  EXPECT_EQ(segments, std::vector<GeometryShapeId>{segment});

  filter.kind_mask = uint32_t{1} << static_cast<uint8_t>(GeometryShapeKind::kPatch);
  const auto patches = design.geometryIndex().query(Rect{-10, -10, 30, 30}, filter);
  EXPECT_EQ(patches, std::vector<GeometryShapeId>{patch});
}

TEST_F(DesignStorageTest, SpatialIndexTracksNetAndRuleWidthChanges)
{
  const auto rule = design.routingStorage().createNonDefaultRule(
      DesignNonDefaultRule{.name = "RULE", .layer_rules = {{.layer = layer, .width = 40}}});
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "N1"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto id = design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRouteSegment{.begin = {.position = {0, 0}},
                                                            .end = {.position = {100, 0}}}});
  const Rect near_line{40, 10, 60, 12};
  EXPECT_TRUE(design.geometryIndex().query(near_line).empty());
  auto value = design.netlistStorage().net(net);
  value.design_non_default_rule = rule;
  value.flags |= DesignNetFlag::kHasNonDefaultRule;
  design.netlistStorage().updateNet(net, std::move(value));
  EXPECT_EQ(design.geometryIndex().query(near_line), std::vector<GeometryShapeId>{id});

  design.routingStorage().updateNonDefaultRule(
      rule, DesignNonDefaultRule{.name = "RULE", .layer_rules = {{.layer = layer, .width = 4}}});
  EXPECT_TRUE(design.geometryIndex().query(near_line).empty());
}

TEST_F(DesignStorageTest, StandaloneEditsKeepSerializablePayloadInvariant)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
  const auto rectangle = design.geometryStorage().appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {10, 10, 20, 20}});
  const auto before = design.geometryStorage().geometryEpoch();
  EXPECT_THROW(design.geometryStorage().replaceNetShape(
                   rectangle, DesignNetRectangle{.layer = routing_layer, .rectangle = {20, 20, 10, 10}}),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(design.geometryStorage().appendNetShape(
                   net, DesignNetPolygon{.layer = routing_layer, .points = {}})), std::invalid_argument);
  EXPECT_EQ(design.geometryStorage().geometryEpoch(), before);
  EXPECT_EQ(design.geometryStorage().bounds(rectangle), (Rect{10, 10, 20, 20}));
  EXPECT_EQ(design.geometryStorage().netShapeIds(net).size(), 1u);
  EXPECT_NO_THROW(design.routingStorage().validateRestoredRoutingState());
}

TEST_F(DesignStorageTest, BlockPinPlacementBoundsAndMoveRemainConnected)
{
  auto& netlist = design.netlistStorage();
  const auto net = netlist.createNet(DesignNet{.name = "signal"});
  const auto term = netlist.createIoPin(
      DesignIoPin{.name = "IN"},
      {DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit | DesignIoPinPortFlag::kHasPlacement,
                           .placement_status = DesignPlacementStatus::kFixed,
                           .origin = {100, 200},
                           .orientation = DesignOrientation::kE,
                           .rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 20}}}}});
  netlist.connect(term, net);

  const auto pin = netlist.blockPins(term).front();
  const auto shape = netlist.blockPin(pin).rectangles.front();
  EXPECT_EQ(design.geometryStorage().owner(shape).domain, GeometryOwnerDomain::kDesignBlockPin);
  EXPECT_EQ(design.geometryStorage().localBounds(shape), (Rect{0, 0, 10, 20}));
  EXPECT_EQ(design.geometryStorage().bounds(shape), (Rect{100, 190, 120, 200}));

  design.geometryStorage().moveShape(shape, 10, 20);

  EXPECT_EQ(netlist.blockPinRectangle(shape).rectangle, (Rect{-20, 10, -10, 30}));
  EXPECT_EQ(design.geometryStorage().bounds(shape), (Rect{110, 210, 130, 220}));
  EXPECT_EQ(netlist.blockPin(pin).term, term);
  EXPECT_EQ(netlist.ioPin(term).net, net);
  const auto indexed = design.geometryIndex().query(net, Rect{100, 200, 140, 230});
  ASSERT_EQ(indexed.size(), 1u);
  EXPECT_EQ(indexed.front(), shape);
}

TEST_F(DesignStorageTest, OwnerGeometryUsesSharedRegistryWithoutNetMisclassification)
{
  auto& netlist = design.netlistStorage();
  auto& geometry = design.geometryStorage();
  const auto net = netlist.createNet(DesignNet{.name = "signal"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto net_shape = geometry.createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRoutePatch{.anchor = {.position = {0, 0}}, .rectangle = {0, 0, 10, 10}}});

  const auto term = netlist.createIoPin(
      DesignIoPin{.name = "IN"},
      {DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit | DesignIoPinPortFlag::kHasPlacement,
                           .placement_status = DesignPlacementStatus::kFixed,
                           .origin = {12, 0},
                           .rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}}}}});
  netlist.connect(term, net);
  const auto block_pin = netlist.blockPins(term).front();
  const auto block_pin_shape = netlist.blockPin(block_pin).rectangles.front();

  const auto via = design.routingStorage().createVia(
      DesignViaInput{.name = "LOCAL", .rectangles = {{.layer = layer, .rectangle = {84, 0, 94, 10}}}});
  const auto via_shape = design.routingStorage().via(via).rectangles.front();

  auto instance = createInstance("bounded");
  instance.flags = DesignInstanceFlag::kHasRegionBounds;
  const std::array<Rect, 1> instance_bounds{{{72, 0, 82, 10}}};
  const auto bounded_instance = netlist.createInstance(instance, instance_bounds);
  const auto instance_shape = netlist.instance(bounded_instance).region_bounds.front();

  const auto region = design.constraintStorage().createRegion(
      DesignRegionInput{.name = "FENCE", .rectangles = {{36, 0, 46, 10}}});
  const auto region_shape = design.constraintStorage().region(region).rectangles.front();
  const auto blockage = design.constraintStorage().createBlockage(
      DesignBlockageInput{.kind = DesignBlockageKind::kPlacement, .rectangles = {{48, 0, 58, 10}}});
  const auto blockage_shape = design.constraintStorage().blockage(blockage).rectangles.front();
  const auto fill = design.fillStorage().createFill(DesignFillInput{.layer = layer, .rectangles = {{60, 0, 70, 10}}});
  const auto fill_shape = design.fillStorage().fill(fill).rectangles.front();

  design.globalStorage().setDieArea(DesignDieAreaInput{.boundary = {{-20, -20}, {120, 120}}});
  const auto die_boundary = design.globalStorage().dieArea().boundary;
  design.floorplanStorage().setCoreArea(DesignCoreAreaInput{.boundary = {{-10, -10}, {110, 110}}});
  const auto core_boundary = design.floorplanStorage().coreArea().boundary;

  design.guideStorage().replaceGuides(net, {{.box = {24, 0, 34, 10}, .layer = routing_layer}});
  design.guideStorage().replaceTrackAssignments(net, {{.layer = routing_layer, .box = {24, 12, 34, 12}}});
  const auto guide_shape = design.guideStorage().guides(net).front();
  const auto assignment_shape = design.guideStorage().trackAssignments(net).front();

  const auto expect_owner = [&](GeometryShapeId id, GeometryOwner owner) {
    const auto actual = geometry.owner(id);
    EXPECT_EQ(actual.domain, owner.domain);
    EXPECT_EQ(actual.value, owner.value);
  };
  expect_owner(net_shape, designWireOwner(wire));
  expect_owner(block_pin_shape, designBlockPinOwner(block_pin));
  expect_owner(via_shape, designViaOwner(via));
  expect_owner(instance_shape, designInstanceOwner(bounded_instance));
  expect_owner(region_shape, designRegionOwner(region));
  expect_owner(blockage_shape, designBlockageOwner(blockage));
  expect_owner(fill_shape, designFillOwner(fill));
  expect_owner(die_boundary, designDieAreaOwner(design.rootId()));
  expect_owner(core_boundary, designCoreAreaOwner(design.rootId()));
  expect_owner(guide_shape, designGuideOwner(net));
  expect_owner(assignment_shape, designTrackAssignmentOwner(net));

  const auto contains = [](const std::vector<GeometryShapeId>& values, GeometryShapeId id) {
    return std::find(values.begin(), values.end(), id) != values.end();
  };
  const Rect window{-30, -30, 130, 130};
  const auto all = design.geometryIndex().query(window);
  EXPECT_TRUE(contains(all, net_shape));
  EXPECT_TRUE(contains(all, block_pin_shape));
  EXPECT_TRUE(contains(all, region_shape));
  EXPECT_TRUE(contains(all, blockage_shape));
  EXPECT_TRUE(contains(all, fill_shape));
  EXPECT_TRUE(contains(all, instance_shape));
  EXPECT_TRUE(contains(all, die_boundary));
  EXPECT_TRUE(contains(all, core_boundary));
  EXPECT_TRUE(contains(all, guide_shape));
  EXPECT_TRUE(contains(all, assignment_shape));
  EXPECT_FALSE(contains(all, via_shape));

  const auto net_shapes = design.geometryIndex().query(net, window);
  EXPECT_TRUE(contains(net_shapes, net_shape));
  EXPECT_TRUE(contains(net_shapes, block_pin_shape));
  EXPECT_TRUE(contains(net_shapes, guide_shape));
  EXPECT_TRUE(contains(net_shapes, assignment_shape));
  EXPECT_FALSE(contains(net_shapes, region_shape));
  EXPECT_FALSE(contains(net_shapes, blockage_shape));
  EXPECT_FALSE(contains(net_shapes, fill_shape));
  EXPECT_FALSE(contains(net_shapes, instance_shape));
  EXPECT_FALSE(contains(net_shapes, via_shape));
  EXPECT_EQ(design.geometryIndex().query(wire, window), std::vector<GeometryShapeId>{net_shape});
  EXPECT_EQ(design.geometryIndex().query(designRegionOwner(region), window), std::vector<GeometryShapeId>{region_shape});
  EXPECT_EQ(design.geometryIndex().query(designViaOwner(via), window), std::vector<GeometryShapeId>{via_shape});

  design.guideStorage().clearGuides(net);
  design.guideStorage().clearTrackAssignments(net);
  netlist.disconnect(term, net);
  EXPECT_TRUE(netlist.destroyIoPin(term));
  EXPECT_TRUE(netlist.destroyInstance(bounded_instance));
  EXPECT_TRUE(design.constraintStorage().destroyRegion(region));
  EXPECT_TRUE(design.constraintStorage().destroyBlockage(blockage));
  EXPECT_TRUE(design.fillStorage().destroyFill(fill));
  EXPECT_TRUE(design.routingStorage().destroyVia(via));
  design.globalStorage().setDieArea(DesignDieAreaInput{.boundary = {{-30, -30}, {130, 130}}});
  design.floorplanStorage().setCoreArea(DesignCoreAreaInput{.boundary = {{-20, -20}, {120, 120}}});

  for (const auto id : {block_pin_shape, via_shape, instance_shape, region_shape, blockage_shape, fill_shape, die_boundary, core_boundary,
                        guide_shape, assignment_shape}) {
    EXPECT_FALSE(design.geometryRegistry().contains(id));
  }
}

TEST_F(DesignStorageTest, GeometryDestroyRejectsLastShapeOfRequiredOwner)
{
  auto& geometry = design.geometryStorage();
  const auto via = design.routingStorage().createVia(
      DesignViaInput{.name = "LOCAL", .rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}}}});
  const auto via_shape = design.routingStorage().via(via).rectangles.front();
  const auto region = design.constraintStorage().createRegion(
      DesignRegionInput{.name = "FENCE", .rectangles = {{20, 0, 30, 10}}});
  const auto region_shape = design.constraintStorage().region(region).rectangles.front();
  const auto blockage = design.constraintStorage().createBlockage(
      DesignBlockageInput{.kind = DesignBlockageKind::kPlacement, .rectangles = {{40, 0, 50, 10}}});
  const auto blockage_shape = design.constraintStorage().blockage(blockage).rectangles.front();
  const auto fill = design.fillStorage().createFill(
      DesignFillInput{.layer = layer, .rectangles = {{60, 0, 70, 10}}});
  const auto fill_shape = design.fillStorage().fill(fill).rectangles.front();

  const auto expect_rejected_destroy = [&](GeometryShapeId id, GeometryOwner expected_owner) {
    const auto epoch = geometry.geometryEpoch();
    EXPECT_THROW(static_cast<void>(geometry.destroy(id)), std::invalid_argument);
    EXPECT_EQ(geometry.geometryEpoch(), epoch);
    EXPECT_TRUE(geometry.contains(id));
    const auto actual_owner = geometry.owner(id);
    EXPECT_EQ(actual_owner.domain, expected_owner.domain);
    EXPECT_EQ(actual_owner.value, expected_owner.value);
    EXPECT_EQ(geometry.shapes(expected_owner), (std::vector<GeometryShapeId>{id}));
  };

  expect_rejected_destroy(via_shape, designViaOwner(via));
  expect_rejected_destroy(region_shape, designRegionOwner(region));
  expect_rejected_destroy(blockage_shape, designBlockageOwner(blockage));
  expect_rejected_destroy(fill_shape, designFillOwner(fill));

  EXPECT_TRUE(design.routingStorage().destroyVia(via));
  EXPECT_TRUE(design.constraintStorage().destroyRegion(region));
  EXPECT_TRUE(design.constraintStorage().destroyBlockage(blockage));
  EXPECT_TRUE(design.fillStorage().destroyFill(fill));
  EXPECT_FALSE(design.geometryRegistry().contains(via_shape));
  EXPECT_FALSE(design.geometryRegistry().contains(region_shape));
  EXPECT_FALSE(design.geometryRegistry().contains(blockage_shape));
  EXPECT_FALSE(design.geometryRegistry().contains(fill_shape));
}

TEST_F(DesignStorageTest, RestoredGeometryValidationRejectsMismatchedOwner)
{
  const auto region = design.constraintStorage().createRegion(
      DesignRegionInput{.name = "FENCE", .rectangles = {{0, 0, 10, 10}}});
  const auto fill = design.fillStorage().createFill(
      DesignFillInput{.layer = layer, .rectangles = {{20, 0, 30, 10}}});
  const auto region_shape = design.constraintStorage().region(region).rectangles.front();

  design.geometryRegistry().shape(region_shape).owner = designFillOwner(fill);

  EXPECT_THROW(design.geometryStorage().validateRestoredState(), std::runtime_error);
}

TEST_F(DesignStorageTest, RestoredGeometryValidationRejectsWrongPayloadForOwnerRelation)
{
  const auto term = design.netlistStorage().createIoPin(
      DesignIoPin{.name = "IN"},
      {DesignBlockPinInput{.rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}}}}});
  const auto pin = design.netlistStorage().blockPins(term).front();
  const auto rectangle = design.netlistStorage().blockPin(pin).rectangles.front();
  auto& registry = design.geometryRegistry().registry();
  registry.remove<DesignPinRectangle>(rectangle.entity());
  registry.emplace<DesignPinPolygon>(rectangle.entity(),
                                     DesignPinPolygon{.layer = layer, .points = {{0, 0}, {10, 0}, {10, 10}}});
  design.geometryRegistry().shape(rectangle).kind = GeometryShapeKind::kPolygon;

  EXPECT_THROW(design.geometryStorage().validateRestoredState(), std::runtime_error);
}

TEST_F(DesignStorageTest, RestoredGeometryValidationRejectsMultiplePayloadsForOneShape)
{
  const auto fill = design.fillStorage().createFill(
      DesignFillInput{.layer = layer, .rectangles = {{0, 0, 10, 10}}});
  const auto rectangle = design.fillStorage().fill(fill).rectangles.front();
  design.geometryRegistry().registry().emplace<GeometryPolygon>(rectangle.entity(),
                                                                 GeometryPolygon{.points = {{0, 0}, {10, 0}, {10, 10}}});

  EXPECT_THROW(design.geometryStorage().validateRestoredState(), std::runtime_error);
}

TEST(UnifiedGeometryService, BulkRouteWriteMaintainsGeometryIndexAndRollsBackFailedReplacement)
{
  TechStore technology;
  const auto routing_layer = technology.createRoutingLayer(TechLayerInfo{.name = "M1"});
  LibraryStore library(technology.techRegistry());
  DesignStore design(technology.techRegistry(), library.libraryRegistry());
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "N1"});
  NetRouteService service(design, technology);
  const std::array<RouteShape, 1> first{
      RoutePatch{.layer = routing_layer, .rectangle = {10, 10, 20, 20}}};
  const auto wire = service.replaceRouted(net, first);
  const auto id = design.geometryStorage().wireShapeIds(wire).front();
  const auto initial = design.geometryIndex().query(net, Rect{0, 0, 30, 30});
  ASSERT_EQ(initial.size(), 1u);
  EXPECT_EQ(initial.front(), id);

  const std::array<RouteShape, 2> invalid{
      RoutePatch{.layer = routing_layer, .rectangle = {100, 100, 110, 110}},
      RoutePatch{.layer = routing_layer, .rectangle = {20, 20, 10, 10}}};
  EXPECT_THROW(static_cast<void>(service.replaceRouted(net, invalid)), std::invalid_argument);
  EXPECT_TRUE(design.routingStorage().contains(wire));
  EXPECT_TRUE(design.geometryStorage().contains(id));
  EXPECT_EQ(design.geometryIndex().query(Rect{0, 0, 200, 200}), initial);

  const std::array<RouteShape, 1> moved{
      RoutePatch{.layer = routing_layer, .rectangle = {100, 100, 110, 110}}};
  const auto replacement = service.replaceRouted(net, moved);
  EXPECT_FALSE(design.geometryStorage().contains(id));
  EXPECT_TRUE(design.geometryIndex().query(net, Rect{0, 0, 30, 30}).empty());
  const auto updated = design.geometryIndex().query(replacement, Rect{90, 90, 120, 120});
  ASSERT_EQ(updated.size(), 1u);
  EXPECT_EQ(updated.front(), design.geometryStorage().wireShapeIds(replacement).front());
}

}  // namespace
}  // namespace eccdb
