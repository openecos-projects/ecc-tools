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
#include <string_view>
#include <vector>

#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "viewer/DesignModelAdapter.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

class DesignModelAdapterTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    m1 = technology.createRoutingLayer(TechLayerInfo{.name = "M1"},
                                       TechRoutingLayer{.direction = TechRoutingDirection::kHorizontal,
                                                        .width = 20,
                                                        .pitch_x = 80,
                                                        .pitch_y = 96,
                                                        .offset_x = 4,
                                                        .offset_y = 6});
    cut = technology.createCutLayer(TechLayerInfo{.name = "V1"}, TechCutLayer{.width = 4});
    m2 = technology.createRoutingLayer(TechLayerInfo{.name = "M2"},
                                       TechRoutingLayer{.direction = TechRoutingDirection::kVertical,
                                                        .width = 24,
                                                        .pitch_x = 96,
                                                        .pitch_y = 80});

    master = library.cellMasterStorage().createCellMaster(
        LibraryCellMaster{.name = "INVX1", .width = 20, .height = 40});
    input = library.masterTermStorage().createMasterTerm(
        master, LibraryMasterTerm{.name = "A", .direction = LibraryMasterTermDirection::kInput});
    output = library.masterTermStorage().createMasterTerm(
        master, LibraryMasterTerm{.name = "Y", .direction = LibraryMasterTermDirection::kOutput});
    input_pin = library.masterPortStorage().createMasterPin(
        input, LibraryMasterPortInput{.layer_clauses = {{.layer = TechLayerId{m1.entity()}, .geometry = {.rects = {{1, 2, 5, 6}}}}}});

    first_instance = design.netlistStorage().createInstance(
        DesignInstance{.name = "u0",
                       .master = master,
                       .origin = {100, 200},
                       .orientation = DesignOrientation::kN,
                       .placement_status = DesignPlacementStatus::kPlaced});
    second_instance = design.netlistStorage().createInstance(
        DesignInstance{.name = "u1",
                       .master = master,
                       .origin = {500, 600},
                       .orientation = DesignOrientation::kN,
                       .placement_status = DesignPlacementStatus::kPlaced});

    net = design.netlistStorage().createNet(DesignNet{.name = "signal"});
    input_term = design.netlistStorage().findInstanceTerm(first_instance, "A");
    ASSERT_TRUE(input_term);
    design.netlistStorage().connect(input_term, net);
    block_term = design.netlistStorage().createBlockTerm(
        DesignBlockTerm{.name = "IN", .direction = DesignIoPinDirection::kInput},
        {DesignBlockPinInput{.flags = DesignIoPinPortFlag::kHasPlacement,
                             .placement_status = DesignPlacementStatus::kPlaced,
                             .origin = {40, 50},
                             .orientation = DesignOrientation::kN,
                             .rectangles = {{.layer = TechLayerId{m1.entity()}, .rectangle = {0, 0, 12, 8}}}}});
    design.netlistStorage().connect(block_term, net);

    static_cast<void>(design.floorplanStorage().createTrackGrid(
        DesignTrackGrid{.axis = DesignAxis::kX, .start = 12, .track_count = 9, .step = 80, .layers = {m1, m2}}));
    wire = design.routingStorage().createWire(DesignWire{.net = net, .status = DesignWireStatus::kRouted});
    route_shape = design.geometryStorage().createRouteShape(
        wire, DesignRouteShape{.layer = m1,
                               .geometry = DesignRouteSegment{.begin = {.position = {100, 200}}, .end = {.position = {180, 200}}}});
  }

  static bool contains(const std::vector<ModelObjectRef>& refs, std::string_view kind, uint64_t id)
  {
    return std::ranges::any_of(refs, [kind, id](const ModelObjectRef& value) { return value.kind == kind && value.id == id; });
  }

  TechStore technology;
  LibraryStore library{technology.techRegistry()};
  DesignStore design{technology.techRegistry(), library.libraryRegistry()};
  TechRoutingLayerId m1;
  TechCutLayerId cut;
  TechRoutingLayerId m2;
  LibraryCellMasterId master;
  LibraryMasterTermId input;
  LibraryMasterTermId output;
  LibraryMasterPinId input_pin;
  DesignInstanceId first_instance;
  DesignInstanceId second_instance;
  DesignInstanceTermId input_term;
  DesignBlockTermId block_term;
  DesignNetId net;
  DesignWireId wire;
  GeometryShapeId route_shape;
};

TEST_F(DesignModelAdapterTest, NavigatesNativeEntitiesWithoutCopyingTheDesign)
{
  DatabaseContext context{design};
  DesignModelAdapter service(context);

  const auto instances = service.list(ModelEntityKind::kInstance, 0, 1);
  ASSERT_EQ(instances.records.size(), 1u);
  EXPECT_TRUE(instances.has_more);
  EXPECT_EQ(instances.next_offset, 1u);
  EXPECT_TRUE(instances.records.front().relations.empty());
  ASSERT_TRUE(instances.records.front().master);
  EXPECT_EQ(instances.records.front().master->id, master.packed());

  const auto wire_ids = service.listIds(
      ModelEntityKind::kWire, 0, 16,
      ModelObjectRef{.kind = std::string(ModelEntityKind::kNet), .id = net.packed()});
  ASSERT_EQ(wire_ids.size(), 1u);
  EXPECT_EQ(wire_ids.front().id, wire.packed());

  const auto term = service.find(ModelEntityKind::kInstanceTerm, "A",
                                 ModelObjectRef{.kind = std::string(ModelEntityKind::kInstance), .id = first_instance.packed()});
  ASSERT_TRUE(term.has_value());
  EXPECT_EQ(term->kind, ModelEntityKind::kInstanceTerm);
  EXPECT_EQ(term->id, input_term.packed());

  const auto iterms = service.list(ModelEntityKind::kInstanceTerm, 0, 16,
                                   ModelObjectRef{.kind = std::string(ModelEntityKind::kInstance), .id = first_instance.packed()});
  ASSERT_EQ(iterms.records.size(), 2u);
  EXPECT_EQ(iterms.records.front().parent->id, first_instance.packed());

  const auto all_term_ids = service.listIds(ModelEntityKind::kInstanceTerm, 0, 16);
  ASSERT_EQ(all_term_ids.size(), 4u);
  EXPECT_TRUE(contains(all_term_ids, ModelEntityKind::kInstanceTerm, input_term.packed()));
  const auto term_page = service.list(ModelEntityKind::kInstanceTerm, 1, 1);
  ASSERT_EQ(term_page.records.size(), 1u);
  EXPECT_EQ(term_page.records.front().object, all_term_ids[1]);
  EXPECT_TRUE(term_page.has_more);

  const auto master_terms = service.list(ModelEntityKind::kMasterTerm, 0, 16,
                                         ModelObjectRef{.kind = std::string(ModelEntityKind::kMaster), .id = master.packed()});
  ASSERT_EQ(master_terms.records.size(), 2u);
  const auto master_pin = service.list(ModelEntityKind::kMasterPin, 0, 16,
                                       ModelObjectRef{.kind = std::string(ModelEntityKind::kMasterTerm), .id = input.packed()});
  ASSERT_EQ(master_pin.records.size(), 1u);
  EXPECT_EQ(master_pin.records.front().object.id, input_pin.packed());
  EXPECT_EQ(master_pin.records.front().parent->id, input.packed());

  const auto net_description = service.describe(ModelEntityKind::kNet, net.packed());
  ASSERT_TRUE(net_description.has_value());
  EXPECT_EQ(net_description->name, "signal");
  EXPECT_TRUE(contains(net_description->relations, ModelEntityKind::kInstanceTerm, input_term.packed()));
  EXPECT_TRUE(contains(net_description->relations, ModelEntityKind::kBlockTerm, block_term.packed()));
  EXPECT_TRUE(contains(net_description->relations, ModelEntityKind::kWire, wire.packed()));

  const auto wire_description = service.describe(ModelEntityKind::kWire, wire.packed());
  ASSERT_TRUE(wire_description.has_value());
  EXPECT_TRUE(contains(wire_description->relations, ModelEntityKind::kShape, route_shape.packed()));
  ASSERT_TRUE(wire_description->bounds.has_value());
  EXPECT_EQ(*wire_description->bounds, (Rect{90, 190, 190, 210}));

  const auto shape = service.describe(ModelEntityKind::kShape, route_shape.packed());
  ASSERT_TRUE(shape.has_value());
  ASSERT_TRUE(shape->parent.has_value());
  ASSERT_TRUE(shape->net.has_value());
  EXPECT_EQ(shape->parent->kind, ModelEntityKind::kWire);
  EXPECT_EQ(shape->parent->id, wire.packed());
  EXPECT_EQ(shape->net->id, net.packed());
}

TEST_F(DesignModelAdapterTest, ReturnsOrderedLayerStackTrackMetadataAndPlacedInstanceBounds)
{
  DatabaseContext context{design};
  DesignModelAdapter service(context);

  const auto layers = service.layers();
  ASSERT_EQ(layers.size(), 3u);
  EXPECT_EQ(layers[0].name, "M1");
  EXPECT_EQ(layers[0].id, m1.packed());
  EXPECT_EQ(layers[0].kind, static_cast<uint8_t>(GeometryLayerKind::kRouting));
  EXPECT_EQ(layers[0].width, 20);
  EXPECT_EQ(layers[0].pitch_x, 80);
  EXPECT_EQ(layers[0].direction, static_cast<uint8_t>(TechRoutingDirection::kHorizontal));
  EXPECT_EQ(layers[1].name, "V1");
  EXPECT_EQ(layers[1].kind, static_cast<uint8_t>(GeometryLayerKind::kCut));
  EXPECT_EQ(layers[2].name, "M2");
  EXPECT_EQ(layers[2].direction, static_cast<uint8_t>(TechRoutingDirection::kVertical));

  const auto layer = service.describe(ModelEntityKind::kLayer, layers[0].id);
  ASSERT_TRUE(layer);
  EXPECT_EQ(layer->name, "M1");
  EXPECT_EQ(layer->geometry_kind, static_cast<uint8_t>(GeometryLayerKind::kRouting));
  const auto m2 = service.find(ModelEntityKind::kLayer, "M2");
  ASSERT_TRUE(m2);
  EXPECT_EQ(m2->id, layers[2].id);

  const auto tracks = service.trackGrids(m1.packed());
  ASSERT_EQ(tracks.size(), 1u);
  EXPECT_EQ(tracks.front().layer, m1.packed());
  EXPECT_EQ(tracks.front().start, 12);
  EXPECT_EQ(tracks.front().track_count, 9u);
  EXPECT_EQ(tracks.front().step, 80);
  EXPECT_EQ(tracks.front().axis, static_cast<uint8_t>(DesignAxis::kX));

  const auto placed = service.listPlacedInstances(Rect{90, 190, 130, 250});
  ASSERT_EQ(placed.records.size(), 1u);
  EXPECT_EQ(placed.records.front().object.id, first_instance.packed());
  ASSERT_TRUE(placed.records.front().bounds.has_value());
  EXPECT_EQ(*placed.records.front().bounds, (Rect{100, 200, 120, 240}));
}

}  // namespace
}  // namespace eccdb
