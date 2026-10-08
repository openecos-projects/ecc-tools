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

#include <array>
#include <cstdint>
#include <vector>

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "context/DatabaseContext.h"
#include "service/geometry/GeometryPayloadService.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

class GeometryPayloadServiceTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    m1 = technology.createRoutingLayer(TechLayerInfo{.name = "M1"}, TechRoutingLayer{.width = 18});
    cut = technology.createCutLayer(TechLayerInfo{.name = "V1"}, TechCutLayer{.width = 4});
    m2 = technology.createRoutingLayer(TechLayerInfo{.name = "M2"}, TechRoutingLayer{.width = 20});

    regular = design.netlistStorage().createNet(DesignNet{.name = "signal"});
    special = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
    wire = design.routingStorage().createWire(DesignWire{.net = regular});
    diagonal = design.geometryStorage().createRouteShape(
        wire,
        DesignRouteShape{.wire = wire,
                         .layer = m1,
                         .geometry = DesignRouteSegment{.begin = {.position = {10, 20}}, .end = {.position = {40, 50}}}});

    polygon = design.geometryStorage().appendNetShape(
        special, DesignNetPolygon{.layer = m1, .points = {{10, 10}, {30, 10}, {10, 30}}});

    design_via = design.routingStorage().createVia(DesignViaInput{
        .name = "LOCAL12",
        .rectangles = std::vector<DesignViaRectangle>{
            DesignViaRectangle{.layer = TechLayerId{m1.entity()}, .rectangle = {-4, -4, 4, 4}},
            DesignViaRectangle{.layer = TechLayerId{cut.entity()}, .rectangle = {-2, -2, 2, 2}},
            DesignViaRectangle{.layer = TechLayerId{m2.entity()}, .rectangle = {-6, -6, 6, 6}},
        },
    });
    net_via = design.geometryStorage().appendNetShape(
        special, DesignNetVia{.design_via = design_via, .origins = {{100, 200}}, .orientation = DesignOrientation::kN});

    const auto io = design.netlistStorage().createIoPin(
        DesignIoPin{.name = "IN"},
        std::vector<DesignBlockPinInput>{DesignBlockPinInput{
            .flags = DesignIoPinPortFlag::kHasPlacement,
            .placement_status = DesignPlacementStatus::kPlaced,
            .origin = {100, 200},
            .orientation = DesignOrientation::kE,
            .rectangles = std::vector<DesignPinRectangle>{
                DesignPinRectangle{.layer = TechLayerId{m1.entity()}, .rectangle = {1, 2, 5, 6}},
            },
        }});
    block_pin = design.netlistStorage().blockPins(io).front();
    pin_rectangle = design.netlistStorage().blockPin(block_pin).rectangles.front();
  }

  TechStore technology;
  LibraryStore library{technology.techRegistry()};
  DesignStore design{technology.techRegistry(), library.libraryRegistry()};
  TechRoutingLayerId m1;
  TechCutLayerId cut;
  TechRoutingLayerId m2;
  DesignNetId regular;
  DesignNetId special;
  DesignWireId wire;
  DesignViaId design_via;
  DesignBlockPinId block_pin;
  GeometryShapeId diagonal;
  GeometryShapeId polygon;
  GeometryShapeId net_via;
  GeometryShapeId pin_rectangle;
};

TEST_F(GeometryPayloadServiceTest, FetchPreservesDiagonalCenterlineAndPolygonVertices)
{
  DatabaseContext context{design};
  GeometryPayloadService service(context);
  const std::array<uint64_t, 2> ids{diagonal.packed(), polygon.packed()};

  const auto batch = service.fetchShapes(ids);
  ASSERT_EQ(batch.headers.size(), 2u);
  ASSERT_EQ(batch.segments.size(), 1u);
  EXPECT_EQ(batch.segments.front().header_index, 0u);
  EXPECT_EQ(batch.segments.front().p0, (Point{10, 20}));
  EXPECT_EQ(batch.segments.front().p1, (Point{40, 50}));
  EXPECT_EQ(batch.segments.front().width, 18);
  EXPECT_DOUBLE_EQ(batch.segments.front().begin_cap, 9.0);
  EXPECT_DOUBLE_EQ(batch.segments.front().end_cap, 9.0);

  ASSERT_EQ(batch.polygons.size(), 1u);
  EXPECT_EQ(batch.polygons.front().header_index, 1u);
  EXPECT_EQ(batch.polygons.front().layer, m1.packed());
  EXPECT_EQ(batch.polygons.front().point_count, 3u);
  ASSERT_EQ(batch.polygon_points.size(), 3u);
  EXPECT_EQ(batch.polygon_points, (std::vector<Point>{{10, 10}, {30, 10}, {10, 30}}));
  EXPECT_TRUE(batch.rectangles.empty());
  // A detached read batch must not populate the native Bounds cache for every
  // returned shape. Bounds remain available through the batch header itself.
  EXPECT_EQ(design.geometryStorage().registry().tryBounds(diagonal), nullptr);
  EXPECT_EQ(design.geometryStorage().registry().tryBounds(polygon), nullptr);
}

TEST_F(GeometryPayloadServiceTest, ExpandsDesignViaAcrossPhysicalLayersAndFiltersByLayer)
{
  DatabaseContext context{design};
  GeometryPayloadService service(context);
  const std::array<uint64_t, 1> ids{net_via.packed()};

  const auto batch = service.fetchShapes(ids);
  ASSERT_EQ(batch.headers.size(), 1u);
  ASSERT_EQ(batch.vias.size(), 1u);
  EXPECT_EQ(batch.vias.front().header_index, 0u);
  EXPECT_EQ(batch.vias.front().design_via, design_via.packed());
  EXPECT_EQ(batch.vias.front().origin, (Point{100, 200}));
  ASSERT_EQ(batch.rectangles.size(), 3u);
  EXPECT_EQ(batch.rectangles[0].layer, m1.packed());
  EXPECT_EQ(batch.rectangles[0].rectangle, (Rect{96, 196, 104, 204}));
  EXPECT_EQ(batch.rectangles[1].layer, cut.packed());
  EXPECT_EQ(batch.rectangles[1].rectangle, (Rect{98, 198, 102, 202}));
  EXPECT_EQ(batch.rectangles[2].layer, m2.packed());
  EXPECT_EQ(batch.rectangles[2].rectangle, (Rect{94, 194, 106, 206}));

  GeometryQueryFilter filter;
  filter.layer = m2.packed();
  const auto m2_batch = service.queryGeometry(Rect{90, 190, 110, 210}, filter);
  ASSERT_EQ(m2_batch.headers.size(), 1u);
  EXPECT_EQ(m2_batch.headers.front().id, net_via.packed());
  ASSERT_EQ(m2_batch.vias.size(), 1u);
  ASSERT_EQ(m2_batch.rectangles.size(), 1u);
  EXPECT_EQ(m2_batch.rectangles.front().layer, m2.packed());
  EXPECT_EQ(m2_batch.rectangles.front().rectangle, (Rect{94, 194, 106, 206}));
}

TEST_F(GeometryPayloadServiceTest, FetchTransformsBlockPinGeometryToWorldCoordinates)
{
  DatabaseContext context{design};
  GeometryPayloadService service(context);
  const std::array<uint64_t, 1> ids{pin_rectangle.packed()};

  const auto batch = service.fetchShapes(ids);
  ASSERT_EQ(batch.headers.size(), 1u);
  EXPECT_EQ(batch.headers.front().owner, block_pin.packed());
  EXPECT_EQ(batch.headers.front().bounds, (Rect{102, 195, 106, 199}));
  ASSERT_EQ(batch.rectangles.size(), 1u);
  EXPECT_EQ(batch.rectangles.front().header_index, 0u);
  EXPECT_EQ(batch.rectangles.front().layer, m1.packed());
  EXPECT_EQ(batch.rectangles.front().rectangle, (Rect{102, 195, 106, 199}));
}

TEST_F(GeometryPayloadServiceTest, HitTestRejectsPolygonBoundingBoxFalsePositive)
{
  DatabaseContext context{design};
  GeometryQueryService service(context);
  GeometryQueryFilter filter;
  filter.net = special.packed();

  // This point is inside the triangle's BBox but outside its actual boundary.
  EXPECT_FALSE(service.hitTest(Point{25, 25}, 0, filter).has_value());
}

TEST_F(GeometryPayloadServiceTest, ResolvesTruncatedShapeSegmentCapsToZeroWithoutDiscardingExtensions)
{
  const auto shape_wire = design.routingStorage().createWire(DesignWire{.net = special});
  const auto shape = design.geometryStorage().createRouteShape(
      shape_wire,
      DesignRouteShape{.wire = shape_wire,
                       .layer = m1,
                       .flags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasShape,
                       .geometry = DesignRouteSegment{
                           .begin = {.position = {0, 0}, .flags = DesignWirePointFlag::kHasExtension, .extension = 17},
                           .end = {.position = {100, 0}, .flags = DesignWirePointFlag::kHasExtension, .extension = 23}}},
      DesignRouteShapeAttributes{.width = 10, .shape = "STRIPE"});
  DatabaseContext context{design};
  GeometryPayloadService service(context);
  const std::array<uint64_t, 1> ids{shape.packed()};

  const auto batch = service.fetchShapes(ids);
  ASSERT_EQ(batch.segments.size(), 1u);
  const auto& segment = batch.segments.front();
  EXPECT_DOUBLE_EQ(segment.begin_cap, 0.0);
  EXPECT_DOUBLE_EQ(segment.end_cap, 0.0);
  EXPECT_EQ(segment.begin_flags, DesignWirePointFlag::kHasExtension);
  EXPECT_EQ(segment.end_flags, DesignWirePointFlag::kHasExtension);
  EXPECT_EQ(segment.begin_extension, 17);
  EXPECT_EQ(segment.end_extension, 23);
  EXPECT_EQ(batch.headers.front().bounds, (Rect{0, -5, 100, 5}));
}

TEST_F(GeometryPayloadServiceTest, RepresentsZeroLengthSegmentAsWidthSquare)
{
  const auto zero_wire = design.routingStorage().createWire(DesignWire{.net = special});
  const auto shape = design.geometryStorage().createRouteShape(
      zero_wire,
      DesignRouteShape{.wire = zero_wire,
                       .layer = m1,
                       .flags = DesignWirePathFlag::kHasWidth,
                       .geometry = DesignRouteSegment{.begin = {.position = {40, 60}}, .end = {.position = {40, 60}}}},
      DesignRouteShapeAttributes{.width = 10});
  DatabaseContext context{design};
  GeometryPayloadService service(context);
  const std::array<uint64_t, 1> ids{shape.packed()};

  const auto batch = service.fetchShapes(ids);
  ASSERT_EQ(batch.segments.size(), 1u);
  const auto& segment = batch.segments.front();
  EXPECT_EQ(segment.p0, segment.p1);
  EXPECT_EQ(segment.width, 10);
  EXPECT_DOUBLE_EQ(segment.begin_cap, 5.0);
  EXPECT_DOUBLE_EQ(segment.end_cap, 5.0);
  EXPECT_EQ(batch.headers.front().bounds, (Rect{35, 55, 45, 65}));
}

}  // namespace
}  // namespace eccdb
