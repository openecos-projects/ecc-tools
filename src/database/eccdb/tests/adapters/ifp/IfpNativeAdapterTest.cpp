// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include <gtest/gtest.h>

#include <span>

#include "DataManager.hpp"
#include "FPInterface.hpp"
#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/via/component/ViaComponents.h"
#include "io/def/DefDesignExporter.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_port/storage/MasterPortInput.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "library/site/model/SiteComponents.h"
#include "service/connectivity/ConnectivityService.h"
#include "service/floorplan/FloorplanService.h"
#include "service/instance/InstanceService.h"
#include "service/routing/RoutingService.h"
#include "tech/TechStore.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace {

class IfpNativeAdapterTest : public testing::Test
{
 protected:
  IfpNativeAdapterTest()
      : library(technology.techRegistry()),
        design(technology.techRegistry(), library.libraryRegistry(), &technology.geometryRegistry()),
        context(technology, library, design)
  {
  }

  void SetUp() override
  {
    routing_layer = technology.createRoutingLayer(
        {.name = "M1"},
        {.direction = eccdb::TechRoutingDirection::kHorizontal, .width = 10, .min_width = 10, .pitch_x = 20, .pitch_y = 20, .offset_x = 10, .offset_y = 10});
    technology.globalStorage().setManufacturingGrid(1);
    site = library.siteStorage().createSite(
        {.name = "CORE", .width = 10, .height = 10, .site_class = eccdb::LibrarySiteClass::kCore});
    master = library.cellMasterStorage().createCellMaster(
        {.name = "CORECELL", .type = eccdb::LibraryCellMasterType::kCore, .site = site, .width = 20, .height = 10});
    vdd_term = library.masterTermStorage().createMasterTerm(
        master, {.name = "VDD", .direction = eccdb::LibraryMasterTermDirection::kInOut, .use = eccdb::LibraryMasterTermUse::kPower});

    design.globalStorage().setInfo({.name = "ifp-native", .database_units_per_micron = 1000});
    eccdb::FloorplanService floorplan{context};
    floorplan.setDieArea({.boundary = {{0, 0}, {100, 0}, {100, 100}, {0, 100}}});
    floorplan.setCoreArea({.boundary = {{0, 0}, {100, 0}, {100, 20}, {0, 20}}});
    static_cast<void>(floorplan.createRow({.name = "ROW0",
                                           .site = site,
                                           .origin = {0, 0},
                                           .orientation = eccdb::DesignOrientation::kN,
                                           .repeat_count_x = 10,
                                           .repeat_count_y = 1,
                                           .step_x = 10,
                                           .step_y = 0,
                                           .flags = eccdb::DesignRowFlag::kHasDo | eccdb::DesignRowFlag::kHasStep}));
    static_cast<void>(floorplan.createTrackGrid(
        {.axis = eccdb::DesignAxis::kY, .start = 10, .track_count = 5, .step = 20, .layers = {routing_layer}}));

    eccdb::InstanceService instances{context};
    instance = instances.create({.name = "u0",
                                 .master = master,
                                 .origin = {10, 10},
                                 .orientation = eccdb::DesignOrientation::kN,
                                 .placement_status = eccdb::DesignPlacementStatus::kPlaced});
    eccdb::ConnectivityService connectivity{context};
    vdd = connectivity.createNet({.name = "VDD", .use = eccdb::DesignSignalUse::kPower}, true);
    const auto block_term = connectivity.createBlockTerm(
        {.name = "VDD", .direction = eccdb::DesignIoPinDirection::kInOut, .use = eccdb::DesignSignalUse::kPower});
    connectivity.connect(block_term, vdd);
    connectivity.connect(design.netlistStorage().findInstanceTerm(instance, vdd_term), vdd);

    eccdb::RoutingService routing{context};
    const auto wire = routing.createWire({.net = vdd, .status = eccdb::DesignWireStatus::kFixed});
    static_cast<void>(routing.createShape(
        wire,
        {.wire = wire,
         .layer = routing_layer,
         .flags = eccdb::DesignWirePathFlag::kHasWidth,
         .geometry = eccdb::DesignRouteSegment{.begin = {.position = {0, 10}}, .end = {.position = {100, 10}}}},
        eccdb::DesignRouteShapeAttributes{.width = 10}));

    ifp::DataManager::initInst();
    FPI.setDesignSource(context);
  }

  void TearDown() override
  {
    FPI.setDesignSource(static_cast<eccdb::DatabaseContext*>(nullptr));
    ifp::DataManager::destroyInst();
    ifp::FPInterface::destroyInst();
  }

  eccdb::TechStore technology;
  eccdb::LibraryStore library;
  eccdb::DesignStore design;
  eccdb::DatabaseContext context;
  eccdb::TechRoutingLayerId routing_layer;
  eccdb::LibrarySiteId site;
  eccdb::LibraryCellMasterId master;
  eccdb::LibraryMasterTermId vdd_term;
  eccdb::DesignInstanceId instance;
  eccdb::DesignNetId vdd;
};

TEST(IfpTrackGridTest, IncludesDieBoundaryAndKeepsPhaseForNegativeOrigins)
{
  const auto positive = ifp::boundedTrackGrid(100, 1100, 0, 200);
  EXPECT_EQ(positive.start, 200);
  EXPECT_EQ(positive.count, 5);
  const auto inclusive = ifp::boundedTrackGrid(200, 1000, 0, 200);
  EXPECT_EQ(inclusive.start, 200);
  EXPECT_EQ(inclusive.count, 5);
  const auto negative = ifp::boundedTrackGrid(-100, 100, 10, 20);
  EXPECT_EQ(negative.start, -90);
  EXPECT_EQ(negative.count, 10);
  EXPECT_EQ(ifp::boundedTrackGrid(11, 19, 10, 20).count, 0);
}

TEST_F(IfpNativeAdapterTest, TrackWritebackAlignsPhaseInsideNonzeroDieAndRetainsLastTrack)
{
  eccdb::FloorplanService floorplan{context};
  floorplan.setDieArea({.boundary = {{100, 100}, {200, 100}, {200, 200}, {100, 200}}});
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& database = FPDM.getDatabase();
  database.get_new_track_list().clear();
  ifp::Track track;
  track.set_layer_name("M1");
  track.set_x_offset(10);
  track.set_y_offset(10);
  track.set_x_pitch(20);
  track.set_y_pitch(20);
  database.get_new_track_list().push_back(track);
  database.set_track_updated(true);
  ASSERT_NO_THROW(FPI.output());
  const auto grids = design.floorplanStorage().trackGrids();
  ASSERT_EQ(grids.size(), 2u);
  for (const auto id : grids) {
    const auto& grid = design.floorplanStorage().trackGrid(id);
    EXPECT_EQ(grid.start, 110);
    EXPECT_EQ(grid.track_count, 5u);
    EXPECT_EQ(grid.start + (grid.track_count - 1) * grid.step, 190);
  }
  database.set_track_updated(true);
  ASSERT_NO_THROW(FPI.output());
  EXPECT_EQ(design.floorplanStorage().trackGridCount(), 2u);
}

TEST_F(IfpNativeAdapterTest, IoPortWritebackAppliesPlacementOnlyOnce)
{
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& pin = FPDM.getDatabase().get_io_pin_list().front();
  pin.set_coord(40, 50);
  pin.set_orient(ifp::PlacementOrientation::kN);
  pin.set_placed(true);
  pin.set_updated(true);
  ifp::IOPort port;
  port.set_layer_name("M1");
  port.set_coord(40, 50);
  port.set_rect(-5, -2, 5, 2);
  port.set_placed(true);
  pin.get_new_port_list().push_back(port);

  ASSERT_NO_THROW(FPI.output());
  const auto term = design.netlistStorage().findBlockTerm("VDD");
  const auto ports = design.netlistStorage().blockPins(term);
  ASSERT_EQ(ports.size(), 1u);
  const auto& stored = design.netlistStorage().blockPin(ports.front());
  ASSERT_EQ(stored.rectangles.size(), 1u);
  const auto local = design.netlistStorage().blockPinRectangle(stored.rectangles.front()).rectangle;
  EXPECT_EQ(local.ll_x, -5);
  EXPECT_EQ(local.ll_y, -2);
  EXPECT_EQ(local.ur_x, 5);
  EXPECT_EQ(local.ur_y, 2);
  const auto physical = design.geometryStorage().bounds(stored.rectangles.front());
  EXPECT_EQ(physical.ll_x, 35);
  EXPECT_EQ(physical.ll_y, 48);
  EXPECT_EQ(physical.ur_x, 45);
  EXPECT_EQ(physical.ur_y, 52);
}

TEST_F(IfpNativeAdapterTest, AppendingExplicitIoPortPreservesExistingPortPlacement)
{
  const auto term = design.netlistStorage().findBlockTerm("VDD");
  eccdb::FloorplanService floorplan{context};
  eccdb::DesignBlockPinInput input{.flags = eccdb::DesignIoPinPortFlag::kExplicit | eccdb::DesignIoPinPortFlag::kHasPlacement,
                                   .placement_status = eccdb::DesignPlacementStatus::kPlaced,
                                   .origin = {10, 20},
                                   .orientation = eccdb::DesignOrientation::kN};
  input.rectangles.push_back({.layer = eccdb::TechLayerId{routing_layer.entity()}, .rectangle = {-5, -2, 5, 2}});
  const auto existing = floorplan.createBlockPin(term, std::move(input));
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& pin = FPDM.getDatabase().get_io_pin_list().front();
  pin.set_coord(40, 50);
  pin.set_placed(true);
  pin.set_updated(true);
  ifp::IOPort added;
  added.set_layer_name("M1");
  added.set_coord(40, 50);
  added.set_rect(-5, -2, 5, 2);
  added.set_placed(true);
  pin.get_new_port_list().push_back(added);
  ASSERT_NO_THROW(FPI.output());
  const auto& retained = design.netlistStorage().blockPin(existing);
  EXPECT_EQ(retained.origin.x, 10);
  EXPECT_EQ(retained.origin.y, 20);
  const auto bounds = design.geometryStorage().bounds(retained.rectangles.front());
  EXPECT_EQ(bounds.ll_x, 5);
  EXPECT_EQ(bounds.ll_y, 18);
  EXPECT_EQ(bounds.ur_x, 15);
  EXPECT_EQ(bounds.ur_y, 22);
  EXPECT_EQ(design.netlistStorage().blockPins(term).size(), 2u);
}

TEST_F(IfpNativeAdapterTest, WrapsAndWritesFloorplanInstanceAndPowerGridNatively)
{
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& database = FPDM.getDatabase();
  ASSERT_EQ(database.get_instance_list().size(), 1u);
  ASSERT_EQ(database.get_pg_net_list().size(), 1u);
  // Existing SPECIALNET geometry remains in EccDB but is not an iFP PG
  // work item.  Legacy iFP starts its generated-PG list empty as well.
  ASSERT_TRUE(database.get_pg_segment_list().empty());
  EXPECT_EQ(database.get_pg_net_list().front().get_name(), "VDD");
  ASSERT_EQ(design.routingStorage().wires(vdd).size(), 1u);

  database.get_die().set_rect(-10, -10, 110, 110);
  database.set_die_updated(true);
  auto& wrapped_instance = database.get_instance_list().front();
  wrapped_instance.set_coord(40, 20);
  wrapped_instance.set_placed(true);
  wrapped_instance.set_placement_updated(true);

  ifp::PGSegment generated;
  generated.set_net_name("VDD");
  generated.set_layer_name("M1");
  generated.set_type(ifp::PGSegmentType::kStripe);
  generated.set_width(10);
  generated.set_start_coord(10, 50);
  generated.set_end_coord(90, 50);
  generated.set_generated(true);
  database.get_pg_segment_list().push_back(generated);

  ASSERT_NO_THROW(FPI.output());

  const auto die = design.globalStorage().dieBounds();
  EXPECT_EQ(die.ll_x, -10);
  EXPECT_EQ(die.ur_y, 110);
  const auto& native_instance = design.netlistStorage().instance(instance);
  EXPECT_EQ(native_instance.origin.x, 40);
  EXPECT_EQ(native_instance.origin.y, 20);
  // The original fixed SPECIALNET route remains alongside the newly
  // generated stripe.  It was not silently erased or used as an input seed.
  EXPECT_EQ(design.routingStorage().wires(vdd).size(), 2u);
}

TEST_F(IfpNativeAdapterTest, WritesGeneratedPowerShapeSemanticsToDef)
{
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& database = FPDM.getDatabase();

  ifp::PGSegment followpin;
  followpin.set_net_name("VDD");
  followpin.set_layer_name("M1");
  followpin.set_type(ifp::PGSegmentType::kFollowPin);
  followpin.set_width(10);
  followpin.set_start_coord(10, 30);
  followpin.set_end_coord(90, 30);
  followpin.set_generated(true);
  database.get_pg_segment_list().push_back(std::move(followpin));

  ifp::PGSegment stripe;
  stripe.set_net_name("VDD");
  stripe.set_layer_name("M1");
  stripe.set_type(ifp::PGSegmentType::kStripe);
  stripe.set_width(10);
  stripe.set_start_coord(10, 50);
  stripe.set_end_coord(90, 50);
  stripe.set_generated(true);
  database.get_pg_segment_list().push_back(std::move(stripe));

  ASSERT_NO_THROW(FPI.output());
  bool saw_followpin = false;
  bool saw_stripe = false;
  for (const auto wire : design.routingStorage().wires(vdd)) {
    design.routingStorage().forEachShape(
        wire, [&](eccdb::GeometryShapeId, const eccdb::DesignRouteShape& shape,
                  const eccdb::DesignRouteShapeAttributes* attributes) {
          const auto* segment = std::get_if<eccdb::DesignRouteSegment>(&shape.geometry);
          if (segment == nullptr || (segment->begin.position.y != 30 && segment->begin.position.y != 50)) {
            return;
          }
          EXPECT_NE(shape.flags & eccdb::DesignWirePathFlag::kHasShape, 0u);
          ASSERT_NE(attributes, nullptr);
          if (attributes->shape == "FOLLOWPIN") {
            saw_followpin = true;
          }
          if (attributes->shape == "STRIPE") {
            saw_stripe = true;
          }
        });
  }
  EXPECT_TRUE(saw_followpin);
  EXPECT_TRUE(saw_stripe);

  const auto def = eccdb::DefDesignExporter{design}.exportText();
  EXPECT_NE(def.find("+ SHAPE FOLLOWPIN"), std::string::npos);
  EXPECT_NE(def.find("+ SHAPE STRIPE"), std::string::npos);
}

TEST_F(IfpNativeAdapterTest, WrapsMacroPortsAndWritesRouteHalo)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto macro_master = library.cellMasterStorage().createCellMaster(
      {.name = "MACRO", .type = eccdb::LibraryCellMasterType::kBlock, .width = 20, .height = 10});
  const auto macro_term = library.masterTermStorage().createMasterTerm(
      macro_master,
      {.name = "VDD", .direction = eccdb::LibraryMasterTermDirection::kInOut, .use = eccdb::LibraryMasterTermUse::kPower});
  static_cast<void>(library.masterPortStorage().createMasterPin(
      macro_term,
      eccdb::LibraryMasterPortInput{.layer_clauses = {
          {.layer = eccdb::TechLayerId{routing_layer.entity()}, .geometry = {.rects = {{1, 2, 5, 6}}}},
          {.layer = eccdb::TechLayerId{cut.entity()}, .geometry = {.rects = {{0, 0, 2, 2}}}},
      }}));

  eccdb::InstanceService instances{context};
  const auto placed = instances.create({.name = "macro_e",
                                        .master = macro_master,
                                        .origin = {50, 40},
                                        .orientation = eccdb::DesignOrientation::kE,
                                        .placement_status = eccdb::DesignPlacementStatus::kFixed});
  static_cast<void>(instances.create({.name = "macro_unplaced",
                                      .master = macro_master,
                                      .placement_status = eccdb::DesignPlacementStatus::kUnplaced}));
  eccdb::ConnectivityService connectivity{context};
  connectivity.connect(design.netlistStorage().findInstanceTerm(placed, macro_term), vdd);

  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& database = FPDM.getDatabase();
  ifp::Instance* placed_macro = nullptr;
  const ifp::Instance* unplaced_macro = nullptr;
  for (auto& candidate : database.get_instance_list()) {
    if (candidate.get_name() == "macro_e") {
      placed_macro = &candidate;
    }
    if (candidate.get_name() == "macro_unplaced") {
      unplaced_macro = &candidate;
    }
  }
  ASSERT_NE(placed_macro, nullptr);
  ASSERT_NE(unplaced_macro, nullptr);
  ASSERT_EQ(placed_macro->get_pin_shape_list().size(), 1u);
  ASSERT_EQ(unplaced_macro->get_pin_shape_list().size(), 1u);

  const auto& placed_shape = placed_macro->get_pin_shape_list().front();
  EXPECT_EQ(placed_shape.get_pin_name(), "VDD");
  EXPECT_EQ(placed_shape.get_layer_name(), "M1");
  // E rotates the local PORT rectangle (1,2)-(5,6) in a 20x10 master.
  EXPECT_EQ(placed_shape.get_ll_x(), 52);
  EXPECT_EQ(placed_shape.get_ll_y(), 55);
  EXPECT_EQ(placed_shape.get_ur_x(), 56);
  EXPECT_EQ(placed_shape.get_ur_y(), 59);

  const auto& unplaced_shape = unplaced_macro->get_pin_shape_list().front();
  EXPECT_EQ(unplaced_shape.get_layer_name(), "M1");
  EXPECT_EQ(unplaced_shape.get_ll_x(), 1);
  EXPECT_EQ(unplaced_shape.get_ll_y(), 2);
  EXPECT_EQ(unplaced_shape.get_ur_x(), 5);
  EXPECT_EQ(unplaced_shape.get_ur_y(), 6);

  // The placed macro bounds are (50,40)-(60,60) after E orientation.
  placed_macro->get_routing_halo_rect().set_rect(45, 35, 65, 65);
  ASSERT_NO_THROW(FPI.output());

  const auto& native_macro = design.netlistStorage().instance(placed);
  EXPECT_NE(native_macro.flags & eccdb::DesignInstanceFlag::kHasRouteHalo, 0u);
  EXPECT_EQ(native_macro.route_halo.distance, 5);
  EXPECT_EQ(native_macro.route_halo.min_layer, routing_layer);
  EXPECT_EQ(native_macro.route_halo.max_layer, routing_layer);
  const auto def = eccdb::DefDesignExporter{design}.exportText();
  EXPECT_NE(def.find("+ ROUTEHALO 5 M1 M1"), std::string::npos);

  // A zero-distance ROUTEHALO still has a meaningful layer range in DEF.
  // iFP uses the macro bounds themselves for this no-margin case.
  placed_macro->get_routing_halo_rect().set_rect(50, 40, 60, 60);
  ASSERT_NO_THROW(FPI.output());
  const auto& zero_halo_macro = design.netlistStorage().instance(placed);
  EXPECT_NE(zero_halo_macro.flags & eccdb::DesignInstanceFlag::kHasRouteHalo, 0u);
  EXPECT_EQ(zero_halo_macro.route_halo.distance, 0);
  EXPECT_EQ(zero_halo_macro.route_halo.min_layer, routing_layer);
  EXPECT_EQ(zero_halo_macro.route_halo.max_layer, routing_layer);
  const auto zero_halo_def = eccdb::DefDesignExporter{design}.exportText();
  EXPECT_NE(zero_halo_def.find("+ ROUTEHALO 0 M1 M1"), std::string::npos);
}

TEST_F(IfpNativeAdapterTest, AbortDropsWorkModelWithoutSubmittingChanges)
{
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& wrapped_instance = FPDM.getDatabase().get_instance_list().front();
  wrapped_instance.set_coord(70, 70);
  wrapped_instance.set_placed(true);
  wrapped_instance.set_placement_updated(true);

  ASSERT_NO_THROW(FPI.abortFP());

  const auto& native_instance = design.netlistStorage().instance(instance);
  EXPECT_EQ(native_instance.origin.x, 10);
  EXPECT_EQ(native_instance.origin.y, 10);
}

TEST_F(IfpNativeAdapterTest, PowerPinNamesAreUniqueAndWritebackConnectsNewOccurrences)
{
  eccdb::InstanceService instances{context};
  eccdb::ConnectivityService connectivity{context};
  for (int index = 1; index < 128; ++index) {
    const auto added = instances.create({.name = "u" + std::to_string(index), .master = master});
    connectivity.connect(design.netlistStorage().findInstanceTerm(added, vdd_term), vdd);
  }
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& pg_net = FPDM.getDatabase().get_pg_net_list().front();
  ASSERT_EQ(pg_net.get_instance_pin_name_list().size(), 1u);
  EXPECT_EQ(pg_net.get_instance_pin_name_list().front(), "VDD");

  // A newly generated physical cell must receive the PG relation, and an
  // external duplicate name must not duplicate its reverse-net endpoint.
  pg_net.add_instance_pin("VDD");
  const auto added = instances.create({.name = "new_pg_cell", .master = master});
  const auto term = design.netlistStorage().findInstanceTerm(added, vdd_term);
  ASSERT_NO_THROW(FPI.output());
  EXPECT_EQ(design.netlistStorage().instanceTerm(term).special_net, vdd);
  EXPECT_EQ(design.netlistStorage().instanceTerms(vdd).size(), 129u);
}

TEST_F(IfpNativeAdapterTest, WritesGeneratedPowerViaAsNativeDesignVia)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"},
      {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10, .pitch_x = 20, .pitch_y = 20,
       .offset_x = 10, .offset_y = 10});
  const auto rule = technology.viaRuleGenerateStorage().createViaRuleGenerate(
      {.name = "VIA12", .flags = eccdb::TechViaRuleGenerateFlag::kDefault},
      {.layer = routing_layer, .enclosure_overhang1 = 2, .enclosure_overhang2 = 2},
      {.layer = cut,
       .flags = eccdb::TechViaRuleGenerateCutLayerFlag::kHasRect | eccdb::TechViaRuleGenerateCutLayerFlag::kHasSpacing,
       .cut_rect = {-1, -1, 1, 1},
       .spacing_x = 2,
       .spacing_y = 2},
      {.layer = upper, .enclosure_overhang1 = 2, .enclosure_overhang2 = 2});
  ASSERT_TRUE(rule);

  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& database = FPDM.getDatabase();
  ifp::PGSegment via;
  via.set_net_name("VDD");
  via.set_type(ifp::PGSegmentType::kVia);
  via.set_bottom_layer_name("M1");
  via.set_top_layer_name("M2");
  via.set_via_width(10);
  via.set_via_height(10);
  via.set_start_coord(40, 10);
  via.set_generated(true);
  database.get_pg_segment_list().push_back(std::move(via));

  ASSERT_NO_THROW(FPI.output());
  EXPECT_EQ(design.routingStorage().viaCount(), 1u);
  bool found_via_shape = false;
  for (const auto wire : design.routingStorage().wires(vdd)) {
    design.routingStorage().forEachShape(wire, [&](eccdb::GeometryShapeId, const eccdb::DesignRouteShape& shape,
                                                   const eccdb::DesignRouteShapeAttributes* attributes) {
      const auto* route_via = std::get_if<eccdb::DesignRouteVia>(&shape.geometry);
      if (route_via != nullptr) {
        found_via_shape = true;
        EXPECT_TRUE(route_via->via.design_via);
        EXPECT_EQ(shape.layer, routing_layer);
        EXPECT_EQ(route_via->exit_layer, upper);
        EXPECT_NE(shape.flags & eccdb::DesignWirePathFlag::kHasShape, 0u);
        ASSERT_NE(attributes, nullptr);
        EXPECT_EQ(attributes->shape, "STRIPE");
      }
    });
  }
  EXPECT_TRUE(found_via_shape);
}

TEST_F(IfpNativeAdapterTest, GeneratedViaSpacingUsesCenterPitch)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  static_cast<void>(technology.viaRuleGenerateStorage().createViaRuleGenerate(
      {.name = "VIA12", .flags = eccdb::TechViaRuleGenerateFlag::kDefault},
      {.layer = routing_layer, .enclosure_overhang1 = 2, .enclosure_overhang2 = 2},
      {.layer = cut,
       .flags = eccdb::TechViaRuleGenerateCutLayerFlag::kHasRect | eccdb::TechViaRuleGenerateCutLayerFlag::kHasSpacing,
       .cut_rect = {-1, -1, 1, 1}, .spacing_x = 6, .spacing_y = 6},
      {.layer = upper, .enclosure_overhang1 = 2, .enclosure_overhang2 = 2}));
  ASSERT_NO_THROW(FPI.wrapDatabase());
  ifp::PGSegment via;
  via.set_net_name("VDD");
  via.set_type(ifp::PGSegmentType::kVia);
  via.set_bottom_layer_name("M1");
  via.set_top_layer_name("M2");
  via.set_via_width(12);
  via.set_via_height(12);
  via.set_start_coord(40, 10);
  via.set_generated(true);
  FPDM.getDatabase().get_pg_segment_list().push_back(std::move(via));
  ASSERT_NO_THROW(FPI.output());
  unsigned found = 0;
  for (const auto wire : design.routingStorage().wires(vdd)) {
    design.routingStorage().forEachShape(wire, [&](auto, const auto& shape, const auto*) {
      if (const auto* geometry = std::get_if<eccdb::DesignRouteVia>(&shape.geometry)) {
        const auto& generated = design.routingStorage().via(geometry->via.design_via).generated;
        EXPECT_EQ(generated.column_count, 2u);
        EXPECT_EQ(generated.row_count, 2u);
        EXPECT_EQ(generated.cut_spacing_x, 4);
        EXPECT_EQ(generated.cut_spacing_y, 4);
        ++found;
      }
    });
  }
  EXPECT_EQ(found, 1u);
}

TEST_F(IfpNativeAdapterTest, GeneratedViaCachePreservesDifferentEnclosureOrientations)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  static_cast<void>(technology.viaRuleGenerateStorage().createViaRuleGenerate(
      {.name = "VIA12", .flags = eccdb::TechViaRuleGenerateFlag::kDefault},
      {.layer = routing_layer, .enclosure_overhang1 = 1, .enclosure_overhang2 = 4},
      {.layer = cut,
       .flags = eccdb::TechViaRuleGenerateCutLayerFlag::kHasRect | eccdb::TechViaRuleGenerateCutLayerFlag::kHasSpacing,
       .cut_rect = {-1, -1, 1, 1}, .spacing_x = 6, .spacing_y = 6},
      {.layer = upper, .enclosure_overhang1 = 1, .enclosure_overhang2 = 4}));
  ASSERT_NO_THROW(FPI.wrapDatabase());
  for (const bool vertical : {true, false}) {
    ifp::PGSegment via;
    via.set_net_name("VDD");
    via.set_type(ifp::PGSegmentType::kVia);
    via.set_bottom_layer_name("M1");
    via.set_top_layer_name("M2");
    via.set_via_width(vertical ? 4 : 10);
    via.set_via_height(vertical ? 10 : 4);
    via.set_start_coord(vertical ? 40 : 60, 10);
    via.set_generated(true);
    FPDM.getDatabase().get_pg_segment_list().push_back(std::move(via));
  }
  ASSERT_NO_THROW(FPI.output());
  EXPECT_EQ(design.routingStorage().viaCount(), 2u);
  unsigned found = 0;
  for (const auto wire : design.routingStorage().wires(vdd)) {
    design.routingStorage().forEachShape(wire, [&](auto, const auto& shape, const auto*) {
      if (const auto* geometry = std::get_if<eccdb::DesignRouteVia>(&shape.geometry)) {
        const auto& generated = design.routingStorage().via(geometry->via.design_via).generated;
        const bool vertical = geometry->origin.position.x == 40;
        EXPECT_EQ(generated.bottom_enclosure_x, vertical ? 1 : 4);
        EXPECT_EQ(generated.bottom_enclosure_y, vertical ? 4 : 1);
        EXPECT_EQ(generated.top_enclosure_x, vertical ? 1 : 4);
        EXPECT_EQ(generated.top_enclosure_y, vertical ? 4 : 1);
        ++found;
      }
    });
  }
  EXPECT_EQ(found, 2u);
}

TEST_F(IfpNativeAdapterTest, ReusesPriorDistributionPhysicalCellOnRerun)
{
  eccdb::InstanceService instances{context};
  const auto prior = instances.create({.name = "BNDRY_CAP_1",
                                       .master = master,
                                       .origin = {10, 10},
                                       .orientation = eccdb::DesignOrientation::kN,
                                       .placement_status = eccdb::DesignPlacementStatus::kFixed,
                                       .source = eccdb::DesignInstanceSource::kDist});
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& database = FPDM.getDatabase();

  ifp::Instance generated;
  generated.set_name("BNDRY_CAP_1");
  generated.set_master_name("CORECELL");
  generated.set_coord(60, 10);
  generated.set_orient(ifp::PlacementOrientation::kN);
  generated.set_fixed(true);
  generated.set_placed(true);
  generated.set_new_instance(true);
  database.get_instance_list().push_back(std::move(generated));

  ASSERT_NO_THROW(FPI.output());
  EXPECT_EQ(design.netlistStorage().instances().size(), 2u);
  const auto& native_instance = design.netlistStorage().instance(prior);
  EXPECT_EQ(native_instance.origin.x, 60);
  EXPECT_EQ(native_instance.origin.y, 10);
  EXPECT_EQ(native_instance.source, eccdb::DesignInstanceSource::kDist);
}

TEST_F(IfpNativeAdapterTest, SwapsCompatiblePriorDistributionMasterOnRerun)
{
  const auto alternate_master = library.cellMasterStorage().createCellMaster(
      {.name = "ALTCORECELL", .type = eccdb::LibraryCellMasterType::kCore, .site = site, .width = 20, .height = 10});
  static_cast<void>(library.masterTermStorage().createMasterTerm(
      alternate_master,
      {.name = "VDD", .direction = eccdb::LibraryMasterTermDirection::kInOut, .use = eccdb::LibraryMasterTermUse::kPower}));
  eccdb::InstanceService instances{context};
  const auto prior = instances.create({.name = "BNDRY_CAP_1",
                                       .master = master,
                                       .origin = {10, 10},
                                       .orientation = eccdb::DesignOrientation::kN,
                                       .placement_status = eccdb::DesignPlacementStatus::kFixed,
                                       .source = eccdb::DesignInstanceSource::kDist});
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& database = FPDM.getDatabase();

  ifp::Instance generated;
  generated.set_name("BNDRY_CAP_1");
  generated.set_master_name("ALTCORECELL");
  generated.set_coord(70, 10);
  generated.set_orient(ifp::PlacementOrientation::kN);
  generated.set_fixed(true);
  generated.set_placed(true);
  generated.set_new_instance(true);
  database.get_instance_list().push_back(std::move(generated));

  ASSERT_NO_THROW(FPI.output());
  const auto& native_instance = design.netlistStorage().instance(prior);
  EXPECT_EQ(native_instance.master, alternate_master);
  EXPECT_EQ(native_instance.origin.x, 70);
}

}  // namespace
