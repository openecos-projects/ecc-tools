// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "DesignTestFixture.h"
#include "context/DatabaseContext.h"
#include "geometry/GeometryQueryService.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, QueryRefsFiltersPlacedViaByItsExpandedPhysicalLayers)
{
  const auto m2_entity = tech.registry().create();
  tech.registry().emplace<TechLayerInfo>(m2_entity, TechLayerInfo{.name = "M2"});
  tech.registry().emplace<TechRoutingLayer>(m2_entity, TechRoutingLayer{});
  const TechRoutingLayerId m2{m2_entity};

  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "signal"});
  const auto definition = design.routingStorage().createVia(DesignViaInput{
      .name = "VIA1",
      .rectangles = {{.layer = layer, .rectangle = {-4, -4, 4, 4}}},
  });
  const auto placed = design.geometryStorage().appendNetShape(
      net, DesignNetVia{.design_via = definition, .origins = {{100, 100}}});

  DatabaseContext context{design};
  GeometryQueryService query(context);
  GeometryQueryFilter filter;
  filter.layers = {m2.packed()};
  EXPECT_TRUE(query.queryRefs({90, 90, 110, 110}, filter).records.empty());

  filter.layers = {routing_layer.packed()};
  const auto refs = query.queryRefs({90, 90, 110, 110}, filter);
  ASSERT_EQ(refs.records.size(), 1u);
  EXPECT_EQ(refs.records.front().id, placed.packed());
}

TEST_F(DesignStorageTest, QueryRefsAllowsLocalViaOnlyForAnExactDesignViaMaskClause)
{
  const auto definition = design.routingStorage().createVia(DesignViaInput{
      .name = "LOCAL",
      .rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}}},
  });
  const auto local = design.routingStorage().via(definition).rectangles.front();

  DatabaseContext context{design};
  GeometryQueryService query(context);
  GeometryQueryFilter broad;
  broad.kind_owner_masks = {{UINT32_MAX, UINT32_MAX}};
  EXPECT_TRUE(query.queryRefs({0, 0, 10, 10}, broad).records.empty());

  GeometryQueryFilter explicit_local;
  explicit_local.kind_owner_masks = {{
      uint32_t{1} << static_cast<uint32_t>(GeometryShapeKind::kRectangle),
      uint32_t{1} << static_cast<uint32_t>(GeometryOwnerDomain::kDesignVia),
  }};
  const auto refs = query.queryRefs({0, 0, 10, 10}, explicit_local);
  ASSERT_EQ(refs.records.size(), 1u);
  EXPECT_EQ(refs.records.front().id, local.packed());
}

TEST_F(DesignStorageTest, HasShapesMatchesQueryRefsForPhysicalViaLayersAndLocalViaVisibility)
{
  const auto m2_entity = tech.registry().create();
  tech.registry().emplace<TechLayerInfo>(m2_entity, TechLayerInfo{.name = "M2"});
  tech.registry().emplace<TechRoutingLayer>(m2_entity, TechRoutingLayer{});
  const TechRoutingLayerId m2{m2_entity};

  const auto m3_entity = tech.registry().create();
  tech.registry().emplace<TechLayerInfo>(m3_entity, TechLayerInfo{.name = "M3"});
  tech.registry().emplace<TechRoutingLayer>(m3_entity, TechRoutingLayer{});
  const TechRoutingLayerId m3{m3_entity};

  const auto net = design.netlistStorage().createNet(DesignNet{.name = "signal"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  const auto definition = design.routingStorage().createVia(DesignViaInput{
      .name = "VIA12",
      .rectangles = {{.layer = layer, .rectangle = {-4, -4, 4, 4}},
                     {.layer = TechLayerId{m2.entity()}, .rectangle = {-6, -6, 6, 6}}},
  });
  static_cast<void>(design.geometryStorage().createRouteShape(
      wire, DesignRouteShape{.layer = routing_layer,
                             .geometry = DesignRouteVia{.origin = {.position = {100, 100}},
                                                        .via = {.design_via = definition},
                                                        .exit_layer = m2}}));

  const auto local_definition = design.routingStorage().createVia(DesignViaInput{
      .name = "LOCAL",
      .rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}}},
  });
  const auto local = design.routingStorage().via(local_definition).rectangles.front();

  DatabaseContext context{design};
  GeometryQueryService query(context);
  const auto expect_equivalent = [&](Rect box, const GeometryQueryFilter& filter) {
    EXPECT_EQ(query.hasShapes(box, filter), !query.queryRefs(box, filter).records.empty());
  };

  GeometryQueryFilter m1_filter;
  m1_filter.layer = routing_layer.packed();
  expect_equivalent({300, 300, 320, 320}, m1_filter);
  EXPECT_FALSE(query.hasShapes({300, 300, 320, 320}, m1_filter));
  expect_equivalent({90, 90, 110, 110}, m1_filter);
  EXPECT_TRUE(query.hasShapes({90, 90, 110, 110}, m1_filter));

  GeometryQueryFilter m2_filter;
  m2_filter.layer = m2.packed();
  expect_equivalent({90, 90, 110, 110}, m2_filter);
  EXPECT_TRUE(query.hasShapes({90, 90, 110, 110}, m2_filter));

  GeometryQueryFilter m3_filter;
  m3_filter.layer = m3.packed();
  expect_equivalent({90, 90, 110, 110}, m3_filter);
  EXPECT_FALSE(query.hasShapes({90, 90, 110, 110}, m3_filter));

  GeometryQueryFilter broad;
  broad.kind_owner_masks = {{UINT32_MAX, UINT32_MAX}};
  expect_equivalent({0, 0, 10, 10}, broad);
  EXPECT_FALSE(query.hasShapes({0, 0, 10, 10}, broad));

  GeometryQueryFilter explicit_local;
  explicit_local.owner = local_definition.packed();
  explicit_local.kind_owner_masks = {{
      uint32_t{1} << static_cast<uint32_t>(GeometryShapeKind::kRectangle),
      uint32_t{1} << static_cast<uint32_t>(GeometryOwnerDomain::kDesignVia),
  }};
  expect_equivalent({0, 0, 10, 10}, explicit_local);
  EXPECT_TRUE(query.hasShapes({0, 0, 10, 10}, explicit_local));
  const auto local_refs = query.queryRefs({0, 0, 10, 10}, explicit_local);
  ASSERT_EQ(local_refs.records.size(), 1u);
  EXPECT_EQ(local_refs.records.front().id, local.packed());
}

TEST_F(DesignStorageTest, SpatialIndexQueryFirstStopsAfterOneAcceptedPredicate)
{
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "signal"});
  const auto wire = design.routingStorage().createWire(DesignWire{.net = net});
  for (int index = 0; index < 16; ++index) {
    static_cast<void>(design.geometryStorage().createRouteShape(
        wire, DesignRouteShape{.layer = routing_layer,
                               .geometry = DesignRoutePatch{.anchor = {.position = {0, 0}}, .rectangle = {0, 0, 10, 10}}}));
  }

  std::size_t predicate_calls = 0;
  const auto first = design.geometryIndex().queryFirst(
      {0, 0, 10, 10}, {}, [&](GeometryShapeId) {
        ++predicate_calls;
        return true;
      });
  EXPECT_TRUE(first.has_value());
  EXPECT_EQ(predicate_calls, 1u);
}

TEST_F(DesignStorageTest, SpatialIndexForgetsPermanentDeletionRestoreSlot)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "signal"});
  const auto shape = design.geometryStorage().appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {0, 0, 10, 10}});

  DatabaseContext context{design};
  GeometryQueryService query(context);
  query.warmupIndex();
  const auto old_bounds = design.geometryStorage().bounds(shape);
  const auto previous_epoch = design.geometryStorage().geometryEpoch();
  ASSERT_TRUE(design.geometryStorage().destroy(shape));
  design.geometryIndex().applyChange(shape, old_bounds, previous_epoch);
  ASSERT_EQ(design.geometryIndex().pendingRestoreSlotCount(), 1u);

  design.geometryIndex().forgetRemovedSlot(shape);
  EXPECT_EQ(design.geometryIndex().pendingRestoreSlotCount(), 0u);
}

}  // namespace
}  // namespace eccdb
