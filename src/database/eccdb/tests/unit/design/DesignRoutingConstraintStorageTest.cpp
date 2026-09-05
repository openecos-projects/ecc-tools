// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>

#include "DesignTestFixture.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, StoresWirePathsWithTechAndDesignViaReferences)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();

  const auto net = netlist.createSpecialNet(DesignNet{.name = "n1"});
  const auto local_via = routing.createVia(DesignVia{.name = "LOCAL_VIA", .rectangles = {{.layer = layer, .rectangle = {-5, -5, 5, 5}}}});
  const auto geometry_via
      = routing.createVia(DesignVia{.name = "GEOMETRY_VIA", .rectangles = {{.layer = layer, .rectangle = {-4, -4, 4, 4}}}});
  DesignWire wire_value{.net = net, .status = DesignWireStatus::kRouted};
  DesignWireRoutingInput wire_routing;
  wire_routing.appendPath(DesignWirePath{.layer = routing_layer,
                                         .flags = DesignWirePathFlag::kHasWidth,
                                         .width = 20,
                                         .points = {{{0, 0}}, {{100, 0}}, {{100, 100}}},
                                         .vias = {{.point_index = 1, .tech_via = tech_via},
                                                  {.point_index = 2, .design_via = local_via}}});
  const auto wire = routing.createWire(std::move(wire_value), std::move(wire_routing));
  routing.setNetGeometry(net, DesignNetGeometry{.rectangles = {{.layer = routing_layer,
                                                                .rectangle = {0, 0, 20, 20},
                                                                .route_status = DesignWireStatus::kFixed,
                                                                .flags = DesignNetGeometryFlag::kHasMask,
                                                                .mask = 2}},
                                                .polygons = {{.layer = routing_layer, .points = {{0, 0}, {20, 0}, {20, 20}, {0, 20}}}},
                                                .vias = {{.design_via = geometry_via, .origins = {{10, 10}, {30, 10}}}}});

  ASSERT_EQ(routing.wires(net).size(), 1u);
  EXPECT_EQ(routing.wires(net).front(), wire);
  std::vector<DesignWireId> visited_wires;
  routing.forEachWire(net, [&](DesignWireId id, const DesignWire& value) {
    visited_wires.push_back(id);
    EXPECT_EQ(value.net, net);
  });
  EXPECT_EQ(visited_wires, (std::vector<DesignWireId>{wire}));
  EXPECT_EQ(routing.path(wire, 0).points().size(), 3u);
  EXPECT_EQ(routing.routingPoolStatistics().paths.count, 1u);
  EXPECT_EQ(routing.routingPoolStatistics().points.count, 3u);
  EXPECT_EQ(routing.via(local_via).name, "LOCAL_VIA");
  const auto* geometry = routing.netGeometry(net);
  ASSERT_NE(geometry, nullptr);
  EXPECT_EQ(geometry->rectangles.size(), 1u);
  EXPECT_EQ(geometry->polygons.size(), 1u);
  EXPECT_EQ(geometry->vias.front().origins.size(), 2u);
  EXPECT_FALSE(routing.destroyVia(local_via));
  EXPECT_FALSE(routing.destroyVia(geometry_via));
  EXPECT_FALSE(netlist.destroyNet(net));

  EXPECT_TRUE(routing.destroyWire(wire));
  EXPECT_TRUE(routing.destroyVia(local_via));
  EXPECT_TRUE(netlist.destroyNet(net));
  EXPECT_TRUE(routing.destroyVia(geometry_via));
}

TEST_F(DesignStorageTest, AcceptsOpenDbStyleZeroWidthSpecialNetViaPath)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  const auto net = netlist.createSpecialNet(DesignNet{.name = "VDD"});

  DesignWire wire_value{.net = net};
  DesignWireRoutingInput wire_routing;
  wire_routing.appendPath(DesignWirePath{.layer = routing_layer,
                                         .flags = DesignWirePathFlag::kHasWidth,
                                         .width = 0,
                                         .points = {{{100, 200}}},
                                         .vias = {{.point_index = 0, .tech_via = tech_via}}});
  const auto wire = routing.createWire(std::move(wire_value), std::move(wire_routing));

  EXPECT_EQ(routing.path(wire, 0).width(), 0);
  EXPECT_TRUE(routing.destroyWire(wire));

  DesignWire invalid_wire{.net = net};
  DesignWireRoutingInput invalid_routing;
  invalid_routing.appendPath(DesignWirePath{.layer = routing_layer,
                                            .flags = DesignWirePathFlag::kHasWidth,
                                            .width = 0,
                                            .points = {{{0, 0}}, {{100, 0}}}});
  EXPECT_THROW((void) routing.createWire(std::move(invalid_wire), std::move(invalid_routing)), std::invalid_argument);
  EXPECT_TRUE(netlist.destroyNet(net));
}

TEST_F(DesignStorageTest, StoresMultiplePathsInDesignWideTypedPools)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  const auto net = netlist.createSpecialNet(DesignNet{.name = "VDD"});

  DesignWire value{.net = net};
  DesignWireRoutingInput input;
  input.appendPath(DesignWirePath{.layer = routing_layer,
                                  .flags = DesignWirePathFlag::kHasWidth,
                                  .width = 20,
                                  .points = {{{0, 0}},
                                             {.position = {100, 0}, .flags = DesignWirePointFlag::kHasExtension, .extension = 7}},
                                  .vias = {{.point_index = 1,
                                            .tech_via = tech_via,
                                            .orientation = DesignOrientation::kS,
                                            .flags = DesignWireViaFlag::kHasMask,
                                            .top_mask = 1,
                                            .cut_mask = 2,
                                            .bottom_mask = 3}}});
  input.appendPath(DesignWirePath{.layer = routing_layer,
                                  .flags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasShape,
                                  .width = 30,
                                  .shape = "STRIPE",
                                  .points = {{{100, 0}},
                                             {.position = {100, 100}, .flags = DesignWirePointFlag::kVirtual},
                                             {{200, 100}}},
                                  .rectangles = {{.point_index = 1, .delta = {-2, -2, 2, 2}}}});

  ASSERT_EQ(input.pathCount(), 2u);
  EXPECT_EQ(input.points.size(), 5u);
  EXPECT_EQ(input.vias.size(), 1u);
  EXPECT_EQ(input.rectangles.size(), 1u);
  EXPECT_EQ(input.extras.size(), 2u);

  const auto wire = routing.createWire(std::move(value), std::move(input));
  ASSERT_EQ(routing.pathCount(wire), 2u);
  const auto first = routing.path(wire, 0);
  const auto second = routing.path(wire, 1);
  const auto pool = routing.routingPoolStatistics();
  ASSERT_EQ(pool.paths.count, 2u);
  EXPECT_EQ(pool.points.count, 5u);
  EXPECT_EQ(pool.vias.count, 1u);
  EXPECT_EQ(pool.rectangles.count, 1u);
  EXPECT_EQ(first.points().size(), 2u);
  EXPECT_EQ(second.points().size(), 3u);
  EXPECT_EQ(second.points().front().position, (Point{100, 0}));
  EXPECT_EQ(first.vias().front().point_index, 1u);
  EXPECT_EQ(second.rectangles().front().point_index, 1u);
  EXPECT_EQ(first.width(), 20);
  EXPECT_EQ(second.width(), 30);
  EXPECT_EQ(second.shape(), "STRIPE");
  EXPECT_EQ(first.points()[1].extension, 7);
  EXPECT_EQ(first.vias().front().orientation, DesignOrientation::kS);
  EXPECT_EQ(first.vias().front().cut_mask, 2u);
  EXPECT_NE(second.points()[1].flags & DesignWirePointFlag::kVirtual, 0u);

  std::vector<int32_t> widths;
  std::vector<std::size_t> point_counts;
  routing.forEachPath(wire, [&](DesignWirePathView path) {
    widths.push_back(path.width());
    point_counts.push_back(path.points().size());
  });
  EXPECT_EQ(widths, (std::vector<int32_t>{20, 30}));
  EXPECT_EQ(point_counts, (std::vector<std::size_t>{2u, 3u}));

  std::size_t compact_path_index = 0u;
  routing.forEachCompactPath(wire, [&](DesignRoutingCompactPathView path) {
    ASSERT_NE(path.extra, nullptr);
    if (compact_path_index == 0u) {
      EXPECT_EQ(path.extra->width, 20);
      ASSERT_EQ(path.points.size(), 2u);
      EXPECT_EQ(path.point_global_begin, 0u);
      ASSERT_EQ(path.point_extras.size(), 1u);
      EXPECT_EQ(path.point_extras.front().point_index, 1u);
      ASSERT_EQ(path.vias.size(), 1u);
      EXPECT_EQ(path.via_global_begin, 0u);
      ASSERT_EQ(path.via_extras.size(), 1u);
      EXPECT_EQ(path.via_extras.front().cut_mask, 2u);
    } else {
      EXPECT_EQ(path.extra->width, 30);
      EXPECT_EQ(path.extra->shape, "STRIPE");
      ASSERT_EQ(path.points.size(), 3u);
      EXPECT_EQ(path.point_global_begin, 2u);
      ASSERT_EQ(path.point_extras.size(), 1u);
      EXPECT_EQ(path.point_extras.front().point_index, 3u);
      ASSERT_EQ(path.rectangles.size(), 1u);
      EXPECT_EQ(path.rectangles.front().point_index, 1u);
    }
    ++compact_path_index;
  });
  EXPECT_EQ(compact_path_index, 2u);
  ASSERT_EQ(routing.wireIds(net).size(), 1u);
  EXPECT_EQ(routing.wireIds(net).front(), wire);
  EXPECT_EQ(sizeof(DesignRoutingPathRecord), 16u);
  EXPECT_EQ(sizeof(DesignRoutingPointRecord), 8u);
  EXPECT_EQ(sizeof(DesignRoutingViaRecord), 16u);
  EXPECT_EQ(sizeof(DesignWire), 56u);
}

TEST_F(DesignStorageTest, RebuildsWireRoutingAndMovesItsNetIndex)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  const auto first_net = netlist.createSpecialNet(DesignNet{.name = "VDD"});
  const auto second_net = netlist.createSpecialNet(DesignNet{.name = "VSS"});

  DesignWireRoutingInput initial;
  initial.appendPath(DesignWirePath{.layer = routing_layer,
                                    .flags = DesignWirePathFlag::kHasWidth,
                                    .width = 20,
                                    .points = {{{0, 0}}, {{100, 0}}}});
  const auto wire = routing.createWire(DesignWire{.net = first_net}, std::move(initial));

  DesignWireRoutingInput replacement;
  replacement.appendPath(DesignWirePath{.layer = routing_layer,
                                        .flags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasShape,
                                        .width = 30,
                                        .shape = "STRIPE",
                                        .points = {{{10, 10}}, {{10, 200}}, {{300, 200}}}});
  routing.updateWire(wire, DesignWire{.net = second_net, .status = DesignWireStatus::kFixed}, std::move(replacement));

  EXPECT_TRUE(routing.wires(first_net).empty());
  ASSERT_EQ(routing.wires(second_net), (std::vector<DesignWireId>{wire}));
  EXPECT_EQ(routing.wire(wire).status, DesignWireStatus::kFixed);
  ASSERT_EQ(routing.pathCount(wire), 1u);
  EXPECT_EQ(routing.path(wire, 0).points().size(), 3u);
  EXPECT_EQ(routing.path(wire, 0).width(), 30);
  EXPECT_EQ(routing.path(wire, 0).shape(), "STRIPE");
  EXPECT_EQ(routing.routingPoolStatistics().paths.count, 2u);

  EXPECT_TRUE(routing.destroyWire(wire));
  EXPECT_TRUE(netlist.destroyNet(first_net));
  EXPECT_TRUE(netlist.destroyNet(second_net));
}

TEST_F(DesignStorageTest, StoresDesignScopeNonDefaultRulesAndProtectsNetReferences)
{
  auto& routing = design.routingStorage();
  auto& netlist = design.netlistStorage();
  const auto rule = routing.createNonDefaultRule(
      DesignNonDefaultRule{.name = "BLOCK_WIDE",
                           .flags = DesignNonDefaultRuleFlag::kHardSpacing,
                           .layer_rules = {{.layer = layer,
                                            .flags = DesignNdrLayerRuleFlag::kHasSpacing,
                                            .width = 20,
                                            .spacing = 12}},
                           .vias = {{.tech_via = tech_via}},
                           .properties = {{.name = "owner", .value = "design"}}});
  const auto net = netlist.createNet(DesignNet{.name = "n1",
                                                .flags = DesignNetFlag::kHasNonDefaultRule,
                                                .design_non_default_rule = rule});

  EXPECT_EQ(routing.findNonDefaultRule("BLOCK_WIDE"), rule);
  EXPECT_EQ(routing.nonDefaultRule(rule).layer_rules.front().spacing, 12);
  EXPECT_EQ(routing.nonDefaultRuleCount(), 1u);
  EXPECT_FALSE(routing.destroyNonDefaultRule(rule));

  auto updated = routing.nonDefaultRule(rule);
  updated.name = "BLOCK_WIDER";
  updated.layer_rules.front().width = 24;
  routing.updateNonDefaultRule(rule, std::move(updated));
  EXPECT_EQ(routing.findNonDefaultRule("BLOCK_WIDER"), rule);

  EXPECT_TRUE(netlist.destroyNet(net));
  EXPECT_TRUE(routing.destroyNonDefaultRule(rule));
}

TEST_F(DesignStorageTest, EnforcesRegionGroupAndBlockageReferences)
{
  auto& netlist = design.netlistStorage();
  auto& constraints = design.constraintStorage();

  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto region
      = constraints.createRegion(DesignRegion{.name = "FENCE", .type = DesignRegionType::kFence, .rectangles = {{0, 0, 1000, 1000}}});
  const auto group = constraints.createGroup(
      DesignGroup{.name = "logic", .flags = DesignGroupFlag::kHasRegion, .region = region, .instances = {instance}});
  const auto placement
      = constraints.createBlockage(DesignBlockage{.kind = DesignBlockageKind::kPlacement,
                                                  .flags = DesignBlockageFlag::kHasPartial | DesignBlockageFlag::kHasComponent,
                                                  .component = instance,
                                                  .partial = 50.0,
                                                  .rectangles = {{100, 100, 200, 200}}});
  const auto routing = constraints.createBlockage(DesignBlockage{.kind = DesignBlockageKind::kRouting,
                                                                 .flags = DesignBlockageFlag::kHasLayer,
                                                                 .layer = routing_layer,
                                                                 .rectangles = {{300, 300, 400, 400}}});

  EXPECT_EQ(constraints.findRegion("FENCE"), region);
  EXPECT_EQ(constraints.findGroup("logic"), group);
  EXPECT_TRUE(constraints.referencesInstance(instance));
  EXPECT_FALSE(constraints.destroyRegion(region));
  EXPECT_FALSE(netlist.destroyInstance(instance));
  EXPECT_EQ(constraints.regionCount(), 1u);
  EXPECT_EQ(constraints.groupCount(), 1u);
  EXPECT_EQ(constraints.blockageCount(), 2u);

  EXPECT_TRUE(constraints.destroyGroup(group));
  EXPECT_TRUE(constraints.destroyBlockage(placement));
  EXPECT_TRUE(constraints.destroyBlockage(routing));
  EXPECT_TRUE(constraints.destroyRegion(region));
  EXPECT_TRUE(netlist.destroyInstance(instance));
}

TEST_F(DesignStorageTest, RejectsInvalidWireAndConstraintPayloads)
{
  auto& netlist = design.netlistStorage();
  auto& routing = design.routingStorage();
  auto& constraints = design.constraintStorage();
  const auto net = netlist.createNet(DesignNet{.name = "n1"});

  EXPECT_THROW((void) routing.createWire(DesignWire{.net = net}, DesignWireRoutingInput{}), std::invalid_argument);
  EXPECT_THROW((void) routing.createVia(DesignVia{.name = "EMPTY"}), std::invalid_argument);
  EXPECT_THROW((void) constraints.createRegion(DesignRegion{.name = "EMPTY"}), std::invalid_argument);
  EXPECT_THROW((void) constraints.createBlockage(DesignBlockage{.kind = DesignBlockageKind::kRouting, .rectangles = {{0, 0, 10, 10}}}),
               std::invalid_argument);

  auto wire = DesignWire{.net = net};
  DesignWireRoutingInput wire_routing;
  wire_routing.appendPath(DesignWirePath{.layer = routing_layer, .points = {{{0, 0}}}});
  wire.status = static_cast<DesignWireStatus>(255);
  EXPECT_THROW((void) routing.createWire(std::move(wire), std::move(wire_routing)), std::invalid_argument);

  auto region = DesignRegion{.name = "BAD_REGION", .rectangles = {{0, 0, 10, 10}}};
  region.type = static_cast<DesignRegionType>(255);
  EXPECT_THROW((void) constraints.createRegion(region), std::invalid_argument);

  auto blockage = DesignBlockage{.rectangles = {{0, 0, 10, 10}}};
  blockage.kind = static_cast<DesignBlockageKind>(255);
  EXPECT_THROW((void) constraints.createBlockage(blockage), std::invalid_argument);

  EXPECT_THROW((void) routing.setNetGeometry(net, DesignNetGeometry{.rectangles = {{.layer = routing_layer, .rectangle = {0, 0, 10, 10}}}}),
               std::invalid_argument);
  const auto special_net = netlist.createSpecialNet(DesignNet{.name = "VDD"});
  EXPECT_THROW((void) routing.setNetGeometry(special_net, DesignNetGeometry{}), std::invalid_argument);
  EXPECT_THROW((void) routing.setNetGeometry(
                   special_net,
                   DesignNetGeometry{.rectangles
                                     = {{.layer = routing_layer, .rectangle = {0, 0, 10, 10}, .flags = DesignNetGeometryFlag::kHasMask}}}),
               std::invalid_argument);

  EXPECT_TRUE(netlist.destroyNet(net));
  EXPECT_TRUE(netlist.destroyNet(special_net));
}

}  // namespace
}  // namespace eccdb
