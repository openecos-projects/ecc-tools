// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "service/routing/NetRouteService.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

class NetRouteServiceTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    m1 = technology.createRoutingLayer(TechLayerInfo{.name = "M1"});
    v1 = technology.createCutLayer(TechLayerInfo{.name = "V1"});
    m2 = technology.createRoutingLayer(TechLayerInfo{.name = "M2"});
    v2 = technology.createCutLayer(TechLayerInfo{.name = "V2"});
    m3 = technology.createRoutingLayer(TechLayerInfo{.name = "M3"});

    via12 = makeVia("VIA12", m1, v1, m2);
    via23 = makeVia("VIA23", m2, v2, m3);
    net = design.netlistStorage().createNet(DesignNet{.name = "signal"});
  }

  [[nodiscard]] TechViaMasterId makeVia(std::string name, TechRoutingLayerId bottom, TechCutLayerId cut,
                                        TechRoutingLayerId top)
  {
    return technology.viaMasterStorage().createFixedViaMaster(
        TechViaMaster{.name = std::move(name)},
        TechViaMasterShapeInput{.bottom_layer = bottom,
                                 .bottom_geometry = {.rects = {{.ll_x = -2, .ll_y = -2, .ur_x = 2, .ur_y = 2}}},
                                 .cut_layer = cut,
                                 .cut_geometry = {.rects = {{.ll_x = -1, .ll_y = -1, .ur_x = 1, .ur_y = 1}}},
                                 .top_layer = top,
                                 .top_geometry = {.rects = {{.ll_x = -2, .ll_y = -2, .ur_x = 2, .ur_y = 2}}}});
  }

  [[nodiscard]] DesignWireId addWire(DesignWireStatus status, std::vector<DesignWirePath> paths, std::string shield_net = {})
  {
    DesignWireRoutingInput routing;
    routing.reservePaths(paths.size());
    for (auto& path : paths) {
      routing.appendPath(std::move(path));
    }
    return design.routingStorage().createWire(
        DesignWire{.net = net, .status = status, .shield_net = std::move(shield_net)}, std::move(routing));
  }

  TechStore technology;
  LibraryStore library{technology.techRegistry()};
  DesignStore design{technology.techRegistry(), library.libraryRegistry()};
  TechRoutingLayerId m1;
  TechRoutingLayerId m2;
  TechRoutingLayerId m3;
  TechCutLayerId v1;
  TechCutLayerId v2;
  TechViaMasterId via12;
  TechViaMasterId via23;
  DesignNetId net;
};

TEST_F(NetRouteServiceTest, ReadsSegmentViaPatchWithLayerTransitions)
{
  const auto wire = addWire(
      DesignWireStatus::kRouted,
      {DesignWirePath{.layer = m1,
                      .points = {DesignWirePoint{.position = {0, 0},
                                                  .flags = DesignWirePointFlag::kHasExtension,
                                                  .extension = 4},
                                 DesignWirePoint{.position = {100, 0},
                                                  .flags = DesignWirePointFlag::kHasExtension,
                                                  .extension = 6},
                                 DesignWirePoint{.position = {100, 80}}},
                      .vias = {{.point_index = 1u, .tech_via = via12}},
                      .rectangles = {{.point_index = 2u, .delta = {-3, -4, 5, 6}}}}});

  NetRouteService service{design, technology};
  const auto snapshot = service.snapshot(net);

  ASSERT_EQ(snapshot.net, net);
  ASSERT_EQ(snapshot.wires.size(), 1u);
  const auto& route = snapshot.wires.front();
  EXPECT_EQ(route.wire, wire);
  EXPECT_EQ(route.status, DesignWireStatus::kRouted);
  ASSERT_EQ(route.shapes.size(), 4u);

  const auto& first_segment = std::get<RouteSegment>(route.shapes[0]);
  EXPECT_EQ(first_segment.layer, m1);
  EXPECT_EQ(first_segment.begin, (Point{0, 0}));
  EXPECT_EQ(first_segment.end, (Point{100, 0}));
  EXPECT_EQ(first_segment.begin_extension, 4);
  EXPECT_EQ(first_segment.end_extension, 6);

  const auto& via = std::get<RouteVia>(route.shapes[1]);
  EXPECT_EQ(std::get<TechViaMasterId>(via.definition), via12);
  EXPECT_EQ(via.origin, (Point{100, 0}));

  const auto& second_segment = std::get<RouteSegment>(route.shapes[2]);
  EXPECT_EQ(second_segment.layer, m2);
  EXPECT_EQ(second_segment.begin, (Point{100, 0}));
  EXPECT_EQ(second_segment.end, (Point{100, 80}));
  EXPECT_EQ(second_segment.begin_extension, 6);
  EXPECT_FALSE(second_segment.end_extension.has_value());

  const auto& patch = std::get<RoutePatch>(route.shapes[3]);
  EXPECT_EQ(patch.layer, m2);
  EXPECT_EQ(patch.rectangle, (Rect{-3 + 100, -4 + 80, 5 + 100, 6 + 80}));
}

TEST_F(NetRouteServiceTest, ReplacesOnlyRoutedWiresAndCanonicalizesMixedShapes)
{
  const auto fixed = addWire(DesignWireStatus::kFixed,
                             {DesignWirePath{.layer = m1,
                                             .points = {DesignWirePoint{.position = {0, 20}}, DesignWirePoint{.position = {30, 20}}}}});
  const auto old_routed = addWire(DesignWireStatus::kRouted,
                                  {DesignWirePath{.layer = m1,
                                                  .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {10, 0}}}}});

  const std::vector<RouteShape> replacement_shapes{
      RouteSegment{.layer = m1, .begin = {10, 0}, .end = {30, 0}, .begin_extension = 2, .end_extension = 4},
      RouteVia{.definition = RouteViaDefinition{via12}, .origin = {30, 0}, .orientation = DesignOrientation::kN},
      RouteSegment{.layer = m2, .begin = {30, 0}, .end = {30, 40}},
      RoutePatch{.layer = m2, .rectangle = {26, 36, 34, 44}}};

  NetRouteService service{design, technology};
  const auto replacement = service.replaceRouted(net, std::span<const RouteShape>{replacement_shapes});

  EXPECT_TRUE(replacement);
  EXPECT_FALSE(design.routingStorage().contains(old_routed));
  EXPECT_TRUE(design.routingStorage().contains(fixed));
  EXPECT_EQ(design.routingStorage().wire(fixed).status, DesignWireStatus::kFixed);
  EXPECT_EQ(design.routingStorage().pathCount(replacement), replacement_shapes.size());

  const auto snapshot = service.snapshot(net);
  ASSERT_EQ(snapshot.wires.size(), 2u);
  EXPECT_EQ(snapshot.wires[0].wire, fixed);
  EXPECT_EQ(snapshot.wires[0].status, DesignWireStatus::kFixed);
  EXPECT_EQ(snapshot.wires[1].wire, replacement);
  EXPECT_EQ(snapshot.wires[1].status, DesignWireStatus::kRouted);
  ASSERT_EQ(snapshot.wires[1].shapes.size(), replacement_shapes.size());
  const auto& replacement_segment = std::get<RouteSegment>(snapshot.wires[1].shapes[0]);
  EXPECT_EQ(replacement_segment.layer, m1);
  EXPECT_EQ(replacement_segment.begin_extension, 2);
  EXPECT_EQ(replacement_segment.end_extension, 4);
  EXPECT_EQ(std::get<RouteVia>(snapshot.wires[1].shapes[1]).origin, (Point{30, 0}));
  EXPECT_EQ(std::get<RouteSegment>(snapshot.wires[1].shapes[2]).layer, m2);
  EXPECT_EQ(std::get<RoutePatch>(snapshot.wires[1].shapes[3]).rectangle, (Rect{26, 36, 34, 44}));

  const std::vector<RouteShape> empty;
  EXPECT_FALSE(service.replaceRouted(net, std::span<const RouteShape>{empty}));
  ASSERT_EQ(design.routingStorage().wireIds(net).size(), 1u);
  EXPECT_EQ(design.routingStorage().wireIds(net).front(), fixed);
}

TEST_F(NetRouteServiceTest, PreservesShieldWireMetadataAndLeavesItUntouched)
{
  const auto shield = addWire(DesignWireStatus::kShield,
                              {DesignWirePath{.layer = m1,
                                              .points = {DesignWirePoint{.position = {0, 20}},
                                                         DesignWirePoint{.position = {30, 20}}}}},
                              "VSS");
  const auto routed = addWire(DesignWireStatus::kRouted,
                              {DesignWirePath{.layer = m1,
                                              .points = {DesignWirePoint{.position = {0, 0}},
                                                         DesignWirePoint{.position = {30, 0}}}}});

  NetRouteService service{design, technology};
  const auto snapshot = service.snapshot(net);
  ASSERT_EQ(snapshot.wires.size(), 2u);
  EXPECT_EQ(snapshot.wires[0].wire, shield);
  EXPECT_EQ(snapshot.wires[0].status, DesignWireStatus::kShield);
  EXPECT_EQ(snapshot.wires[0].shield_net, "VSS");

  const std::vector<RouteShape> empty;
  EXPECT_FALSE(service.replaceRouted(net, std::span<const RouteShape>{empty}));
  EXPECT_TRUE(design.routingStorage().contains(shield));
  EXPECT_FALSE(design.routingStorage().contains(routed));
  EXPECT_EQ(design.routingStorage().wire(shield).shield_net, "VSS");
}

TEST_F(NetRouteServiceTest, RejectsUnprojectablePathAndViaAttributesBeforeReplacement)
{
  NetRouteService service{design, technology};
  const std::vector<RouteShape> replacement{
      RouteSegment{.layer = m1, .begin = {10, 0}, .end = {30, 0}}};
  std::size_t index = 0u;
  const auto expect_rejected = [&](DesignWirePath path) {
    const auto unsupported_net =
        design.netlistStorage().createNet(DesignNet{.name = "unprojectable_" + std::to_string(index++)});
    DesignWireRoutingInput routing;
    routing.appendPath(std::move(path));
    const auto old_wire = design.routingStorage().createWireTrusted(
        DesignWire{.net = unsupported_net, .status = DesignWireStatus::kRouted}, std::move(routing));

    EXPECT_THROW((void) service.snapshot(unsupported_net), std::logic_error);
    EXPECT_THROW((void) service.replaceRouted(unsupported_net, std::span<const RouteShape>{replacement}), std::logic_error);
    ASSERT_EQ(design.routingStorage().wireIds(unsupported_net).size(), 1u);
    EXPECT_EQ(design.routingStorage().wireIds(unsupported_net).front(), old_wire);
  };

  expect_rejected(DesignWirePath{.layer = m1,
                                 .flags = DesignWirePathFlag::kHasWidth,
                                 .width = 1,
                                 .points = {{.position = {0, 0}}, {.position = {10, 0}}}});
  expect_rejected(DesignWirePath{.layer = m1,
                                 .flags = DesignWirePathFlag::kHasMask,
                                 .mask = 1u,
                                 .points = {{.position = {0, 0}}, {.position = {10, 0}}}});
  expect_rejected(DesignWirePath{.layer = m1,
                                 .flags = DesignWirePathFlag::kTaper,
                                 .points = {{.position = {0, 0}}, {.position = {10, 0}}}});
  expect_rejected(DesignWirePath{.layer = m1,
                                 .flags = DesignWirePathFlag::kHasTaperRule,
                                 .taper_rule = "route_rule",
                                 .points = {{.position = {0, 0}}, {.position = {10, 0}}}});
  expect_rejected(DesignWirePath{.layer = m1,
                                 .flags = DesignWirePathFlag::kHasShape,
                                 .shape = "STRIPE",
                                 .points = {{.position = {0, 0}}, {.position = {10, 0}}}});
  expect_rejected(DesignWirePath{.layer = m1,
                                 .flags = DesignWirePathFlag::kHasStyle,
                                 .style = 1,
                                 .points = {{.position = {0, 0}}, {.position = {10, 0}}}});
  expect_rejected(DesignWirePath{.layer = m1,
                                 .points = {{.position = {0, 0}}},
                                 .vias = {{.point_index = 0u,
                                           .tech_via = via12,
                                           .flags = DesignWireViaFlag::kHasMask,
                                           .top_mask = 1u}}});
  expect_rejected(DesignWirePath{.layer = m1,
                                 .points = {{.position = {0, 0}}},
                                 .vias = {{.point_index = 0u,
                                           .tech_via = via12,
                                           .flags = DesignWireViaFlag::kHasArray,
                                           .rows = 2u,
                                           .columns = 3u,
                                           .step_x = 10,
                                           .step_y = 20}}});
  expect_rejected(DesignWirePath{.layer = m1,
                                 .points = {{.position = {0, 0}, .flags = DesignWirePointFlag::kVirtual}}});
  expect_rejected(DesignWirePath{.layer = m1,
                                 .points = {{.position = {0, 0},
                                             .flags = DesignWirePointFlag::kHasExtension,
                                             .extension = 4}},
                                 .vias = {{.point_index = 0u, .tech_via = via12}}});
}

TEST_F(NetRouteServiceTest, RejectsInvalidViaLayerTransition)
{
  const auto wire = addWire(DesignWireStatus::kRouted,
                            {DesignWirePath{.layer = m1,
                                            .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {20, 0}}},
                                            .vias = {{.point_index = 1u, .tech_via = via23}}}});

  NetRouteService service{design, technology};
  EXPECT_THROW((void) service.snapshot(net), std::logic_error);
  const std::vector<RouteShape> replacement{RouteSegment{.layer = m1, .begin = {0, 0}, .end = {20, 0}}};
  EXPECT_THROW((void) service.replaceRouted(net, std::span<const RouteShape>{replacement}), std::logic_error);
  EXPECT_TRUE(design.routingStorage().contains(wire));
}

TEST_F(NetRouteServiceTest, ReadsUnorderedViasWithStableSameAnchorOrder)
{
  const auto wire = addWire(
      DesignWireStatus::kRouted,
      {DesignWirePath{.layer = m1,
                      .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {10, 0}},
                                 DesignWirePoint{.position = {20, 0}}, DesignWirePoint{.position = {30, 0}}},
                      .vias = {{.point_index = 2u, .tech_via = via23, .orientation = DesignOrientation::kS},
                               {.point_index = 1u, .tech_via = via12, .orientation = DesignOrientation::kE},
                               {.point_index = 2u, .tech_via = via23, .orientation = DesignOrientation::kW}}}});

  NetRouteService service{design, technology};
  const auto snapshot = service.snapshot(net);

  ASSERT_EQ(snapshot.wires.size(), 1u);
  EXPECT_EQ(snapshot.wires.front().wire, wire);
  const auto& shapes = snapshot.wires.front().shapes;
  ASSERT_EQ(shapes.size(), 6u);
  EXPECT_EQ(std::get<RouteSegment>(shapes[0]).layer, m1);
  EXPECT_EQ(std::get<RouteSegment>(shapes[0]).end, (Point{10, 0}));
  EXPECT_EQ(std::get<RouteVia>(shapes[1]).orientation, DesignOrientation::kE);
  EXPECT_EQ(std::get<TechViaMasterId>(std::get<RouteVia>(shapes[1]).definition), via12);
  EXPECT_EQ(std::get<RouteSegment>(shapes[2]).layer, m2);
  EXPECT_EQ(std::get<RouteVia>(shapes[3]).orientation, DesignOrientation::kS);
  EXPECT_EQ(std::get<RouteVia>(shapes[4]).orientation, DesignOrientation::kW);
  EXPECT_EQ(std::get<TechViaMasterId>(std::get<RouteVia>(shapes[3]).definition), via23);
  EXPECT_EQ(std::get<TechViaMasterId>(std::get<RouteVia>(shapes[4]).definition), via23);
  EXPECT_EQ(std::get<RouteSegment>(shapes[5]).layer, m2);
}

TEST_F(NetRouteServiceTest, ReadsUnorderedPatchesWithStableSameAnchorOrder)
{
  const auto wire = addWire(
      DesignWireStatus::kRouted,
      {DesignWirePath{.layer = m1,
                      .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {10, 0}},
                                 DesignWirePoint{.position = {20, 0}}, DesignWirePoint{.position = {30, 0}}},
                      .rectangles = {{.point_index = 2u, .delta = {-6, -2, -4, 2}},
                                     {.point_index = 1u, .delta = {-2, -3, 0, 3}},
                                     {.point_index = 2u, .delta = {4, -1, 6, 1}}}}});

  NetRouteService service{design, technology};
  const auto snapshot = service.snapshot(net);

  ASSERT_EQ(snapshot.wires.size(), 1u);
  EXPECT_EQ(snapshot.wires.front().wire, wire);
  const auto& shapes = snapshot.wires.front().shapes;
  ASSERT_EQ(shapes.size(), 6u);
  EXPECT_EQ(std::get<RouteSegment>(shapes[0]).end, (Point{10, 0}));
  EXPECT_EQ(std::get<RoutePatch>(shapes[1]).rectangle, (Rect{8, -3, 10, 3}));
  EXPECT_EQ(std::get<RouteSegment>(shapes[2]).end, (Point{20, 0}));
  EXPECT_EQ(std::get<RoutePatch>(shapes[3]).rectangle, (Rect{14, -2, 16, 2}));
  EXPECT_EQ(std::get<RoutePatch>(shapes[4]).rectangle, (Rect{24, -1, 26, 1}));
  EXPECT_EQ(std::get<RouteSegment>(shapes[5]).end, (Point{30, 0}));
}

TEST_F(NetRouteServiceTest, RejectsTrustedOutOfRangePrimitiveAnchors)
{
  NetRouteService service{design, technology};
  const std::vector<RouteShape> replacement{RouteSegment{.layer = m1, .begin = {0, 0}, .end = {20, 0}}};
  const auto expect_rejected = [&](std::string name, DesignWirePath path) {
    const auto invalid_net = design.netlistStorage().createNet(DesignNet{.name = std::move(name)});
    DesignWireRoutingInput routing;
    routing.appendPath(std::move(path));
    const auto wire = design.routingStorage().createWireTrusted(
        DesignWire{.net = invalid_net, .status = DesignWireStatus::kRouted}, std::move(routing));

    EXPECT_THROW((void) service.snapshot(invalid_net), std::logic_error);
    EXPECT_THROW((void) service.replaceRouted(invalid_net, std::span<const RouteShape>{replacement}), std::logic_error);
    ASSERT_EQ(design.routingStorage().wireIds(invalid_net).size(), 1u);
    EXPECT_EQ(design.routingStorage().wireIds(invalid_net).front(), wire);
  };

  expect_rejected("out_of_range_via",
                  DesignWirePath{.layer = m1,
                                 .points = {{.position = {0, 0}}},
                                 .vias = {{.point_index = 1u, .tech_via = via12}}});
  expect_rejected("out_of_range_patch",
                  DesignWirePath{.layer = m1,
                                 .points = {{.position = {0, 0}}},
                                 .rectangles = {{.point_index = 1u, .delta = {-1, -1, 1, 1}}}});
}

TEST_F(NetRouteServiceTest, ReadsMixedPathsAndRejectsAmbiguousPatchViaAnchor)
{
  const auto mixed = addWire(DesignWireStatus::kRouted,
                             {DesignWirePath{.layer = m1,
                                             .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {20, 0}}}},
                              DesignWirePath{.layer = m2,
                                             .points = {DesignWirePoint{.position = {40, 0}}, DesignWirePoint{.position = {40, 20}}}}});

  NetRouteService service{design, technology};
  const auto snapshot = service.snapshot(net);
  ASSERT_EQ(snapshot.wires.size(), 1u);
  EXPECT_EQ(snapshot.wires.front().wire, mixed);
  ASSERT_EQ(snapshot.wires.front().shapes.size(), 2u);
  EXPECT_EQ(std::get<RouteSegment>(snapshot.wires.front().shapes[0]).layer, m1);
  EXPECT_EQ(std::get<RouteSegment>(snapshot.wires.front().shapes[1]).layer, m2);

  const auto ambiguous_net = design.netlistStorage().createNet(DesignNet{.name = "ambiguous"});
  DesignWireRoutingInput routing;
  routing.appendPath(DesignWirePath{.layer = m1,
                                     .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {20, 0}},
                                                DesignWirePoint{.position = {40, 0}}},
                                     .vias = {{.point_index = 2u, .tech_via = via12}, {.point_index = 1u, .tech_via = via12}},
                                     .rectangles = {{.point_index = 2u, .delta = {-1, -1, 1, 1}}}});
  const auto ambiguous_wire =
      design.routingStorage().createWire(DesignWire{.net = ambiguous_net, .status = DesignWireStatus::kRouted}, std::move(routing));
  EXPECT_THROW((void) service.snapshot(ambiguous_net), std::logic_error);
  const std::vector<RouteShape> replacement{RouteSegment{.layer = m1, .begin = {0, 0}, .end = {20, 0}}};
  EXPECT_THROW((void) service.replaceRouted(ambiguous_net, std::span<const RouteShape>{replacement}), std::logic_error);
  EXPECT_TRUE(design.routingStorage().contains(ambiguous_wire));
}

}  // namespace
}  // namespace eccdb
