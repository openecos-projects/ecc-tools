// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "DatabaseTestAccess.h"
#include "DesignTestFixture.h"
#include "query/GeometryQueryService.h"

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

  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryQueryService query(database);
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

  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryQueryService query(database);
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

TEST_F(DesignStorageTest, SpatialIndexForgetsPermanentDeletionRestoreSlot)
{
  const auto net = design.netlistStorage().createSpecialNet(DesignNet{.name = "signal"});
  const auto shape = design.geometryStorage().appendNetShape(
      net, DesignNetRectangle{.layer = routing_layer, .rectangle = {0, 0, 10, 10}});

  auto database = detail::DatabaseTestAccess::borrow(design);
  GeometryQueryService query(database);
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
