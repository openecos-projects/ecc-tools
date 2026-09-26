// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "library/cell_master/storage/MasterObsInput.h"
#include "library/master_port/storage/MasterPortInput.h"
#include "service/query/PhysicalDesignService.h"
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
    design.globalStorage().setDieArea(DesignDieArea{.boundary = {{0, 0}, {1000, 0}, {1000, 800}, {0, 800}}});
    static_cast<void>(design.floorplanStorage().createTrackGrid(
        DesignTrackGrid{.axis = DesignAxis::kX, .start = 20, .track_count = 3, .step = 40, .layers = {m1}}));
    static_cast<void>(design.floorplanStorage().createGCellGrid(
        DesignGCellGrid{.axis = DesignAxis::kY, .start = 0, .line_count = 3, .step = 200}));

    design_ndr = design.routingStorage().createNonDefaultRule(
        DesignNonDefaultRule{.name = "WIDE", .layer_rules = {{.layer = TechLayerId{m1.entity()}, .width = 20}}});
    design_via = design.routingStorage().createVia(
        DesignVia{.name = "LOCAL_VIA",
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
        DesignBlockage{.kind = DesignBlockageKind::kRouting,
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
                                EXPECT_EQ(pin.master_term_ptr, &master_term);
                                EXPECT_EQ(pin.master_term_ptr, &library.masterTermStorage().masterTerm(term));
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

TEST_F(PhysicalDesignServiceFixture, ReadsCachedMasterTermAfterUnrelatedTermCreation)
{
  const auto unrelated_master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "UNRELATED"});
  const auto* cached_term = design.netlistStorage().instancePin(instance_pin).master_term_ptr;
  static_cast<void>(library.masterTermStorage().createMasterTerm(unrelated_master, LibraryMasterTerm{.name = "A"}));
  std::size_t visited = 0;
  service->forEachNetPin(regular,
                        PhysicalNetPinVisitor{.instance_pin = [&](DesignInstancePinId id, const DesignInstancePin&,
                                                                 const DesignInstance& owner, const LibraryMasterTerm& value) {
                          EXPECT_EQ(id, instance_pin);
                          EXPECT_EQ(owner.name, "u0");
                          EXPECT_EQ(&value, cached_term);
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
