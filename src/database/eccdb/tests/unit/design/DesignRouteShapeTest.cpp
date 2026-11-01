// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <variant>
#include <vector>

#include "DesignTestFixture.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, ShapeCrudKeepsIdsBoundingBoxesAndWireOwnership)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  const auto net = netlist.createSpecialNet(DesignNet{.name = "VDD"});
  const auto wire = routing.createWire(DesignWire{.net = net, .status = DesignWireStatus::kFixed});

  const auto segment = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .flags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask,
                       .geometry = DesignRouteSegment{
                           .begin = {.position = {10, 20}, .flags = DesignWirePointFlag::kHasExtension, .extension = 2},
                           .end = {.position = {30, 20}, .flags = DesignWirePointFlag::kHasExtension, .extension = 4}}},
      DesignRouteShapeAttributes{.width = 20, .mask = 3});
  const auto patch = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .flags = DesignWirePathFlag::kHasWidth,
                       .geometry = DesignRoutePatch{.anchor = {.position = {40, 50}}, .rectangle = {35, 45, 55, 65}}},
      DesignRouteShapeAttributes{.width = 20});
  const auto point = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .flags = DesignWirePathFlag::kHasWidth,
                       .geometry = DesignRoutePoint{.point = {.position = {60, 70}, .flags = DesignWirePointFlag::kVirtual}}},
      DesignRouteShapeAttributes{.width = 20});

  ASSERT_EQ(routing.wire(wire).shapes, (std::vector<DesignRouteShapeId>{segment, patch, point}));
  ASSERT_TRUE(routing.contains(segment));
  ASSERT_TRUE(routing.contains(patch));
  ASSERT_NE(routing.shapeAttributes(segment), nullptr);
  EXPECT_EQ(routing.shapeAttributes(segment)->width, 20);
  EXPECT_EQ(routing.shapeAttributes(segment)->mask, 3u);
  EXPECT_EQ(routing.shapeBoundingBox(segment), (Rect{8, 10, 34, 30}));
  EXPECT_EQ(routing.shapeBoundingBox(patch), (Rect{35, 45, 55, 65}));

  auto updated = routing.shape(segment);
  auto& updated_segment = std::get<DesignRouteSegment>(updated.geometry);
  updated_segment.end.position = {50, 20};
  routing.updateShape(segment, std::move(updated));

  EXPECT_EQ(routing.shape(segment).wire, wire);
  EXPECT_EQ(std::get<DesignRouteSegment>(routing.shape(segment).geometry).end.position, (Point{50, 20}));
  ASSERT_NE(routing.shapeAttributes(segment), nullptr);
  EXPECT_EQ(routing.shapeAttributes(segment)->width, 20);
  EXPECT_EQ(routing.shapeAttributes(segment)->mask, 3u);
  EXPECT_EQ(routing.shapeBoundingBox(segment), (Rect{8, 10, 54, 30}));
  EXPECT_EQ(routing.shapeBoundingBox(patch), (Rect{35, 45, 55, 65}));
  EXPECT_TRUE(routing.contains(patch));
  EXPECT_TRUE(routing.contains(point));
  ASSERT_EQ(routing.wire(wire).shapes, (std::vector<DesignRouteShapeId>{segment, patch, point}));

  EXPECT_TRUE(routing.destroyShape(patch));
  EXPECT_FALSE(routing.contains(patch));
  ASSERT_EQ(routing.wire(wire).shapes, (std::vector<DesignRouteShapeId>{segment, point}));

  EXPECT_TRUE(routing.destroyWire(wire));
  EXPECT_FALSE(routing.contains(wire));
  EXPECT_FALSE(routing.contains(segment));
  EXPECT_FALSE(routing.contains(point));
  EXPECT_TRUE(netlist.destroyNet(net));
}

TEST_F(DesignStorageTest, SpecialNetSegmentsUseFlushDefaultsAndHonorExplicitExtensions)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  const auto special = netlist.createSpecialNet(DesignNet{.name = "VSS"});
  const auto wire = routing.createWire(DesignWire{.net = special, .status = DesignWireStatus::kFixed});

  // SPECIALNETS have flush endpoints unless a point explicitly supplies an
  // extension.  Reverse point order must preserve that endpoint association.
  const auto flush_reversed = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .flags = DesignWirePathFlag::kHasWidth,
                       .geometry = DesignRouteSegment{.begin = {.position = {100, 200}}, .end = {.position = {0, 200}}}},
      DesignRouteShapeAttributes{.width = 20});
  EXPECT_EQ(routing.shapeBoundingBox(flush_reversed), (Rect{0, 190, 100, 210}));

  // SHAPE does not discard an explicit extension.  The zero extension on the
  // lower endpoint is intentional: it must remain flush rather than fall
  // back to half the width.
  const auto stripe_with_extensions = routing.createShape(
      wire,
      DesignRouteShape{
          .wire = wire,
          .layer = routing_layer,
          .flags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasShape,
          .geometry = DesignRouteSegment{.begin = {.position = {100, 10},
                                                    .flags = DesignWirePointFlag::kHasExtension,
                                                    .extension = 7},
                                         .end = {.position = {0, 10},
                                                 .flags = DesignWirePointFlag::kHasExtension,
                                                 .extension = 0}}},
      DesignRouteShapeAttributes{.width = 20, .shape = "STRIPE"});
  EXPECT_EQ(routing.shapeBoundingBox(stripe_with_extensions), (Rect{0, 0, 107, 20}));
}

TEST_F(DesignStorageTest, RegularNetSegmentsResolveTheirDesignNdrWidth)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  const auto ndr = routing.createNonDefaultRule(
      DesignNonDefaultRule{.name = "WIDE", .layer_rules = {{.layer = TechLayerId{routing_layer.entity()}, .width = 40}}});
  const auto net = netlist.createNet(
      DesignNet{.name = "signal", .flags = DesignNetFlag::kHasNonDefaultRule, .design_non_default_rule = ndr});
  const auto wire = routing.createWire(DesignWire{.net = net});
  const auto segment = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .geometry = DesignRouteSegment{.begin = {.position = {0, 0}}, .end = {.position = {100, 0}}}});

  // A regular NET without an explicit WIDTH uses the design NDR width and
  // its ordinary half-width endpoint extensions.
  EXPECT_EQ(routing.shapeBoundingBox(segment), (Rect{-20, -20, 120, 20}));
}

TEST_F(DesignStorageTest, ShapesKeepLayerTransitionsAndDetectDisconnectedEdit)
{
  const auto m2_entity = tech.registry().create();
  tech.registry().emplace<TechLayerInfo>(m2_entity, TechLayerInfo{.name = "M2"});
  tech.registry().emplace<TechRoutingLayer>(m2_entity, TechRoutingLayer{});
  const auto m2 = TechRoutingLayerId{m2_entity};

  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  const auto net = netlist.createNet(DesignNet{.name = "signal"});
  const auto wire = routing.createWire(DesignWire{.net = net});

  const auto first_segment = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .source_path = 0,
                       .geometry = DesignRouteSegment{.begin = {.position = {0, 0}}, .end = {.position = {100, 0}}}});
  const auto via = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = routing_layer,
                       .source_path = 0,
                       .geometry = DesignRouteVia{.origin = {.position = {100, 0}},
                                                  .via = {.tech_via = tech_via},
                                                  .exit_layer = m2}});
  const auto second_segment = routing.createShape(
      wire,
      DesignRouteShape{.wire = wire,
                       .layer = m2,
                       .source_path = 0,
                       .geometry = DesignRouteSegment{.begin = {.position = {100, 0}}, .end = {.position = {100, 100}}}});

  ASSERT_EQ(routing.wire(wire).shapes.size(), 3u);
  EXPECT_EQ(routing.shape(first_segment).source_path, 0u);
  EXPECT_EQ(routing.shape(via).source_path, 0u);
  EXPECT_EQ(routing.shape(second_segment).source_path, 0u);
  EXPECT_EQ(routing.shape(via).layer, routing_layer);
  EXPECT_EQ(std::get<DesignRouteVia>(routing.shape(via).geometry).exit_layer, m2);

  auto disconnected = routing.shape(second_segment);
  std::get<DesignRouteSegment>(disconnected.geometry).begin.position = {200, 0};
  routing.updateShape(second_segment, std::move(disconnected));

  EXPECT_EQ(routing.wire(wire).shapes.size(), 3u);
  EXPECT_EQ(std::get<DesignRouteSegment>(routing.shape(second_segment).geometry).begin.position, (Point{200, 0}));
  EXPECT_EQ(routing.shape(second_segment).layer, m2);
  EXPECT_NE(std::get<DesignRouteSegment>(routing.shape(first_segment).geometry).end.position,
            std::get<DesignRouteSegment>(routing.shape(second_segment).geometry).begin.position);
  EXPECT_TRUE(routing.contains(first_segment));
  EXPECT_TRUE(routing.contains(via));
  EXPECT_TRUE(routing.contains(second_segment));
}

}  // namespace
}  // namespace eccdb
