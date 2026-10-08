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

#include <algorithm>
#include <span>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "service/routing/RoutingService.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

class RoutingRouteServiceTest : public testing::Test
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

  [[nodiscard]] DesignWireId addWireFor(DesignNetId target, DesignWireStatus status,
                                         std::vector<DesignWirePath> paths, std::string shield_net = {})
  {
    auto& routing = design.routingStorage();
    const auto wire = routing.createWire(
        DesignWire{.net = target, .status = status, .shield_net = std::move(shield_net)});
    try {
      for (std::size_t path_index = 0; path_index < paths.size(); ++path_index) {
        auto& path = paths[path_index];
        std::optional<DesignRouteShapeAttributes> attributes;
        if (path.flags & (DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask
                          | DesignWirePathFlag::kHasTaperRule | DesignWirePathFlag::kHasShape
                          | DesignWirePathFlag::kHasStyle)) {
          attributes = DesignRouteShapeAttributes{.width = path.width,
                                                  .mask = path.mask,
                                                  .taper_rule = path.taper_rule,
                                                  .shape = path.shape,
                                                  .style = path.style};
        }
        auto make_attributes = [&]() { return attributes; };
        auto add_shape = [&](DesignRouteShape shape) {
          shape.wire = wire;
          shape.source_path = static_cast<uint32_t>(path_index);
          static_cast<void>(routing.createShape(wire, std::move(shape), make_attributes()));
        };
        auto exit_layer = [&](const DesignWireVia& via, TechRoutingLayerId current) {
          if (!via.tech_via) {
            return current;
          }
          const auto& geometry = technology.techRegistry().registry().get<const TechViaGeometry>(via.tech_via.entity());
          if (geometry.bottom_layer.entity == current.entity()) {
            return TechRoutingLayerId{geometry.top_layer.entity};
          }
          if (geometry.top_layer.entity == current.entity()) {
            return TechRoutingLayerId{geometry.bottom_layer.entity};
          }
          throw std::invalid_argument("VIA does not touch the active routing layer");
        };
        TechRoutingLayerId current_layer = path.layer;
        for (std::size_t point_index = 0; point_index < path.points.size(); ++point_index) {
          if (point_index > 0) {
            add_shape(DesignRouteShape{.layer = current_layer,
                                       .flags = path.flags,
                                       .geometry = DesignRouteSegment{.begin = path.points[point_index - 1],
                                                                      .end = path.points[point_index]}});
          }
          for (const auto& rectangle : path.rectangles) {
            if (rectangle.point_index == point_index) {
              const auto anchor = path.points[point_index];
              add_shape(DesignRouteShape{.layer = current_layer,
                                         .flags = path.flags,
                                         .geometry = DesignRoutePatch{.anchor = anchor,
                                                                      .rectangle = {anchor.position.x + rectangle.delta.ll_x,
                                                                                   anchor.position.y + rectangle.delta.ll_y,
                                                                                   anchor.position.x + rectangle.delta.ur_x,
                                                                                   anchor.position.y + rectangle.delta.ur_y}}});
            }
          }
          if (point_index + 1 == path.points.size() && path.points.size() == 1 && path.vias.empty()
              && path.rectangles.empty()) {
            add_shape(DesignRouteShape{.layer = current_layer,
                                       .flags = path.flags,
                                       .geometry = DesignRoutePoint{.point = path.points.front()}});
          }
          for (const auto& via : path.vias) {
            if (via.point_index == point_index) {
              const auto next_layer = exit_layer(via, current_layer);
              add_shape(DesignRouteShape{.layer = current_layer,
                                         .flags = path.flags,
                                         .geometry = DesignRouteVia{.origin = path.points[point_index],
                                                                    .via = via,
                                                                    .exit_layer = next_layer}});
              current_layer = next_layer;
            }
          }
        }
      }
      return wire;
    } catch (...) {
      static_cast<void>(routing.destroyWire(wire));
      throw;
    }
  }

  [[nodiscard]] DesignWireId addWire(DesignWireStatus status, std::vector<DesignWirePath> paths, std::string shield_net = {})
  {
    return addWireFor(net, status, std::move(paths), std::move(shield_net));
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

TEST_F(RoutingRouteServiceTest, ReadsSegmentViaPatchWithLayerTransitions)
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

  DatabaseContext context{design, &technology};
  RoutingService service{context};
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

TEST_F(RoutingRouteServiceTest, ReplacesOnlyRoutedWiresAndCanonicalizesMixedShapes)
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

  DatabaseContext context{design, &technology};
  RoutingService service{context};
  NetRouteWriteProfile profile;
  const auto replacement = service.replaceRouted(net, std::span<const RouteShape>{replacement_shapes}, &profile);

  EXPECT_TRUE(replacement);
  EXPECT_FALSE(design.routingStorage().contains(old_routed));
  EXPECT_TRUE(design.routingStorage().contains(fixed));
  EXPECT_EQ(design.routingStorage().wire(fixed).status, DesignWireStatus::kFixed);
  EXPECT_EQ(design.routingStorage().wire(replacement).shapes.size(), replacement_shapes.size());

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
  EXPECT_FALSE(service.replaceRouted(net, std::span<const RouteShape>{empty}, &profile));
  ASSERT_EQ(design.routingStorage().wireIds(net).size(), 1u);
  EXPECT_EQ(design.routingStorage().wireIds(net).front(), fixed);
  EXPECT_EQ(profile.calls, 2u);
  EXPECT_EQ(profile.input_shapes, replacement_shapes.size());
  EXPECT_EQ(profile.replaced_wires, 2u);
  EXPECT_EQ(profile.replaced_shapes, 1u + replacement_shapes.size());
  EXPECT_LE(profile.select_wires_ns + profile.convert_shapes_ns + profile.create_wire_ns
                + profile.append_shapes_ns + profile.destroy_old_ns,
            profile.service_total_ns);
}

TEST_F(RoutingRouteServiceTest, RejectsLateInvalidReplacementWithoutLeakingStagedShapes)
{
  auto& routing = design.routingStorage();
  const auto fixed = addWire(DesignWireStatus::kFixed,
                             {DesignWirePath{.layer = m1,
                                             .points = {DesignWirePoint{.position = {0, 20}},
                                                        DesignWirePoint{.position = {30, 20}}}}});
  const auto routed = addWire(DesignWireStatus::kRouted,
                              {DesignWirePath{.layer = m1,
                                              .points = {DesignWirePoint{.position = {0, 0}},
                                                         DesignWirePoint{.position = {10, 0}}}}});
  const auto routed_shapes = routing.wire(routed).shapes;
  const auto fixed_shapes = routing.wire(fixed).shapes;
  ASSERT_EQ(routed_shapes.size(), 1u);
  const auto& routed_shape_before = routing.shape(routed_shapes.front());
  const auto routed_before = std::get<DesignRouteSegment>(routed_shape_before.geometry);
  const auto routed_layer_before = routed_shape_before.layer;
  const auto routed_source_path_before = routed_shape_before.source_path;
  const auto wire_ids_before = routing.wires(net);
  const auto wire_count_before = routing.wireCount();
  const auto shape_count = [&] { return design.designRegistry().registry().view<const DesignRouteShape>().size(); };
  const auto shape_count_before = shape_count();
  const auto live_entity_count = [&] { return design.designRegistry().registry().storage<DesignEntity>().free_list(); };
  const auto live_entity_count_before = live_entity_count();

  // The final Patch is invalid, after the preceding Segment/Via/Patch DTOs
  // have been accepted. Replacement must still leave the published route intact.
  const std::vector<RouteShape> replacement{
      RouteSegment{.layer = m1, .begin = {10, 0}, .end = {30, 0}},
      RouteVia{.definition = RouteViaDefinition{via12}, .origin = {30, 0}},
      RouteSegment{.layer = m2, .begin = {30, 0}, .end = {30, 40}},
      RoutePatch{.layer = m2, .rectangle = {26, 36, 34, 44}},
      RoutePatch{.layer = m2, .rectangle = {10, 10, 9, 11}}};

  DatabaseContext context{design, &technology};
  RoutingService service{context};
  EXPECT_THROW((void) service.replaceRouted(net, std::span<const RouteShape>{replacement}), std::invalid_argument);

  EXPECT_TRUE(routing.contains(routed));
  EXPECT_TRUE(routing.contains(fixed));
  EXPECT_EQ(routing.wire(routed).net, net);
  EXPECT_EQ(routing.wire(routed).status, DesignWireStatus::kRouted);
  EXPECT_EQ(routing.wire(routed).shapes, routed_shapes);
  EXPECT_EQ(routing.wire(fixed).status, DesignWireStatus::kFixed);
  EXPECT_EQ(routing.wire(fixed).shapes, fixed_shapes);
  EXPECT_EQ(routing.shape(routed_shapes.front()).layer, routed_layer_before);
  EXPECT_EQ(routing.shape(routed_shapes.front()).source_path, routed_source_path_before);
  EXPECT_EQ(std::get<DesignRouteSegment>(routing.shape(routed_shapes.front()).geometry).begin.position, routed_before.begin.position);
  EXPECT_EQ(std::get<DesignRouteSegment>(routing.shape(routed_shapes.front()).geometry).end.position, routed_before.end.position);
  EXPECT_EQ(routing.wires(net), wire_ids_before);
  EXPECT_EQ(routing.wireCount(), wire_count_before);
  EXPECT_EQ(shape_count(), shape_count_before);
  EXPECT_EQ(live_entity_count(), live_entity_count_before);
}

TEST_F(RoutingRouteServiceTest, SuccessfulReplacementAdvancesDefSourcePathForLaterShapeEdits)
{
  const auto old_wire = addWire(DesignWireStatus::kRouted,
                                {DesignWirePath{.layer = m1,
                                                .points = {{.position = {0, 0}}, {.position = {10, 0}}}}});
  const std::vector<RouteShape> replacement{
      RouteSegment{.layer = m1, .begin = {10, 0}, .end = {30, 0}},
      RoutePatch{.layer = m1, .rectangle = {28, -2, 32, 2}}};

  DatabaseContext context{design, &technology};
  RoutingService service{context};
  const auto replacement_wire = service.replaceRouted(net, std::span<const RouteShape>{replacement});
  auto& routing = design.routingStorage();
  EXPECT_FALSE(routing.contains(old_wire));
  ASSERT_EQ(routing.wire(replacement_wire).shapes.size(), replacement.size());

  const auto appended = routing.createShape(
      replacement_wire,
      DesignRouteShape{.layer = m1,
                       .geometry = DesignRouteSegment{.begin = {.position = {40, 0}}, .end = {.position = {60, 0}}}});

  const auto& shapes = routing.wire(replacement_wire).shapes;
  ASSERT_EQ(shapes.size(), replacement.size() + 1u);
  EXPECT_EQ(routing.shape(shapes[0]).source_path, 0u);
  EXPECT_EQ(routing.shape(shapes[1]).source_path, 1u);
  EXPECT_EQ(routing.shape(appended).source_path, replacement.size());
  EXPECT_EQ(routing.wire(replacement_wire).next_source_path, replacement.size() + 1u);
  ASSERT_EQ(service.snapshot(net).wires.size(), 1u);
  EXPECT_EQ(service.snapshot(net).wires.front().shapes.size(), shapes.size());
}

TEST_F(RoutingRouteServiceTest, PreservesShieldWireMetadataAndLeavesItUntouched)
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

  DatabaseContext context{design, &technology};
  RoutingService service{context};
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

TEST_F(RoutingRouteServiceTest, ReplacesRoutedWiresWithDirectShapeInput)
{
  const auto old_wire = addWire(DesignWireStatus::kRouted,
                                {DesignWirePath{.layer = m1,
                                                .points = {{.position = {0, 0}}, {.position = {10, 0}}}}});
  DatabaseContext context{design, &technology};
  RoutingService service{context};
  const std::vector<RouteShape> replacement{RouteSegment{.layer = m1, .begin = {10, 0}, .end = {30, 0}}};
  const auto replacement_wire = service.replaceRouted(net, std::span<const RouteShape>{replacement});
  EXPECT_FALSE(design.routingStorage().contains(old_wire));
  EXPECT_TRUE(design.routingStorage().contains(replacement_wire));
  ASSERT_EQ(service.snapshot(net).wires.size(), 1u);
  EXPECT_EQ(service.snapshot(net).wires.front().shapes.size(), 1u);
}

TEST_F(RoutingRouteServiceTest, PreservesTaperFlagInSnapshotAndReplacement)
{
  const auto old_wire = addWire(
      DesignWireStatus::kRouted,
      {DesignWirePath{.layer = m1,
                      .flags = DesignWirePathFlag::kTaper,
                      .points = {{.position = {0, 0}}, {.position = {10, 0}}}}});
  DatabaseContext context{design, &technology};
  RoutingService service{context};
  const auto old_snapshot = service.snapshot(net);
  ASSERT_EQ(old_snapshot.wires.size(), 1u);
  ASSERT_EQ(old_snapshot.wires.front().shapes.size(), 1u);
  EXPECT_TRUE(std::get<RouteSegment>(old_snapshot.wires.front().shapes.front()).tapered);

  const std::vector<RouteShape> replacement{
      RouteSegment{.layer = m1, .begin = {10, 0}, .end = {30, 0}, .tapered = true}};
  const auto new_wire = service.replaceRouted(net, std::span<const RouteShape>{replacement});
  EXPECT_FALSE(design.routingStorage().contains(old_wire));
  const auto new_snapshot = service.snapshot(net);
  ASSERT_EQ(new_snapshot.wires.size(), 1u);
  EXPECT_EQ(new_snapshot.wires.front().wire, new_wire);
  ASSERT_EQ(new_snapshot.wires.front().shapes.size(), 1u);
  EXPECT_TRUE(std::get<RouteSegment>(new_snapshot.wires.front().shapes.front()).tapered);
}

TEST_F(RoutingRouteServiceTest, RejectsInvalidViaLayerTransition)
{
  // Shape creation resolves the active layer once, so this invalid reference
  // is rejected at ingestion rather than every subsequent snapshot.
  EXPECT_THROW((void) addWire(DesignWireStatus::kRouted,
                            {DesignWirePath{.layer = m1,
                                            .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {20, 0}}},
                                            .vias = {{.point_index = 1u, .tech_via = via23}}}}), std::invalid_argument);

  DatabaseContext context{design, &technology};
  RoutingService service{context};
  EXPECT_TRUE(service.snapshot(net).wires.empty());
  const std::vector<RouteShape> replacement{RouteSegment{.layer = m1, .begin = {0, 0}, .end = {20, 0}}};
  const auto replacement_wire = service.replaceRouted(net, std::span<const RouteShape>{replacement});
  EXPECT_TRUE(design.routingStorage().contains(replacement_wire));
}

TEST_F(RoutingRouteServiceTest, ReadsUnorderedViasWithStableSameAnchorOrder)
{
  const auto wire = addWire(
      DesignWireStatus::kRouted,
      {DesignWirePath{.layer = m1,
                      .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {10, 0}},
                                 DesignWirePoint{.position = {20, 0}}, DesignWirePoint{.position = {30, 0}}},
                      .vias = {{.point_index = 2u, .tech_via = via23, .orientation = DesignOrientation::kS},
                               {.point_index = 1u, .tech_via = via12, .orientation = DesignOrientation::kE},
                               {.point_index = 2u, .tech_via = via23, .orientation = DesignOrientation::kW}}}});

  DatabaseContext context{design, &technology};
  RoutingService service{context};
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

TEST_F(RoutingRouteServiceTest, ReadsUnorderedPatchesWithStableSameAnchorOrder)
{
  const auto wire = addWire(
      DesignWireStatus::kRouted,
      {DesignWirePath{.layer = m1,
                      .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {10, 0}},
                                 DesignWirePoint{.position = {20, 0}}, DesignWirePoint{.position = {30, 0}}},
                      .rectangles = {{.point_index = 2u, .delta = {-6, -2, -4, 2}},
                                     {.point_index = 1u, .delta = {-2, -3, 0, 3}},
                                     {.point_index = 2u, .delta = {4, -1, 6, 1}}}}});

  DatabaseContext context{design, &technology};
  RoutingService service{context};
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

TEST_F(RoutingRouteServiceTest, RejectsTrustedOutOfRangePrimitiveAnchors)
{
  DatabaseContext context{design, &technology};
  RoutingService service{context};
  const std::vector<RouteShape> replacement{RouteSegment{.layer = m1, .begin = {0, 0}, .end = {20, 0}}};
  const auto expect_rejected = [&](std::string name) {
    const auto invalid_net = design.netlistStorage().createNet(DesignNet{.name = std::move(name)});
    EXPECT_TRUE(service.snapshot(invalid_net).wires.empty());
    const auto replacement_wire = service.replaceRouted(invalid_net, std::span<const RouteShape>{replacement});
    ASSERT_EQ(design.routingStorage().wireIds(invalid_net).size(), 1u);
    EXPECT_EQ(design.routingStorage().wireIds(invalid_net).front(), replacement_wire);
  };

  expect_rejected("empty_net_a");
  expect_rejected("empty_net_b");
}

TEST_F(RoutingRouteServiceTest, ReadsMixedPathsAndResolvesPatchViaAnchorLayers)
{
  const auto mixed = addWire(DesignWireStatus::kRouted,
                             {DesignWirePath{.layer = m1,
                                             .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {20, 0}}}},
                              DesignWirePath{.layer = m2,
                                             .points = {DesignWirePoint{.position = {40, 0}}, DesignWirePoint{.position = {40, 20}}}}});

  DatabaseContext context{design, &technology};
  RoutingService service{context};
  const auto snapshot = service.snapshot(net);
  ASSERT_EQ(snapshot.wires.size(), 1u);
  EXPECT_EQ(snapshot.wires.front().wire, mixed);
  ASSERT_EQ(snapshot.wires.front().shapes.size(), 2u);
  EXPECT_EQ(std::get<RouteSegment>(snapshot.wires.front().shapes[0]).layer, m1);
  EXPECT_EQ(std::get<RouteSegment>(snapshot.wires.front().shapes[1]).layer, m2);

  const auto ambiguous_net = design.netlistStorage().createNet(DesignNet{.name = "ambiguous"});
  const auto ambiguous_wire = addWireFor(
      ambiguous_net,
      DesignWireStatus::kRouted,
      {DesignWirePath{.layer = m1,
                      .points = {DesignWirePoint{.position = {0, 0}}, DesignWirePoint{.position = {20, 0}},
                                 DesignWirePoint{.position = {40, 0}}},
                      .vias = {{.point_index = 2u, .tech_via = via12}, {.point_index = 1u, .tech_via = via12}},
                      .rectangles = {{.point_index = 2u, .delta = {-1, -1, 1, 1}}}}});
  const auto resolved = service.snapshot(ambiguous_net);
  ASSERT_EQ(resolved.wires.size(), 1u);
  const auto& shapes = resolved.wires.front().shapes;
  ASSERT_EQ(shapes.size(), 5u);
  std::vector<RouteSegment> resolved_segments;
  std::vector<RouteVia> resolved_vias;
  std::vector<RoutePatch> resolved_patches;
  for (const auto& shape : shapes) {
    std::visit(
        [&](const auto& value) {
          using Value = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, RouteSegment>) {
            resolved_segments.push_back(value);
          } else if constexpr (std::is_same_v<Value, RouteVia>) {
            resolved_vias.push_back(value);
          } else {
            resolved_patches.push_back(value);
          }
        },
        shape);
  }
  ASSERT_EQ(resolved_segments.size(), 2u);
  ASSERT_EQ(resolved_vias.size(), 2u);
  ASSERT_EQ(resolved_patches.size(), 1u);
  EXPECT_EQ(resolved_segments[0].layer, m1);
  EXPECT_EQ(resolved_segments[1].layer, m2);
  const auto via_at_40 = std::find_if(resolved_vias.begin(), resolved_vias.end(), [](const auto& via) {
    return via.origin == Point{40, 0};
  });
  ASSERT_NE(via_at_40, resolved_vias.end());
  EXPECT_EQ(resolved_patches.front().layer, m2);
  EXPECT_EQ(resolved_patches.front().rectangle, (Rect{39, -1, 41, 1}));
  const std::vector<RouteShape> replacement{RouteSegment{.layer = m1, .begin = {0, 0}, .end = {20, 0}}};
  const auto replacement_wire = service.replaceRouted(ambiguous_net, std::span<const RouteShape>{replacement});
  EXPECT_FALSE(design.routingStorage().contains(ambiguous_wire));
  EXPECT_TRUE(design.routingStorage().contains(replacement_wire));
}

}  // namespace
}  // namespace eccdb
