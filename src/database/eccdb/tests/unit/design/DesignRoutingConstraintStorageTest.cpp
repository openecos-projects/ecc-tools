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

#include <variant>
#include <vector>

#include "DesignTestFixture.h"
#include "design/geometry/DesignGeometryComponents.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, StoresWireShapesAndStandaloneNetGeometry)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  const auto net = netlist.createSpecialNet(DesignNet{.name = "n1"});
  const auto local_via = routing.createVia(
      DesignViaInput{.name = "LOCAL_VIA", .rectangles = {{.layer = layer, .rectangle = {-5, -5, 5, 5}}}});
  const auto geometry_via = routing.createVia(
      DesignViaInput{.name = "GEOMETRY_VIA", .rectangles = {{.layer = layer, .rectangle = {-4, -4, 4, 4}}}});

  const auto wire = routing.createWire(DesignWire{.net = net, .status = DesignWireStatus::kRouted});
  const auto segment = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .flags = DesignWirePathFlag::kHasWidth,
                       .source_path = 0,
                       .geometry = DesignRouteSegment{.begin = {.position = {0, 0}},
                                                      .end = {.position = {100, 0}}}},
      DesignRouteShapeAttributes{.width = 20});
  const auto via = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .flags = DesignWirePathFlag::kHasWidth,
                       .source_path = 0,
                       .geometry = DesignRouteVia{.origin = {.position = {100, 0}},
                                                  .via = {.design_via = local_via},
                                                  .exit_layer = routing_layer}},
      DesignRouteShapeAttributes{.width = 20});
  const auto patch = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .flags = DesignWirePathFlag::kHasWidth,
                       .source_path = 1,
                       .geometry = DesignRoutePatch{.anchor = {.position = {120, 0}},
                                                    .rectangle = {118, -2, 122, 2}}},
      DesignRouteShapeAttributes{.width = 20});

  ASSERT_EQ(routing.wire(wire).shapes, (std::vector<DesignRouteShapeId>{segment, via, patch}));
  EXPECT_EQ(routing.shape(segment).source_path, 0u);
  EXPECT_EQ(routing.shape(patch).source_path, 1u);
  EXPECT_EQ(routing.shapeBoundingBox(segment), (Rect{-10, -10, 110, 10}));
  EXPECT_EQ(routing.shapeBoundingBox(patch), (Rect{118, -2, 122, 2}));

  auto& geometry = design.geometryStorage();
  const auto special_rectangle
      = geometry.appendNetShape(net, DesignNetRectangle{.layer = routing_layer, .rectangle = {0, 0, 20, 20}});
  const auto special_polygon = geometry.appendNetShape(
      net, DesignNetPolygon{.layer = routing_layer, .points = {{0, 0}, {20, 0}, {20, 20}}});
  const auto special_via
      = geometry.appendNetShape(net, DesignNetVia{.design_via = geometry_via, .origins = {{10, 10}, {30, 10}}});
  ASSERT_NE(routing.netGeometry(net), nullptr);
  ASSERT_EQ(routing.netGeometry(net)->shapes.size(), 3u);
  EXPECT_EQ(geometry.registry().get<const DesignGeometryNetVia>(special_via).origins.size(), 2u);
  EXPECT_FALSE(routing.destroyVia(local_via));
  EXPECT_FALSE(routing.destroyVia(geometry_via));

  EXPECT_TRUE(routing.destroyWire(wire));
  EXPECT_TRUE(geometry.destroy(special_rectangle));
  EXPECT_TRUE(geometry.destroy(special_polygon));
  EXPECT_TRUE(geometry.destroy(special_via));
  EXPECT_TRUE(routing.destroyVia(local_via));
  EXPECT_TRUE(netlist.destroyNet(net));
  EXPECT_TRUE(routing.destroyVia(geometry_via));
}

TEST_F(DesignStorageTest, ShapeCrudUpdatesGeometryWithoutDroppingAttributes)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  const auto net = netlist.createSpecialNet(DesignNet{.name = "signal"});
  const auto wire = routing.createWire(DesignWire{.net = net});
  const auto shape = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .flags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask,
                       .geometry = DesignRouteSegment{.begin = {.position = {10, 20}},
                                                      .end = {.position = {30, 20}}}},
      DesignRouteShapeAttributes{.width = 20, .mask = 3});

  auto edited = routing.shape(shape);
  std::get<DesignRouteSegment>(edited.geometry).end.position = {50, 20};
  routing.updateShape(shape, std::move(edited));
  EXPECT_EQ(std::get<DesignRouteSegment>(routing.shape(shape).geometry).end.position, (Point{50, 20}));
  ASSERT_NE(routing.shapeAttributes(shape), nullptr);
  EXPECT_EQ(routing.shapeAttributes(shape)->width, 20);
  EXPECT_EQ(routing.shapeAttributes(shape)->mask, 3u);
  EXPECT_EQ(routing.shapeBoundingBox(shape), (Rect{0, 10, 60, 30}));

  EXPECT_TRUE(routing.destroyShape(shape));
  EXPECT_FALSE(routing.contains(shape));
  EXPECT_TRUE(routing.destroyWire(wire));
  EXPECT_TRUE(netlist.destroyNet(net));
}

TEST_F(DesignStorageTest, ViaShapeKeepsDesignViaAliveUntilWireIsDestroyed)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  const auto net = netlist.createNet(DesignNet{.name = "signal"});
  const auto design_via = routing.createVia(
      DesignViaInput{.name = "WIRE_VIA", .rectangles = {{.layer = layer, .rectangle = {-2, -2, 2, 2}}}});
  const auto wire = routing.createWire(DesignWire{.net = net});
  const auto shape = routing.createShape(wire,
                                         DesignRouteShape{.wire = wire,
                                                          .layer = routing_layer,
                                                          .geometry = DesignRouteVia{.origin = {.position = {100, 200}},
                                                                                     .via = {.design_via = design_via},
                                                                                     .exit_layer = routing_layer}});
  EXPECT_TRUE(routing.contains(shape));

  EXPECT_FALSE(routing.destroyVia(design_via));
  EXPECT_TRUE(routing.destroyWire(wire));
  EXPECT_TRUE(routing.destroyVia(design_via));
  EXPECT_TRUE(netlist.destroyNet(net));
}

}  // namespace
}  // namespace eccdb
