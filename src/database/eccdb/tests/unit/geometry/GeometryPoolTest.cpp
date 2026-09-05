// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "geometry/GeometryPool.h"

namespace eccdb {
namespace {

static_assert(sizeof(GeometryHandle) == sizeof(uint32_t));

template <typename Value>
void expectSpanEquals(std::span<const Value> actual, std::span<const Value> expected)
{
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t index = 0; index < actual.size(); ++index) {
    EXPECT_EQ(actual[index], expected[index]);
  }
}

TEST(GeometryPoolTest, StoresOneGeometryGroupBehindOneHandle)
{
  GeometryPool pool;
  const std::array rectangles{Rect{.ll_x = 10, .ll_y = 20, .ur_x = 30, .ur_y = 40}, Rect{.ll_x = 50, .ll_y = 60, .ur_x = 70, .ur_y = 80}};
  const std::vector polygons{GeometryPolygonInput{.points = {{0, 0}, {20, 0}, {20, 30}, {0, 30}}}};

  const auto geometry = pool.append(rectangles, polygons);

  EXPECT_EQ(geometry.index, 0u);
  EXPECT_EQ(pool.entryCount(), 1u);
  expectSpanEquals(pool.rectangles(geometry), std::span<const Rect>{rectangles});
  ASSERT_EQ(pool.polygonCount(geometry), 1u);
  expectSpanEquals(pool.polygonPoints(geometry, 0), std::span<const Point>{polygons.front().points});
  EXPECT_EQ(pool.bounds(geometry), (Rect{.ll_x = 0, .ll_y = 0, .ur_x = 70, .ur_y = 80}));
  EXPECT_EQ(pool.shapeCount(geometry), 3u);
  EXPECT_EQ(pool.rectangleCount(), 2u);
  EXPECT_EQ(pool.polygonCount(), 1u);
  EXPECT_EQ(pool.pointCount(), 4u);
}

TEST(GeometryPoolTest, RectangularizesManhattanPolygonsIntoContiguousRectangles)
{
  GeometryPool pool({.polygon_mode = PolygonStorageMode::kRectangularized});
  const std::vector polygons{GeometryPolygonInput{.points = {{0, 0}, {40, 0}, {40, 10}, {10, 10}, {10, 40}, {0, 40}}}};
  const std::array expected{Rect{.ll_x = 0, .ll_y = 0, .ur_x = 10, .ur_y = 40}, Rect{.ll_x = 10, .ll_y = 0, .ur_x = 40, .ur_y = 10}};

  const auto geometry = pool.appendPolygons(polygons);

  EXPECT_EQ(pool.options().polygon_mode, PolygonStorageMode::kRectangularized);
  expectSpanEquals(pool.rectangles(geometry), std::span<const Rect>{expected});
  EXPECT_EQ(pool.polygonCount(geometry), 0u);
  EXPECT_EQ(pool.bounds(geometry), (Rect{.ll_x = 0, .ll_y = 0, .ur_x = 40, .ur_y = 40}));
  EXPECT_EQ(pool.rectangleCount(), 2u);
  EXPECT_EQ(pool.polygonCount(), 0u);
  EXPECT_EQ(pool.pointCount(), 0u);
}

TEST(GeometryPoolTest, PreservesLegacyIdbFortyFiveDegreeConversion)
{
  GeometryPool pool({.polygon_mode = PolygonStorageMode::kRectangularized});
  const std::vector polygon{GeometryPolygonInput{
      .points = {{168335, 103550}, {168335, 103960}, {168510, 103960}, {168590, 103880}, {168590, 103550}, {168335, 103550}}}};
  const std::array expected{Rect{.ll_x = 168335, .ll_y = 103550, .ur_x = 168510, .ur_y = 103960},
                            Rect{.ll_x = 168510, .ll_y = 103550, .ur_x = 168590, .ur_y = 103880}};

  const auto geometry = pool.appendPolygons(polygon);

  expectSpanEquals(pool.rectangles(geometry), std::span<const Rect>{expected});
  EXPECT_EQ(pool.polygonCount(geometry), 0u);
  EXPECT_EQ(pool.rectangleCount(), 2u);
  EXPECT_EQ(pool.pointCount(), 0u);
}

TEST(GeometryPoolTest, RollsBackEntriesAndBackingArrays)
{
  GeometryPool pool;
  const std::array first{Rect{.ll_x = 0, .ll_y = 0, .ur_x = 10, .ur_y = 10}};
  const auto saved = pool.appendRectangles(first);
  const auto checkpoint = pool.checkpoint();

  const std::array second{Rect{.ll_x = 20, .ll_y = 20, .ur_x = 30, .ur_y = 30}};
  const std::vector polygons{GeometryPolygonInput{.points = {{20, 20}, {30, 20}, {30, 30}, {20, 30}}}};
  static_cast<void>(pool.appendRectangles(second));
  static_cast<void>(pool.appendPolygons(polygons));
  pool.rollback(checkpoint);

  EXPECT_EQ(pool.entryCount(), 1u);
  EXPECT_EQ(pool.rectangleCount(), 1u);
  EXPECT_EQ(pool.polygonCount(), 0u);
  EXPECT_EQ(pool.pointCount(), 0u);
  expectSpanEquals(pool.rectangles(saved), std::span<const Rect>{first});
  EXPECT_THROW(static_cast<void>(pool.rectangles(GeometryHandle{.index = 1})), std::out_of_range);
  EXPECT_THROW(static_cast<void>(pool.rectangles(GeometryHandle{})), std::out_of_range);
}

TEST(GeometryPoolTest, RejectsDegenerateGeometry)
{
  GeometryPool pool;
  const std::array zero_area{Rect{.ll_x = 0, .ll_y = 0, .ur_x = 0, .ur_y = 10}};
  EXPECT_THROW(static_cast<void>(pool.appendRectangles(zero_area)), std::invalid_argument);

  const std::vector collinear{GeometryPolygonInput{.points = {{0, 0}, {10, 0}, {20, 0}}}};
  EXPECT_THROW(static_cast<void>(pool.appendPolygons(collinear)), std::invalid_argument);
}

}  // namespace
}  // namespace eccdb
