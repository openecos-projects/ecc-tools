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
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "library/cell_master/storage/MasterObsInput.h"
#include "library/master_port/storage/MasterPortInput.h"
#include "service/geometry/PhysicalDesignService.h"
#include "tech/TechStore.h"
#include "tech/via_master/storage/ViaMasterInput.h"

namespace eccdb {
namespace {

struct PhysicalDesignServiceFixture : testing::Test
{
  void SetUp() override
  {
    m1 = tech.createRoutingLayer(TechLayerInfo{.name = "M1"}, TechRoutingLayer{.direction = TechRoutingDirection::kHorizontal,
                                                                                 .width = 12,
                                                                                 .pitch_x = 40,
                                                                                 .pitch_y = 50});
    cut = tech.createCutLayer(TechLayerInfo{.name = "V1"}, TechCutLayer{.width = 8});
    m2 = tech.createRoutingLayer(TechLayerInfo{.name = "M2"}, TechRoutingLayer{.direction = TechRoutingDirection::kVertical,
                                                                                 .width = 14,
                                                                                 .pitch_x = 60,
                                                                                 .pitch_y = 70});
    via = tech.viaMasterStorage().createFixedViaMaster(
        TechViaMaster{.name = "VIA12"},
        TechViaMasterShapeInput{.bottom_layer = m1,
                                .bottom_geometry = {.rects = {{-2, -2, 2, 2}}},
                                .cut_layer = cut,
                                .cut_geometry = {.rects = {{-1, -1, 1, 1}}},
                                .top_layer = m2,
                                .top_geometry = {.rects = {{-3, -3, 3, 3}}}});
    generated_via_rule = tech.viaRuleGenerateStorage().createViaRuleGenerate(
        TechViaRuleGenerate{.name = "VIA12_GEN", .flags = TechViaRuleGenerateFlag::kDefault},
        TechViaRuleGenerateBottomLayer{.layer = m1,
                                       .flags = TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure,
                                       .enclosure_overhang1 = 1,
                                       .enclosure_overhang2 = 2},
        TechViaRuleGenerateCutLayer{.layer = cut,
                                    .flags = TechViaRuleGenerateCutLayerFlag::kHasRect | TechViaRuleGenerateCutLayerFlag::kHasSpacing,
                                    .cut_rect = {-2, -3, 2, 3},
                                    .spacing_x = 2,
                                    .spacing_y = 4},
        TechViaRuleGenerateTopLayer{.layer = m2,
                                    .flags = TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure,
                                    .enclosure_overhang1 = 3,
                                    .enclosure_overhang2 = 4});

    site = library.siteStorage().createSite(LibrarySite{.name = "CORE", .width = 20, .height = 40});
    master = library.cellMasterStorage().createCellMaster(
        LibraryCellMaster{.name = "CELL", .site = site, .width = 20, .height = 40});
    term = library.masterTermStorage().createMasterTerm(master, LibraryMasterTerm{.name = "A", .direction = LibraryMasterTermDirection::kInput});
    first_port = library.masterPortStorage().createMasterPort(
        term,
        LibraryMasterPortInput{.layer_clauses = {{.layer = TechLayerId{m1.entity()},
                                                   .geometry = {.rects = {{1, 2, 5, 6}},
                                                                .polygons = {{.points = {{2, 1}, {4, 1}, {4, 3}, {2, 3}}}}}}},
                               .vias = {{.via = via, .origin = {10, 20}}}});
    second_port = library.masterPortStorage().createMasterPort(term);
    library.cellMasterStorage().setObs(
        master,
        LibraryMasterObsInput{.layer_clauses = {{.layer = TechLayerId{m1.entity()},
                                                  .flags = LibraryObsLayerFlag::kHasSpacing,
                                                  .spacing = 7,
                                                  .geometry = {.rects = {{7, 8, 11, 12}}}}}});

    design.globalStorage().setInfo(DesignInfo{.name = "service", .database_units_per_micron = 1000});
    design.globalStorage().setDieArea(DesignDieAreaInput{.boundary = {{0, 0}, {1000, 0}, {1000, 800}, {0, 800}}});
    static_cast<void>(design.floorplanStorage().createTrackGrid(
        DesignTrackGrid{.axis = DesignAxis::kX, .start = 20, .track_count = 3, .step = 40, .layers = {m1}}));
    static_cast<void>(design.floorplanStorage().createGCellGrid(
        DesignGCellGrid{.axis = DesignAxis::kY, .start = 0, .line_count = 3, .step = 200}));

    design_ndr = design.routingStorage().createNonDefaultRule(
        DesignNonDefaultRule{.name = "WIDE", .layer_rules = {{.layer = TechLayerId{m1.entity()}, .width = 20}}});
    design_via = design.routingStorage().createVia(
        DesignViaInput{.name = "LOCAL_VIA",
                       .rectangles = {{.layer = TechLayerId{m1.entity()}, .rectangle = {1, 2, 3, 4}, .mask = 2}},
                       .polygons = {{.layer = TechLayerId{m2.entity()}, .points = {{1, 1}, {3, 1}, {3, 2}, {1, 2}}, .mask = 1}}});
    regular = design.netlistStorage().createNet(
        DesignNet{.name = "signal", .flags = DesignNetFlag::kHasNonDefaultRule, .design_non_default_rule = design_ndr});
    special = design.netlistStorage().createSpecialNet(DesignNet{.name = "VDD"});
    instance = design.netlistStorage().createInstance(DesignInstance{.name = "u0",
                                                                       .master = master,
                                                                       .origin = {100, 200},
                                                                       .orientation = DesignOrientation::kE,
                                                                       .placement_status = DesignPlacementStatus::kPlaced});
    instance_pin = design.netlistStorage().findInstancePin(instance, term);
    design.netlistStorage().connect(instance_pin, regular);
    io_regular = design.netlistStorage().createIoPin(DesignIoPin{.name = "in"});
    io_special = design.netlistStorage().createIoPin(DesignIoPin{.name = "vdd"});
    design.netlistStorage().connect(io_regular, regular);
    design.netlistStorage().connect(io_special, special);
    blockage = design.constraintStorage().createBlockage(
        DesignBlockageInput{.kind = DesignBlockageKind::kRouting,
                            .flags = DesignBlockageFlag::kHasLayer | DesignBlockageFlag::kHasMask,
                            .layer = m2,
                            .mask = 3,
                            .rectangles = {{300, 310, 340, 360}},
                            .polygons = {{{400, 400}, {420, 400}, {420, 430}, {400, 430}}}});

    service = std::make_unique<PhysicalDesignService>(tech, library, design);
  }

  TechStore tech;
  LibraryStore library{tech.techRegistry()};
  DesignStore design{tech.techRegistry(), library.libraryRegistry()};
  std::unique_ptr<PhysicalDesignService> service;
  TechRoutingLayerId m1;
  TechCutLayerId cut;
  TechRoutingLayerId m2;
  TechViaMasterId via;
  TechViaRuleGenerateId generated_via_rule;
  LibrarySiteId site;
  LibraryCellMasterId master;
  LibraryMasterTermId term;
  LibraryMasterPortId first_port;
  LibraryMasterPortId second_port;
  DesignNonDefaultRuleId design_ndr;
  DesignViaId design_via;
  DesignNetId regular;
  DesignNetId special;
  DesignInstanceId instance;
  DesignInstancePinId instance_pin;
  DesignIoPinId io_regular;
  DesignIoPinId io_special;
  DesignBlockageId blockage;
};

TEST_F(PhysicalDesignServiceFixture, EnumeratesPhysicalMetadataAndTransformsInstancePinGeometry)
{
  EXPECT_TRUE(service->hasDesignInfo());
  EXPECT_EQ(service->databaseUnitsPerMicron(), 1000);
  EXPECT_TRUE(service->hasDieArea());
  EXPECT_EQ(service->dieBounds(), (Rect{0, 0, 1000, 800}));
  EXPECT_EQ(service->layerKind(TechLayerId{m1.entity()}), GeometryLayerKind::kRouting);
  EXPECT_EQ(service->layerKind(TechLayerId{cut.entity()}), GeometryLayerKind::kCut);
  EXPECT_EQ(service->routingLevel(m2), std::optional<uint32_t>{2});
  EXPECT_EQ(service->trackCoordinates(m1, DesignAxis::kX), (std::vector<int32_t>{20, 60, 100}));

  std::vector<TechRoutingLayerId> routing_layers;
  service->forEachRoutingLayer([&](TechRoutingLayerId id, uint32_t, uint32_t, const TechLayerInfo&, const TechRoutingLayer&) {
    routing_layers.push_back(id);
  });
  EXPECT_EQ(routing_layers, (std::vector<TechRoutingLayerId>{m1, m2}));

  std::vector<PhysicalLayerRect> rectangles;
  std::vector<std::vector<Point>> polygons;
  std::vector<PhysicalViaPlacement> vias;
  service->forEachPlacedInstancePinGeometry(
      instance_pin,
      PhysicalGeometryVisitor{.rectangle = [&](const PhysicalLayerRect& shape) { rectangles.push_back(shape); },
                              .polygon = [&](const PhysicalLayerPolygon& shape) { polygons.emplace_back(shape.points.begin(), shape.points.end()); },
                              .via = [&](const PhysicalViaPlacement& placement) { vias.push_back(placement); }});

  ASSERT_EQ(rectangles.size(), 1u);
  EXPECT_EQ(rectangles.front().layer, TechLayerId{m1.entity()});
  EXPECT_EQ(rectangles.front().rectangle, (Rect{102, 215, 106, 219}));
  ASSERT_EQ(polygons.size(), 1u);
  EXPECT_EQ(polygons.front(), (std::vector<Point>{{101, 218}, {101, 216}, {103, 216}, {103, 218}}));
  ASSERT_EQ(vias.size(), 1u);
  EXPECT_EQ(vias.front().tech_via, via);
  EXPECT_EQ(vias.front().origin, (Point{120, 210}));
  EXPECT_EQ(vias.front().orientation, DesignOrientation::kE);

  std::vector<PhysicalLayerRect> expanded_via;
  service->forEachTechViaGeometry(vias.front().tech_via, vias.front().origin, vias.front().orientation,
                                  PhysicalGeometryVisitor{.rectangle = [&](const PhysicalLayerRect& shape) { expanded_via.push_back(shape); }});
  ASSERT_EQ(expanded_via.size(), 3u);
  EXPECT_EQ(expanded_via.front().rectangle, (Rect{118, 208, 122, 212}));

  std::vector<PhysicalLayerRect> local_via_rectangles;
  service->forEachDesignViaGeometry(
      design_via, {10, 20}, DesignOrientation::kW,
      PhysicalGeometryVisitor{.rectangle = [&](const PhysicalLayerRect& shape) { local_via_rectangles.push_back(shape); }});
  ASSERT_EQ(local_via_rectangles.size(), 1u);
  EXPECT_EQ(local_via_rectangles.front().rectangle, (Rect{6, 21, 8, 23}));
  EXPECT_EQ(local_via_rectangles.front().mask, 2u);
}

TEST_F(PhysicalDesignServiceFixture, EnumeratesViaGenerateRuleClauses)
{
  std::size_t visited = 0;
  service->forEachTechViaRuleGenerate(
      [&](TechViaRuleGenerateId id, const TechViaRuleGenerate& rule, const TechViaRuleGenerateBottomLayer& bottom,
          const TechViaRuleGenerateCutLayer& cut_clause, const TechViaRuleGenerateTopLayer& top) {
        ++visited;
        EXPECT_EQ(id, generated_via_rule);
        EXPECT_EQ(rule.name, "VIA12_GEN");
        EXPECT_TRUE(rule.isDefault());
        EXPECT_EQ(bottom.layer, TechConductorLayerRef{m1});
        EXPECT_EQ(bottom.enclosure_overhang1, 1);
        EXPECT_EQ(bottom.enclosure_overhang2, 2);
        EXPECT_EQ(cut_clause.layer, cut);
        EXPECT_EQ(cut_clause.cut_rect, (Rect{-2, -3, 2, 3}));
        EXPECT_EQ(cut_clause.spacing_x, 2);
        EXPECT_EQ(cut_clause.spacing_y, 4);
        EXPECT_EQ(top.layer, TechConductorLayerRef{m2});
        EXPECT_EQ(top.enclosure_overhang1, 3);
        EXPECT_EQ(top.enclosure_overhang2, 4);
      });
  EXPECT_EQ(visited, 1u);
}

TEST_F(PhysicalDesignServiceFixture, ExpandsGeneratedDesignViaWithOriginOffsetsAndOrientation)
{
  const auto generated_via = design.routingStorage().createVia(
      DesignViaInput{.name = "LOCAL_GENERATED",
                     .flags = DesignViaFlag::kGenerated,
                     .generated = {.via_rule = generated_via_rule,
                                   .bottom_layer = m1,
                                   .cut_layer = cut,
                                   .top_layer = m2,
                                   .flags = DesignGeneratedViaFlag::kHasRowCol | DesignGeneratedViaFlag::kHasOrigin
                                            | DesignGeneratedViaFlag::kHasOffset,
                                   .cut_size_x = 4,
                                   .cut_size_y = 6,
                                   .cut_spacing_x = 2,
                                   .cut_spacing_y = 4,
                                   .bottom_enclosure_x = 1,
                                   .bottom_enclosure_y = 2,
                                   .top_enclosure_x = 3,
                                   .top_enclosure_y = 4,
                                   .row_count = 2,
                                   .column_count = 3,
                                   .origin = {1, -1},
                                   .bottom_offset = {3, 5},
                                   .top_offset = {-2, 4}}});

  std::vector<PhysicalLayerRect> rectangles;
  const auto visitor = PhysicalGeometryVisitor{.rectangle = [&](const PhysicalLayerRect& shape) { rectangles.push_back(shape); }};
  service->forEachDesignViaGeometry(generated_via, {100, 200}, DesignOrientation::kN, visitor);
  ASSERT_EQ(rectangles.size(), 8u);
  EXPECT_EQ(rectangles[0].layer, TechLayerId{m1.entity()});
  EXPECT_EQ(rectangles[0].rectangle, (Rect{95, 194, 113, 214}));
  EXPECT_EQ(rectangles[1].layer, TechLayerId{cut.entity()});
  EXPECT_EQ(rectangles[1].rectangle, (Rect{93, 191, 97, 197}));
  EXPECT_EQ(rectangles[6].rectangle, (Rect{105, 201, 109, 207}));
  EXPECT_EQ(rectangles[7].layer, TechLayerId{m2.entity()});
  EXPECT_EQ(rectangles[7].rectangle, (Rect{88, 191, 110, 215}));
  for (const auto& rectangle : rectangles) {
    EXPECT_EQ(rectangle.source, PhysicalGeometrySource::kDesignVia);
    EXPECT_EQ(rectangle.mask, 0u);
  }

  rectangles.clear();
  service->forEachDesignViaGeometry(generated_via, {100, 200}, DesignOrientation::kE, visitor);
  ASSERT_EQ(rectangles.size(), 8u);
  EXPECT_EQ(rectangles[0].rectangle, (Rect{94, 187, 114, 205}));
  EXPECT_EQ(rectangles[1].rectangle, (Rect{91, 203, 97, 207}));
}

TEST_F(PhysicalDesignServiceFixture, EnumeratesRegularAndSpecialNetPinsAndNdr)
{
  std::vector<DesignNetId> regular_nets;
  std::vector<DesignNetId> special_nets;
  service->forEachRegularNet([&](DesignNetId id, const DesignNet&) { regular_nets.push_back(id); });
  service->forEachSpecialNet([&](DesignNetId id, const DesignNet&) { special_nets.push_back(id); });
  EXPECT_EQ(regular_nets, (std::vector<DesignNetId>{regular}));
  EXPECT_EQ(special_nets, (std::vector<DesignNetId>{special}));
  EXPECT_EQ(service->findRegularNet("signal"), regular);
  EXPECT_FALSE(service->findRegularNet("VDD"));
  EXPECT_EQ(service->findInstance("u0"), instance);
  EXPECT_FALSE(service->findInstance("missing"));
  EXPECT_EQ(service->findInstancePin(instance, "A"), instance_pin);
  EXPECT_FALSE(service->findInstancePin(instance, "Y"));
  EXPECT_EQ(service->findIoPin("in"), io_regular);
  EXPECT_FALSE(service->findIoPin("missing"));
  EXPECT_EQ(service->findMaster("CELL"), master);
  EXPECT_FALSE(service->findMaster("missing"));
  EXPECT_EQ(service->findMasterTerm(master, "A"), term);
  EXPECT_FALSE(service->findMasterTerm(master, "missing"));
  const auto ports = service->masterPorts(term);
  ASSERT_EQ(ports.size(), 2u);
  EXPECT_EQ(ports[0], first_port);
  EXPECT_EQ(ports[1], second_port);

  std::vector<DesignInstancePinId> instance_pins;
  std::vector<DesignIoPinId> io_pins;
  service->forEachNetPin(
      regular,
      PhysicalNetPinVisitor{.instance_pin = [&](DesignInstancePinId id, const DesignInstancePin& pin, const DesignInstance& owner,
                                                 const LibraryMasterTerm& master_term) {
                                instance_pins.push_back(id);
                                EXPECT_EQ(owner.name, "u0");
                                EXPECT_EQ(master_term.name, "A");
                                EXPECT_EQ(pin.instance, instance);
                                EXPECT_EQ(pin.master_term, term);
                                EXPECT_EQ(pin.net, regular);
                              },
                             .io_pin = [&](DesignIoPinId id, const DesignIoPin&) { io_pins.push_back(id); }});
  EXPECT_EQ(instance_pins, (std::vector<DesignInstancePinId>{instance_pin}));
  EXPECT_EQ(io_pins, (std::vector<DesignIoPinId>{io_regular}));

  std::vector<DesignIoPinId> special_io_pins;
  service->forEachNetPin(special, PhysicalNetPinVisitor{.io_pin = [&](DesignIoPinId id, const DesignIoPin&) { special_io_pins.push_back(id); }});
  EXPECT_EQ(special_io_pins, (std::vector<DesignIoPinId>{io_special}));
  const auto ndr = service->nonDefaultRule(regular);
  EXPECT_FALSE(ndr.tech_rule);
  EXPECT_EQ(ndr.design_rule, design_ndr);
}

TEST_F(PhysicalDesignServiceFixture, ReadsNetJumpers)
{
  EXPECT_FALSE(service->netHasJumpers(regular));
  const auto jumper_net = design.netlistStorage().createNet(DesignNet{.name = "jumpers", .flags = DesignNetFlag::kHasJumpers});
  EXPECT_TRUE(service->netHasJumpers(jumper_net));
}

TEST_F(PhysicalDesignServiceFixture, IdentifiesOnlyOdbCompatibleAbutmentNets)
{
  auto& netlist = design.netlistStorage();
  const auto block_master = library.cellMasterStorage().createCellMaster(
      LibraryCellMaster{.name = "BLOCK", .type = LibraryCellMasterType::kBlock, .width = 20, .height = 20});
  const auto block_term = library.masterTermStorage().createMasterTerm(
      block_master, LibraryMasterTerm{.name = "A", .direction = LibraryMasterTermDirection::kInput});
  static_cast<void>(library.masterPortStorage().createMasterPort(
      block_term, LibraryMasterPortInput{.layer_clauses = {{.layer = TechLayerId{m1.entity()}, .geometry = {.rects = {{1, 2, 5, 6}}}}}}));

  const auto blackbox_master = library.cellMasterStorage().createCellMaster(
      LibraryCellMaster{.name = "BLOCK_BLACKBOX", .type = LibraryCellMasterType::kBlockBlackbox, .width = 20, .height = 20});
  const auto blackbox_term = library.masterTermStorage().createMasterTerm(
      blackbox_master, LibraryMasterTerm{.name = "A", .direction = LibraryMasterTermDirection::kInput});
  static_cast<void>(library.masterPortStorage().createMasterPort(
      blackbox_term,
      LibraryMasterPortInput{.layer_clauses = {{.layer = TechLayerId{m2.entity()}, .geometry = {.rects = {{1, 2, 5, 6}}}}}}));

  const auto first_block = netlist.createInstance(DesignInstance{.name = "block_first",
                                                                    .master = block_master,
                                                                    .origin = {100, 200},
                                                                    .placement_status = DesignPlacementStatus::kPlaced});
  const auto second_block = netlist.createInstance(DesignInstance{.name = "block_second",
                                                                     .master = blackbox_master,
                                                                     .origin = {90, 188},
                                                                     .orientation = DesignOrientation::kS,
                                                                     .placement_status = DesignPlacementStatus::kPlaced});
  const auto first_block_pin = netlist.findInstancePin(first_block, block_term);
  const auto second_block_pin = netlist.findInstancePin(second_block, blackbox_term);
  ASSERT_TRUE(first_block_pin);
  ASSERT_TRUE(second_block_pin);
  const auto abutment_net = netlist.createNet(DesignNet{.name = "abutment"});
  netlist.connect(first_block_pin, abutment_net);
  netlist.connect(second_block_pin, abutment_net);
  EXPECT_TRUE(service->isNetConnectedByAbutment(abutment_net));

  const auto non_block_first = netlist.createInstance(DesignInstance{.name = "core_first",
                                                                        .master = master,
                                                                        .origin = {100, 200},
                                                                        .placement_status = DesignPlacementStatus::kPlaced});
  const auto non_block_second = netlist.createInstance(DesignInstance{.name = "core_second",
                                                                         .master = master,
                                                                         .origin = {100, 200},
                                                                         .placement_status = DesignPlacementStatus::kPlaced});
  const auto non_block_first_pin = netlist.findInstancePin(non_block_first, term);
  const auto non_block_second_pin = netlist.findInstancePin(non_block_second, term);
  ASSERT_TRUE(non_block_first_pin);
  ASSERT_TRUE(non_block_second_pin);
  const auto non_block_net = netlist.createNet(DesignNet{.name = "non_block"});
  netlist.connect(non_block_first_pin, non_block_net);
  netlist.connect(non_block_second_pin, non_block_net);
  EXPECT_FALSE(service->isNetConnectedByAbutment(non_block_net));

  const auto make_block_instance = [&](std::string name, Point origin,
                                       DesignPlacementStatus placement_status = DesignPlacementStatus::kPlaced) {
    return netlist.createInstance(DesignInstance{.name = std::move(name),
                                                  .master = block_master,
                                                  .origin = origin,
                                                  .placement_status = placement_status});
  };
  const auto bterm_first = make_block_instance("bterm_first", {300, 300});
  const auto bterm_second = make_block_instance("bterm_second", {300, 300});
  const auto bterm_first_pin = netlist.findInstancePin(bterm_first, block_term);
  const auto bterm_second_pin = netlist.findInstancePin(bterm_second, block_term);
  ASSERT_TRUE(bterm_first_pin);
  ASSERT_TRUE(bterm_second_pin);
  const auto bterm_net = netlist.createNet(DesignNet{.name = "with_bterm"});
  netlist.connect(bterm_first_pin, bterm_net);
  netlist.connect(bterm_second_pin, bterm_net);
  const auto bterm = netlist.createIoPin(DesignIoPin{.name = "BTERM"});
  netlist.connect(bterm, bterm_net);
  EXPECT_FALSE(service->isNetConnectedByAbutment(bterm_net));

  const auto overfull_first = make_block_instance("overfull_first", {400, 400});
  const auto overfull_second = make_block_instance("overfull_second", {400, 400});
  const auto overfull_third = make_block_instance("overfull_third", {400, 400});
  const auto overfull_first_pin = netlist.findInstancePin(overfull_first, block_term);
  const auto overfull_second_pin = netlist.findInstancePin(overfull_second, block_term);
  const auto overfull_third_pin = netlist.findInstancePin(overfull_third, block_term);
  ASSERT_TRUE(overfull_first_pin);
  ASSERT_TRUE(overfull_second_pin);
  ASSERT_TRUE(overfull_third_pin);
  const auto overfull_net = netlist.createNet(DesignNet{.name = "overfull"});
  netlist.connect(overfull_first_pin, overfull_net);
  netlist.connect(overfull_second_pin, overfull_net);
  netlist.connect(overfull_third_pin, overfull_net);
  EXPECT_FALSE(service->isNetConnectedByAbutment(overfull_net));

  const auto unplaced_block = make_block_instance("unplaced", {500, 500}, DesignPlacementStatus::kUnplaced);
  const auto placed_block = make_block_instance("placed", {500, 500});
  const auto unplaced_block_pin = netlist.findInstancePin(unplaced_block, block_term);
  const auto placed_block_pin = netlist.findInstancePin(placed_block, block_term);
  ASSERT_TRUE(unplaced_block_pin);
  ASSERT_TRUE(placed_block_pin);
  const auto unplaced_net = netlist.createNet(DesignNet{.name = "unplaced"});
  netlist.connect(unplaced_block_pin, unplaced_net);
  netlist.connect(placed_block_pin, unplaced_net);
  EXPECT_FALSE(service->isNetConnectedByAbutment(unplaced_net));
}

TEST_F(PhysicalDesignServiceFixture, RejectsRoutingPolygonsInBlockAbutmentQuery)
{
  auto& netlist = design.netlistStorage();
  const auto block_soft_master = library.cellMasterStorage().createCellMaster(
      LibraryCellMaster{.name = "BLOCK_SOFT", .type = LibraryCellMasterType::kBlockSoft, .width = 20, .height = 20});
  const auto block_soft_term = library.masterTermStorage().createMasterTerm(
      block_soft_master, LibraryMasterTerm{.name = "A", .direction = LibraryMasterTermDirection::kInput});
  static_cast<void>(library.masterPortStorage().createMasterPort(
      block_soft_term,
      LibraryMasterPortInput{.layer_clauses = {{.layer = TechLayerId{m1.entity()},
                                                 .geometry = {.polygons = {{.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}}}}}}}}));
  const auto block_soft_instance = netlist.createInstance(DesignInstance{.name = "block_soft",
                                                                            .master = block_soft_master,
                                                                            .placement_status = DesignPlacementStatus::kPlaced});
  const auto block_soft_pin = netlist.findInstancePin(block_soft_instance, block_soft_term);
  ASSERT_TRUE(block_soft_pin);
  const auto polygon_net = netlist.createNet(DesignNet{.name = "polygon"});
  netlist.connect(block_soft_pin, polygon_net);
  EXPECT_THROW((void) service->isNetConnectedByAbutment(polygon_net), std::logic_error);
}

TEST_F(PhysicalDesignServiceFixture, PreservesIndependentBlockPinGeometryAndPlacements)
{
  auto& netlist = design.netlistStorage();
  const auto terminal = netlist.createIoPin(
      DesignBlockTerm{.name = "two_physical_pins"},
      {{.flags = DesignIoPinPortFlag::kExplicit | DesignIoPinPortFlag::kHasPlacement,
        .placement_status = DesignPlacementStatus::kFixed,
        .origin = {100, 200},
        .rectangles = {{.layer = TechLayerId{m1.entity()}, .rectangle = {0, 0, 10, 20}}}},
       {.flags = DesignIoPinPortFlag::kExplicit | DesignIoPinPortFlag::kHasPlacement,
        .placement_status = DesignPlacementStatus::kFixed,
        .origin = {300, 400},
        .rectangles = {{.layer = TechLayerId{m2.entity()}, .rectangle = {0, 0, 20, 10}}}}});
  const auto ids = netlist.blockPins(terminal);
  ASSERT_EQ(ids.size(), 2u);
  std::vector<Rect> rectangles;
  const PhysicalGeometryVisitor visitor{.rectangle = [&](const PhysicalLayerRect& shape) {
    rectangles.push_back(shape.rectangle);
  }};
  service->forEachBlockPinGeometry(ids[0], visitor);
  EXPECT_EQ(rectangles, (std::vector<Rect>{{100, 200, 110, 220}}));
  rectangles.clear();
  service->forEachBlockPinGeometry(ids[1], visitor);
  EXPECT_EQ(rectangles, (std::vector<Rect>{{300, 400, 320, 410}}));
  rectangles.clear();
  service->forEachIoPinGeometry(terminal, visitor);
  EXPECT_EQ(rectangles, (std::vector<Rect>{{100, 200, 110, 220}, {300, 400, 320, 410}}));
}

TEST_F(PhysicalDesignServiceFixture, SupportsPartialNetPinVisitors)
{
  std::vector<DesignInstancePinId> instance_pins;
  service->forEachNetPin(
      regular,
      PhysicalNetPinVisitor{.instance_pin = [&](DesignInstancePinId id, const DesignInstancePin&, const DesignInstance&,
                                                 const LibraryMasterTerm&) { instance_pins.push_back(id); }});
  EXPECT_EQ(instance_pins, (std::vector<DesignInstancePinId>{instance_pin}));

  std::vector<DesignIoPinId> io_pins;
  service->forEachNetPin(regular, PhysicalNetPinVisitor{.io_pin = [&](DesignIoPinId id, const DesignIoPin&) { io_pins.push_back(id); }});
  EXPECT_EQ(io_pins, (std::vector<DesignIoPinId>{io_regular}));
}

TEST_F(PhysicalDesignServiceFixture, ResolvesMasterTermByIdAfterUnrelatedTermCreation)
{
  const auto unrelated_master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "UNRELATED"});
  static_cast<void>(library.masterTermStorage().createMasterTerm(unrelated_master, LibraryMasterTerm{.name = "A"}));
  std::size_t visited = 0;
  service->forEachNetPin(regular,
                        PhysicalNetPinVisitor{.instance_pin = [&](DesignInstancePinId id, const DesignInstancePin& pin,
                                                                 const DesignInstance& owner, const LibraryMasterTerm& value) {
                          EXPECT_EQ(id, instance_pin);
                          EXPECT_EQ(owner.name, "u0");
                          EXPECT_EQ(pin.instance, instance);
                          EXPECT_EQ(pin.master_term, term);
                          EXPECT_EQ(value.name, "A");
                          ++visited;
                        }});
  EXPECT_EQ(visited, 1u);
}

TEST_F(PhysicalDesignServiceFixture, ReturnsPlacedMasterObsAndRoutingBlockages)
{
  std::vector<PhysicalLayerRect> master_obstructions;
  service->forEachPlacedMasterObstructionGeometry(
      instance, PhysicalGeometryVisitor{.rectangle = [&](const PhysicalLayerRect& shape) { master_obstructions.push_back(shape); }});
  ASSERT_EQ(master_obstructions.size(), 1u);
  EXPECT_EQ(master_obstructions.front().rectangle, (Rect{108, 209, 112, 213}));
  EXPECT_EQ(master_obstructions.front().source, PhysicalGeometrySource::kMasterObstruction);
  EXPECT_EQ(master_obstructions.front().spacing, 7);

  std::vector<DesignBlockageId> blockages;
  service->forEachRoutingBlockage([&](DesignBlockageId id, const DesignBlockage& value) {
    blockages.push_back(id);
    EXPECT_EQ(value.layer, m2);
  });
  EXPECT_EQ(blockages, (std::vector<DesignBlockageId>{blockage}));

  std::vector<PhysicalLayerRect> blockage_rectangles;
  std::vector<std::vector<Point>> blockage_polygons;
  service->forEachRoutingBlockageGeometry(
      blockage,
      PhysicalGeometryVisitor{.rectangle = [&](const PhysicalLayerRect& shape) { blockage_rectangles.push_back(shape); },
                              .polygon = [&](const PhysicalLayerPolygon& shape) {
                                blockage_polygons.emplace_back(shape.points.begin(), shape.points.end());
                              }});
  ASSERT_EQ(blockage_rectangles.size(), 1u);
  EXPECT_EQ(blockage_rectangles.front().layer, TechLayerId{m2.entity()});
  EXPECT_EQ(blockage_rectangles.front().rectangle, (Rect{300, 310, 340, 360}));
  EXPECT_EQ(blockage_rectangles.front().mask, 3u);
  ASSERT_EQ(blockage_polygons.size(), 1u);
  EXPECT_EQ(blockage_polygons.front(), (std::vector<Point>{{400, 400}, {420, 400}, {420, 430}, {400, 430}}));
}

}  // namespace
}  // namespace eccdb
