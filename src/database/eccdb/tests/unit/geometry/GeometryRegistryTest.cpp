// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>

#include <cstdint>

#include "geometry/DomainGeometryStorage.h"

namespace eccdb {

static_assert(sizeof(GeometryEntity) == sizeof(uint64_t));
static_assert(entt::entt_traits<GeometryEntity>::entity_mask == 0xFFFFFFFFu);
static_assert(entt::entt_traits<GeometryEntity>::version_mask == 0xFFFFFFFFu);

TEST(GeometryRegistryTest, CreatesIndependentTypedEntities)
{
  DomainGeometryStorage storage;
  const auto first = storage.createRectangle(GeometryOwner{}, Rect{0, 0, 10, 10});
  const auto second = storage.createPolygon(GeometryOwner{}, {{0, 0}, {10, 0}, {10, 10}});

  ASSERT_NE(first, second);
  EXPECT_TRUE(storage.contains(first));
  EXPECT_TRUE(storage.contains(second));
  EXPECT_EQ(storage.rectangle(first).rectangle, (Rect{0, 0, 10, 10}));
  EXPECT_EQ(storage.polygonPoints(second).size(), 3u);
}

TEST(GeometryRegistryTest, DestroyInvalidatesOnlyTheSelectedEntity)
{
  DomainGeometryStorage storage;
  const auto first = storage.createRectangle(GeometryOwner{}, Rect{0, 0, 10, 10});
  const auto second = storage.createRectangle(GeometryOwner{}, Rect{10, 0, 20, 10});

  EXPECT_TRUE(storage.geometryRegistry().destroy(first));
  EXPECT_FALSE(storage.contains(first));
  EXPECT_TRUE(storage.contains(second));
}

}  // namespace eccdb
