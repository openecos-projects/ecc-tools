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
