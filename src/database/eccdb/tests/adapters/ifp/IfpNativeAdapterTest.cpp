// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "FPInterface.hpp"
#include "adapters/ifp/FPInterfaceEccdb.h"
#include "IOPlacer.hpp"
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
#include "service/geometry/PhysicalDesignService.h"
#include "service/instance/InstanceService.h"
#include "service/routing/RoutingService.h"
#include "tech/TechStore.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/routing_layer/model/RoutingRuleComponents.h"
#include "tech/via_master/storage/ViaMasterInput.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace {

struct SourceLifecycleState
{
  uint32_t wrap_count = 0;
  uint32_t output_count = 0;
};

class CountingDesignSource final : public ifp::FPDesignSource
{
 public:
  explicit CountingDesignSource(std::shared_ptr<SourceLifecycleState> state) : _state(std::move(state)) {}

  void wrapDatabase(ifp::Database&) override { ++_state->wrap_count; }
  void output(ifp::Database&) override { ++_state->output_count; }

 private:
  std::shared_ptr<SourceLifecycleState> _state;
};

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
    eccdb::adapters::bindFpSource(FPI, context);
  }

  void TearDown() override
  {
    eccdb::adapters::bindFpSource(FPI, nullptr);
    ifp::DataManager::destroyInst();
    ifp::FPInterface::destroyInst();
  }

  struct PlacedVia
  {
    eccdb::TechRoutingLayerId entry;
    eccdb::DesignRouteVia geometry;
  };

  std::vector<PlacedVia> placedVias()
  {
    const eccdb::RoutingService routing{context};
    std::vector<PlacedVia> result;
    for (const auto wire_id : design.routingStorage().wires(vdd)) {
      for (const auto shape_id : routing.wire(wire_id).shapes) {
        const auto& shape = routing.shape(shape_id);
        if (const auto* via = std::get_if<eccdb::DesignRouteVia>(&shape.geometry)) {
          result.push_back({shape.layer, *via});
        }
      }
    }
    return result;
  }

  std::vector<eccdb::Rect> physicalBlockPinRectangles(eccdb::DesignBlockPinId pin)
  {
    const eccdb::PhysicalDesignService physical{context};
    std::vector<eccdb::Rect> rectangles;
    physical.forEachBlockPinGeometry(pin, {.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
                                             rectangles.push_back(rectangle.rectangle);
                                           }});
    return rectangles;
  }

  void requestVia(std::string bottom, std::string top, eccdb::Point origin, int32_t width = 10, int32_t height = 10)
  {
    ifp::PGSegment via;
    via.set_net_name("VDD");
    via.set_type(ifp::PGSegmentType::kVia);
    via.set_bottom_layer_name(std::move(bottom));
    via.set_top_layer_name(std::move(top));
    via.set_via_width(width);
    via.set_via_height(height);
    via.set_start_coord(origin.x, origin.y);
    via.set_generated(true);
    FPDM.getDatabase().get_pg_segment_list().push_back(std::move(via));
  }

  eccdb::DesignViaId createFixedVia(std::string name, eccdb::TechRoutingLayerId bottom, eccdb::TechCutLayerId cut,
                                   eccdb::TechRoutingLayerId top)
  {
    eccdb::RoutingService routing{context};
    return routing.createVia(eccdb::DesignViaInput{
        .name = std::move(name),
        .rectangles = {{.layer = eccdb::TechLayerId{bottom.entity()}, .rectangle = {-5, -5, 5, 5}},
                       {.layer = eccdb::TechLayerId{cut.entity()}, .rectangle = {-1, -1, 1, 1}},
                       {.layer = eccdb::TechLayerId{top.entity()}, .rectangle = {-5, -5, 5, 5}}}});
  }

  eccdb::TechViaRuleGenerateId createViaRule(std::string name, eccdb::TechRoutingLayerId bottom, eccdb::TechCutLayerId cut,
                                            eccdb::TechRoutingLayerId top, bool is_default = false, int32_t half_cut = 1,
                                            int32_t pitch = 6, int32_t enclosure = 2)
  {
    return technology.viaRuleGenerateStorage().createViaRuleGenerate(
        {.name = std::move(name), .flags = is_default ? eccdb::TechViaRuleGenerateFlag::kDefault : 0u},
        {.layer = bottom,
         .flags = eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure,
         .enclosure_overhang1 = enclosure,
         .enclosure_overhang2 = enclosure},
        {.layer = cut,
         .flags = eccdb::TechViaRuleGenerateCutLayerFlag::kHasRect | eccdb::TechViaRuleGenerateCutLayerFlag::kHasSpacing,
         .cut_rect = {-half_cut, -half_cut, half_cut, half_cut},
         .spacing_x = pitch,
         .spacing_y = pitch},
        {.layer = top,
         .flags = eccdb::TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure,
         .enclosure_overhang1 = enclosure,
         .enclosure_overhang2 = enclosure});
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

TEST_F(IfpNativeAdapterTest, RoutingLayerIndicesUseCompactRoutingOrderAndPhysicalOrderUsesFullSequence)
{
  // iFP's layer_idx and name map index the compact routing-layer list, while
  // order follows the complete physical M1/V1/M2 sequence. Reserve
  // non-physical technology entries so an EnTT packed ID cannot accidentally
  // look like either value.
  static_cast<void>(technology.createOverlapLayer({.name = "OVERLAP0"}));
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  static_cast<void>(technology.createOverlapLayer({.name = "OVERLAP1"}));
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 12, .min_width = 12, .pitch_x = 30, .pitch_y = 30,
                       .offset_x = 15, .offset_y = 15});

  const auto upper_position = technology.layerPosition(eccdb::TechLayerId{upper.entity()});
  ASSERT_TRUE(upper_position.has_value());
  EXPECT_EQ(*upper_position, 2u);
  EXPECT_NE(static_cast<std::uint32_t>(upper.packed()), *upper_position);

  ASSERT_NO_THROW(FPI.wrapDatabase());
  const auto& database = FPDM.getDatabase();
  ASSERT_EQ(database.get_routing_layer_list().size(), 2u);
  ASSERT_EQ(database.get_routing_layer_name_to_idx_map().at("M1"), 0);
  ASSERT_EQ(database.get_routing_layer_name_to_idx_map().at("M2"), 1);
  const auto& lower = database.get_routing_layer_list().at(0);
  const auto& wrapped_upper = database.get_routing_layer_list().at(1);
  EXPECT_EQ(lower.get_name(), "M1");
  EXPECT_EQ(lower.get_layer_idx(), 0);
  EXPECT_EQ(lower.get_order(), 0);
  EXPECT_EQ(wrapped_upper.get_name(), "M2");
  EXPECT_EQ(wrapped_upper.get_layer_idx(), 1);
  EXPECT_EQ(wrapped_upper.get_order(), 2);
  EXPECT_NE(wrapped_upper.get_layer_idx(), static_cast<int32_t>(upper.packed()));

  auto& mutable_database = FPDM.getDatabase();
  mutable_database.get_new_track_list().clear();
  ifp::Track track;
  track.set_layer_name("M2");
  track.set_x_offset(15);
  track.set_y_offset(15);
  track.set_x_pitch(30);
  track.set_y_pitch(30);
  mutable_database.get_new_track_list().push_back(track);
  mutable_database.set_track_updated(true);
  ASSERT_NO_THROW(FPI.output());

  const auto grids = design.floorplanStorage().trackGridsForLayer(upper);
  ASSERT_EQ(grids.size(), 2u);
  for (const auto grid_id : grids) {
    const auto& grid = design.floorplanStorage().trackGrid(grid_id);
    ASSERT_EQ(grid.layers.size(), 1u);
    EXPECT_EQ(grid.layers.front(), upper);
  }
  EXPECT_TRUE(design.floorplanStorage().trackGridsForLayer(routing_layer).empty());
  static_cast<void>(cut);
}

TEST_F(IfpNativeAdapterTest, RoutingLayerAreaAndDefaultSpacingRetainNativeValues)
{
  constexpr int64_t kLargeArea = 4'000'000'000LL;
  auto& source = technology.routingLayerStorage().routingLayer(routing_layer);
  source.area = kLargeArea;
  static_cast<void>(technology.routingLayerStorage().addSpacingRule(
      routing_layer, eccdb::TechRoutingSpacingRule{.type = eccdb::TechRoutingSpacingType::kDefault, .min_spacing = 37}));

  ASSERT_NO_THROW(FPI.wrapDatabase());
  const auto& layer = FPDM.getDatabase().get_routing_layer_list().front();
  EXPECT_EQ(layer.get_min_area(), kLargeArea);
  EXPECT_EQ(layer.get_spacing(), 37);
}

TEST_F(IfpNativeAdapterTest, UpdatedIoPortListGroupsImplicitGeometryAtThePinAnchor)
{
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& pin = FPDM.getDatabase().get_io_pin_list().front();
  pin.set_coord(40, 50);
  pin.set_orient(ifp::PlacementOrientation::kN);
  pin.set_placed(true);
  pin.set_updated(true);
  ifp::IOPort first;
  first.set_layer_name("M1");
  first.set_coord(400, 500);
  first.set_rect(-5, -2, 5, 2);
  first.set_placed(true);
  pin.get_new_port_list().push_back(first);
  ifp::IOPort second;
  second.set_layer_name("M1");
  second.set_coord(700, 800);
  second.set_rect(-2, -6, 2, 6);
  second.set_placed(true);
  pin.get_new_port_list().push_back(second);

  ASSERT_NO_THROW(FPI.output());
  const auto term = design.netlistStorage().findBlockTerm("VDD");
  const auto ports = design.netlistStorage().blockPins(term);
  // No prior +PORT means this is implicit DEF geometry. EccDB represents all
  // of its output rectangles under one physical BlockPin at the pin anchor.
  ASSERT_EQ(ports.size(), 1u);
  const auto& stored = design.netlistStorage().blockPin(ports.front());
  EXPECT_EQ(stored.origin, (eccdb::Point{40, 50}));
  EXPECT_EQ(stored.flags & eccdb::DesignIoPinPortFlag::kExplicit, 0u);
  ASSERT_EQ(stored.rectangles.size(), 2u);
  EXPECT_EQ(design.netlistStorage().blockPinRectangle(stored.rectangles[0]).rectangle, (eccdb::Rect{-5, -2, 5, 2}));
  EXPECT_EQ(design.netlistStorage().blockPinRectangle(stored.rectangles[1]).rectangle, (eccdb::Rect{-2, -6, 2, 6}));
  const auto physical = physicalBlockPinRectangles(ports.front());
  ASSERT_EQ(physical.size(), 2u);
  EXPECT_EQ(physical[0], (eccdb::Rect{35, 48, 45, 52}));
  EXPECT_EQ(physical[1], (eccdb::Rect{38, 44, 42, 56}));
  EXPECT_EQ(design.netlistStorage().blockTerm(term).special_net, vdd);
  EXPECT_FALSE(pin.is_updated());
  EXPECT_TRUE(pin.get_new_port_list().empty());

  ASSERT_NO_THROW(FPI.output());
  EXPECT_EQ(design.netlistStorage().blockPins(term).size(), 1u);
}

TEST_F(IfpNativeAdapterTest, UpdatedExplicitIoPortListUsesPerPortCoordinatesAndCorrectGeometry)
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
  pin.set_coord(101, 203);
  pin.set_orient(ifp::PlacementOrientation::kE);
  pin.set_placed(true);
  pin.set_updated(true);
  ifp::IOPort placed_port;
  placed_port.set_layer_name("M1");
  placed_port.set_coord(311, 419);
  placed_port.set_rect(-4, -2, 6, 3);
  placed_port.set_placed(true);
  pin.get_new_port_list().push_back(placed_port);
  ifp::IOPort direct_location_port;
  direct_location_port.set_layer_name("M1");
  direct_location_port.set_coord(777, 888);
  direct_location_port.set_rect(-2, -3, 4, 1);
  direct_location_port.set_placed(false);
  pin.get_new_port_list().push_back(direct_location_port);

  ASSERT_NO_THROW(FPI.output());
  const auto ports = design.netlistStorage().blockPins(term);
  ASSERT_EQ(ports.size(), 2u);
  EXPECT_FALSE(design.netlistStorage().contains(existing));
  EXPECT_EQ(design.netlistStorage().blockTerm(term).special_net, vdd);

  const auto& placed = design.netlistStorage().blockPin(ports[0]);
  EXPECT_NE(placed.flags & eccdb::DesignIoPinPortFlag::kExplicit, 0u);
  EXPECT_EQ(placed.origin, (eccdb::Point{311, 419}));
  EXPECT_EQ(placed.orientation, eccdb::DesignOrientation::kE);
  ASSERT_EQ(placed.rectangles.size(), 1u);
  EXPECT_EQ(design.netlistStorage().blockPinRectangle(placed.rectangles.front()).rectangle, (eccdb::Rect{-4, -2, 6, 3}));
  const auto placed_geometry = physicalBlockPinRectangles(ports[0]);
  ASSERT_EQ(placed_geometry.size(), 1u);
  EXPECT_EQ(placed_geometry.front(), (eccdb::Rect{309, 413, 314, 423}));

  const auto& direct_location = design.netlistStorage().blockPin(ports[1]);
  EXPECT_NE(direct_location.flags & eccdb::DesignIoPinPortFlag::kExplicit, 0u);
  // A non-placed IOPort deliberately uses the IOPin anchor instead of its
  // unset/placeholder coordinate.
  EXPECT_EQ(direct_location.origin, (eccdb::Point{101, 203}));
  EXPECT_EQ(direct_location.orientation, eccdb::DesignOrientation::kE);
  ASSERT_EQ(direct_location.rectangles.size(), 1u);
  EXPECT_EQ(design.netlistStorage().blockPinRectangle(direct_location.rectangles.front()).rectangle, (eccdb::Rect{-2, -3, 4, 1}));
  const auto direct_geometry = physicalBlockPinRectangles(ports[1]);
  ASSERT_EQ(direct_geometry.size(), 1u);
  EXPECT_EQ(direct_geometry.front(), (eccdb::Rect{98, 199, 102, 205}));
  EXPECT_FALSE(pin.is_updated());
  EXPECT_TRUE(pin.get_new_port_list().empty());
}

TEST_F(IfpNativeAdapterTest, UpdatedImplicitIoPortListReplacesOldGeometryAtTheSharedPinAnchor)
{
  eccdb::ConnectivityService connectivity{context};
  const auto signal = connectivity.createNet({.name = "SIG_IMPLICIT"}, false);
  const auto term = connectivity.createBlockTerm({.name = "SIG_IMPLICIT", .direction = eccdb::DesignIoPinDirection::kInput});
  connectivity.connect(term, signal);
  eccdb::FloorplanService floorplan{context};
  eccdb::DesignBlockPinInput input{.flags = eccdb::DesignIoPinPortFlag::kHasPlacement,
                                   .placement_status = eccdb::DesignPlacementStatus::kPlaced,
                                   .origin = {10, 20},
                                   .orientation = eccdb::DesignOrientation::kN};
  input.rectangles.push_back({.layer = eccdb::TechLayerId{routing_layer.entity()}, .rectangle = {-5, -2, 5, 2}});
  const auto existing = floorplan.createBlockPin(term, std::move(input));

  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& io_pins = FPDM.getDatabase().get_io_pin_list();
  const auto pin_it = std::find_if(io_pins.begin(), io_pins.end(), [](const ifp::IOPin& candidate) {
    return candidate.get_name() == "SIG_IMPLICIT";
  });
  ASSERT_NE(pin_it, io_pins.end());
  auto& pin = *pin_it;
  ASSERT_FALSE(pin.get_port_exist());
  pin.set_coord(41, 53);
  pin.set_orient(ifp::PlacementOrientation::kN);
  pin.set_placed(true);
  pin.set_updated(true);
  ifp::IOPort first;
  first.set_layer_name("M1");
  first.set_coord(501, 601);
  first.set_rect(-5, -2, 5, 2);
  first.set_placed(true);
  pin.get_new_port_list().push_back(first);
  ifp::IOPort second;
  second.set_layer_name("M1");
  second.set_coord(701, 801);
  second.set_rect(-2, -6, 2, 6);
  second.set_placed(false);
  pin.get_new_port_list().push_back(second);

  ASSERT_NO_THROW(FPI.output());

  const auto ports = design.netlistStorage().blockPins(term);
  ASSERT_EQ(ports.size(), 1u);
  EXPECT_FALSE(design.netlistStorage().contains(existing));
  const auto& replacement = design.netlistStorage().blockPin(ports.front());
  EXPECT_EQ(replacement.flags & eccdb::DesignIoPinPortFlag::kExplicit, 0u);
  EXPECT_EQ(replacement.origin, (eccdb::Point{41, 53}));
  ASSERT_EQ(replacement.rectangles.size(), 2u);
  EXPECT_EQ(design.netlistStorage().blockPinRectangle(replacement.rectangles[0]).rectangle, (eccdb::Rect{-5, -2, 5, 2}));
  EXPECT_EQ(design.netlistStorage().blockPinRectangle(replacement.rectangles[1]).rectangle, (eccdb::Rect{-2, -6, 2, 6}));
  const auto physical = physicalBlockPinRectangles(ports.front());
  ASSERT_EQ(physical.size(), 2u);
  EXPECT_EQ(physical[0], (eccdb::Rect{36, 51, 46, 55}));
  EXPECT_EQ(physical[1], (eccdb::Rect{39, 47, 43, 59}));
  EXPECT_EQ(design.netlistStorage().blockTerm(term).net, signal);
  EXPECT_FALSE(pin.is_updated());
  EXPECT_TRUE(pin.get_new_port_list().empty());
}

TEST_F(IfpNativeAdapterTest, AutoIoPlacementReplacesExplicitPortWithoutAccumulation)
{
  const auto term = design.netlistStorage().findBlockTerm("VDD");
  eccdb::FloorplanService floorplan{context};
  eccdb::DesignBlockPinInput input{.flags = eccdb::DesignIoPinPortFlag::kExplicit | eccdb::DesignIoPinPortFlag::kHasPlacement,
                                   .placement_status = eccdb::DesignPlacementStatus::kPlaced,
                                   .origin = {10, 20},
                                   .orientation = eccdb::DesignOrientation::kN};
  input.rectangles.push_back({.layer = eccdb::TechLayerId{routing_layer.entity()}, .rectangle = {-5, -2, 5, 2}});
  const auto existing = floorplan.createBlockPin(term, std::move(input));
  static_cast<void>(technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10, .pitch_x = 20, .pitch_y = 20,
                       .offset_x = 10, .offset_y = 10}));

  ASSERT_NO_THROW(FPI.wrapDatabase());
  FPDM.getConfig().io_pin_layer_name_list = {"M1", "M2"};
  const auto place_auto = [] {
    ifp::IOPlacer::initInst();
    try {
      ifp::IOPlacer::getInst().placeAuto();
    } catch (...) {
      ifp::IOPlacer::destroyInst();
      throw;
    }
    ifp::IOPlacer::destroyInst();
  };

  ASSERT_NO_THROW(place_auto());
  ASSERT_NO_THROW(FPI.output());
  const auto first_ports = design.netlistStorage().blockPins(term);
  ASSERT_EQ(first_ports.size(), 1u);
  const auto first = first_ports.front();
  EXPECT_FALSE(design.netlistStorage().contains(existing));
  const auto& first_port = design.netlistStorage().blockPin(first);
  ASSERT_EQ(first_port.rectangles.size(), 1u);
  const auto first_origin = first_port.origin;
  const auto first_local = design.netlistStorage().blockPinRectangle(first_port.rectangles.front()).rectangle;
  EXPECT_NE(first_origin, (eccdb::Point{10, 20}));

  ASSERT_NO_THROW(place_auto());
  ASSERT_NO_THROW(FPI.output());
  const auto second_ports = design.netlistStorage().blockPins(term);
  ASSERT_EQ(second_ports.size(), 1u);
  EXPECT_FALSE(design.netlistStorage().contains(first));
  const auto& second_port = design.netlistStorage().blockPin(second_ports.front());
  ASSERT_EQ(second_port.rectangles.size(), 1u);
  EXPECT_EQ(second_port.origin, first_origin);
  EXPECT_EQ(design.netlistStorage().blockPinRectangle(second_port.rectangles.front()).rectangle, first_local);
}

TEST_F(IfpNativeAdapterTest, AutoIoPlacementUsesDirectLocationAnchorForRegularIoPort)
{
  eccdb::ConnectivityService connectivity{context};
  const auto signal = connectivity.createNet({.name = "SIG"}, false);
  const auto signal_term = connectivity.createBlockTerm({.name = "SIG", .direction = eccdb::DesignIoPinDirection::kInput});
  connectivity.connect(signal_term, signal);
  static_cast<void>(technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10, .pitch_x = 20, .pitch_y = 20,
                       .offset_x = 10, .offset_y = 10}));

  ASSERT_NO_THROW(FPI.wrapDatabase());
  FPDM.getConfig().io_pin_layer_name_list = {"M1", "M2"};
  auto& io_pins = FPDM.getDatabase().get_io_pin_list();
  const auto signal_pin = std::find_if(io_pins.begin(), io_pins.end(), [](const ifp::IOPin& pin) { return pin.get_name() == "SIG"; });
  ASSERT_NE(signal_pin, io_pins.end());
  EXPECT_FALSE(signal_pin->get_special_net());
  EXPECT_FALSE(signal_pin->get_port_exist());

  const auto place_auto = [] {
    ifp::IOPlacer::initInst();
    try {
      ifp::IOPlacer::getInst().placeAuto();
    } catch (...) {
      ifp::IOPlacer::destroyInst();
      throw;
    }
    ifp::IOPlacer::destroyInst();
  };
  ASSERT_NO_THROW(place_auto());

  ASSERT_TRUE(signal_pin->get_direct_location());
  ASSERT_EQ(signal_pin->get_new_port_list().size(), 1u);
  EXPECT_FALSE(signal_pin->get_new_port_list().front().get_placed());
  EXPECT_EQ(signal_pin->get_new_port_list().front().get_x(), -1);
  EXPECT_EQ(signal_pin->get_new_port_list().front().get_y(), -1);
  const auto anchor = eccdb::Point{signal_pin->get_x(), signal_pin->get_y()};
  ASSERT_NE(anchor, (eccdb::Point{-1, -1}));

  ASSERT_NO_THROW(FPI.output());
  const auto ports = design.netlistStorage().blockPins(signal_term);
  ASSERT_EQ(ports.size(), 1u);
  const auto& stored = design.netlistStorage().blockPin(ports.front());
  EXPECT_EQ(stored.origin, anchor);
  EXPECT_NE(stored.origin, (eccdb::Point{-1, -1}));
  ASSERT_EQ(stored.rectangles.size(), 1u);
  const auto bounds = design.geometryStorage().bounds(stored.rectangles.front());
  const auto die = design.globalStorage().dieBounds();
  EXPECT_TRUE(bounds.ll_x == die.ll_x || bounds.ur_x == die.ur_x || bounds.ll_y == die.ll_y || bounds.ur_y == die.ur_y);
}

TEST_F(IfpNativeAdapterTest, EmptyUpdatedIoPortListClearsPhysicalPortsButKeepsLogicalConnection)
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
  pin.set_updated(true);
  ASSERT_NO_THROW(FPI.output());

  EXPECT_TRUE(design.netlistStorage().contains(term));
  EXPECT_EQ(design.netlistStorage().findBlockTerm("VDD"), term);
  EXPECT_EQ(design.netlistStorage().blockTerm(term).special_net, vdd);
  EXPECT_TRUE(design.netlistStorage().blockPins(term).empty());
  EXPECT_FALSE(design.netlistStorage().contains(existing));
  EXPECT_FALSE(pin.is_updated());
  EXPECT_TRUE(pin.get_new_port_list().empty());
}

TEST_F(IfpNativeAdapterTest, UnmodifiedIoPinLeavesExistingPhysicalPortAndPendingGeometryUntouched)
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
  ifp::IOPort pending;
  pending.set_layer_name("M1");
  pending.set_coord(40, 50);
  pending.set_rect(-3, -3, 3, 3);
  pending.set_placed(true);
  pin.get_new_port_list().push_back(std::move(pending));
  ASSERT_FALSE(pin.is_updated());

  ASSERT_NO_THROW(FPI.output());

  const auto ports = design.netlistStorage().blockPins(term);
  ASSERT_EQ(ports.size(), 1u);
  EXPECT_EQ(ports.front(), existing);
  const auto& stored = design.netlistStorage().blockPin(existing);
  EXPECT_EQ(stored.origin, (eccdb::Point{10, 20}));
  ASSERT_EQ(stored.rectangles.size(), 1u);
  EXPECT_EQ(design.netlistStorage().blockPinRectangle(stored.rectangles.front()).rectangle, (eccdb::Rect{-5, -2, 5, 2}));
  EXPECT_EQ(design.netlistStorage().blockTerm(term).special_net, vdd);
  EXPECT_FALSE(pin.is_updated());
  EXPECT_EQ(pin.get_new_port_list().size(), 1u);
}

TEST_F(IfpNativeAdapterTest, UnknownUpdatedIoPinFailsWithoutCreatingTermOrConsumingPendingDelta)
{
  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& unknown = FPDM.getDatabase().get_io_pin_list().emplace_back();
  unknown.set_name("UNKNOWN_IO");
  ifp::IOPort pending;
  pending.set_layer_name("M1");
  pending.set_coord(40, 50);
  pending.set_rect(-3, -3, 3, 3);
  pending.set_placed(true);
  unknown.get_new_port_list().push_back(std::move(pending));
  unknown.set_updated(true);

  EXPECT_THROW(FPI.output(), std::invalid_argument);
  EXPECT_FALSE(design.netlistStorage().findBlockTerm("UNKNOWN_IO"));
  EXPECT_TRUE(unknown.is_updated());
  EXPECT_EQ(unknown.get_new_port_list().size(), 1u);
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

TEST_F(IfpNativeAdapterTest, WrapsRegularNetPinsWithSingleAnchorRoundingAndPlacedIoGeometry)
{
  const auto signal_term = library.masterTermStorage().createMasterTerm(
      master, {.name = "SIG", .direction = eccdb::LibraryMasterTermDirection::kInput, .use = eccdb::LibraryMasterTermUse::kSignal});
  static_cast<void>(library.masterPortStorage().createMasterPin(
      signal_term,
      eccdb::LibraryMasterPortInput{.layer_clauses = {
          {.layer = eccdb::TechLayerId{routing_layer.entity()}, .geometry = {.rects = {{1, 1, 4, 4}, {8, 4, 11, 7}}}},
      }}));

  eccdb::InstanceService instances{context};
  const auto fs_instance = instances.create({.name = "u_fs",
                                             .master = master,
                                             .origin = {100, 200},
                                             .orientation = eccdb::DesignOrientation::kFS,
                                             .placement_status = eccdb::DesignPlacementStatus::kPlaced});
  const auto unplaced_instance = instances.create(
      {.name = "u_unplaced", .master = master, .placement_status = eccdb::DesignPlacementStatus::kUnplaced});

  eccdb::ConnectivityService connectivity{context};
  const auto net = connectivity.createNet({.name = "N_SIG"}, false);
  connectivity.connect(design.netlistStorage().findInstanceTerm(fs_instance, signal_term), net);
  connectivity.connect(design.netlistStorage().findInstanceTerm(unplaced_instance, signal_term), net);

  eccdb::DesignBlockPinInput io_port{.flags = eccdb::DesignIoPinPortFlag::kExplicit | eccdb::DesignIoPinPortFlag::kHasPlacement,
                                      .placement_status = eccdb::DesignPlacementStatus::kPlaced,
                                      .origin = {300, 400},
                                      .orientation = eccdb::DesignOrientation::kN};
  io_port.rectangles.push_back({.layer = eccdb::TechLayerId{routing_layer.entity()}, .rectangle = {1, 3, 4, 6}});
  std::vector<eccdb::DesignBlockPinInput> io_ports;
  io_ports.push_back(std::move(io_port));
  const auto io_term = connectivity.createBlockTerm(
      {.name = "IO_SIG", .direction = eccdb::DesignIoPinDirection::kInput}, std::move(io_ports));
  connectivity.connect(io_term, net);

  ASSERT_NO_THROW(FPI.wrapDatabase());
  const auto& database = FPDM.getDatabase();
  const ifp::Net* wrapped_net = nullptr;
  for (const auto& candidate : database.get_net_list()) {
    if (candidate.get_name() == "N_SIG") {
      wrapped_net = &candidate;
      break;
    }
  }
  ASSERT_NE(wrapped_net, nullptr);
  ASSERT_EQ(wrapped_net->get_pin_num(), 3);
  ASSERT_EQ(wrapped_net->get_net_pin_list().size(), 3u);

  const ifp::NetPin* fs_pin = nullptr;
  const ifp::NetPin* unplaced_pin = nullptr;
  const ifp::NetPin* io_pin = nullptr;
  for (const auto& pin : wrapped_net->get_net_pin_list()) {
    if (pin.get_io()) {
      io_pin = &pin;
    } else if (pin.get_instance_name() == "u_fs") {
      fs_pin = &pin;
    } else if (pin.get_instance_name() == "u_unplaced") {
      unplaced_pin = &pin;
    }
  }
  ASSERT_NE(fs_pin, nullptr);
  ASSERT_NE(unplaced_pin, nullptr);
  ASSERT_NE(io_pin, nullptr);

  // The two 3-DBU-wide RECTs produce one local anchor at (6,4).  Dividing
  // each shape separately would produce a different x coordinate. FS mirrors
  // that point through the 20x10 master before applying the instance origin.
  EXPECT_EQ(fs_pin->get_pin_name(), "SIG");
  EXPECT_EQ(fs_pin->get_offset_x(), 6);
  EXPECT_EQ(fs_pin->get_offset_y(), 4);
  EXPECT_TRUE(fs_pin->get_placed());
  EXPECT_EQ(fs_pin->get_x(), 106);
  EXPECT_EQ(fs_pin->get_y(), 206);

  EXPECT_EQ(unplaced_pin->get_pin_name(), "SIG");
  EXPECT_EQ(unplaced_pin->get_offset_x(), 6);
  EXPECT_EQ(unplaced_pin->get_offset_y(), 4);
  EXPECT_FALSE(unplaced_pin->get_placed());
  EXPECT_EQ(unplaced_pin->get_x(), -1);
  EXPECT_EQ(unplaced_pin->get_y(), -1);

  EXPECT_EQ(io_pin->get_pin_name(), "IO_SIG");
  EXPECT_TRUE(io_pin->get_io());
  EXPECT_TRUE(io_pin->get_placed());
  EXPECT_EQ(io_pin->get_x(), 302);
  EXPECT_EQ(io_pin->get_y(), 404);

  const ifp::IOPin* wrapped_io = nullptr;
  for (const auto& candidate : database.get_io_pin_list()) {
    if (candidate.get_name() == "IO_SIG") {
      wrapped_io = &candidate;
      break;
    }
  }
  ASSERT_NE(wrapped_io, nullptr);
  EXPECT_TRUE(wrapped_io->get_placed());
  EXPECT_EQ(wrapped_io->get_x(), 300);
  EXPECT_EQ(wrapped_io->get_y(), 400);
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

TEST_F(IfpNativeAdapterTest, DesignSourceDispatchesOnDestroyAndDetachReleasesIt)
{
  const auto released_source = std::shared_ptr<ifp::FPDesignSource>{};

  auto completed_state = std::make_shared<SourceLifecycleState>();
  std::weak_ptr<CountingDesignSource> completed_source;
  {
    auto source = std::make_shared<CountingDesignSource>(completed_state);
    completed_source = source;
    FPI.setDesignSource(source);
  }
  ASSERT_FALSE(completed_source.expired());
  ASSERT_NO_THROW(FPI.wrapDatabase());
  EXPECT_EQ(completed_state->wrap_count, 1u);
  ASSERT_NO_THROW(FPI.destroyFP());
  EXPECT_EQ(completed_state->output_count, 1u);
  EXPECT_FALSE(completed_source.expired());
  FPI.setDesignSource(released_source);
  EXPECT_TRUE(completed_source.expired());

  ifp::DataManager::initInst();
  auto aborted_state = std::make_shared<SourceLifecycleState>();
  std::weak_ptr<CountingDesignSource> aborted_source;
  {
    auto source = std::make_shared<CountingDesignSource>(aborted_state);
    aborted_source = source;
    FPI.setDesignSource(source);
  }
  ASSERT_NO_THROW(FPI.wrapDatabase());
  EXPECT_EQ(aborted_state->wrap_count, 1u);
  ASSERT_NO_THROW(FPI.abortFP());
  EXPECT_EQ(aborted_state->output_count, 0u);
  EXPECT_FALSE(aborted_source.expired());
  FPI.setDesignSource(released_source);
  EXPECT_TRUE(aborted_source.expired());
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

TEST_F(IfpNativeAdapterTest, GeneratedPowerStripesCoverViaEnclosuresInBothDirections)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 20, .min_width = 20, .pitch_x = 40, .pitch_y = 40,
                       .offset_x = 20, .offset_y = 20});
  static_cast<void>(technology.viaRuleGenerateStorage().createViaRuleGenerate(
      {.name = "VIA12", .flags = eccdb::TechViaRuleGenerateFlag::kDefault},
      {.layer = routing_layer, .enclosure_overhang1 = 2, .enclosure_overhang2 = 2},
      {.layer = cut,
       .flags = eccdb::TechViaRuleGenerateCutLayerFlag::kHasRect | eccdb::TechViaRuleGenerateCutLayerFlag::kHasSpacing,
       .cut_rect = {-1, -1, 1, 1},
       .spacing_x = 2,
       .spacing_y = 2},
      {.layer = upper, .enclosure_overhang1 = 2, .enclosure_overhang2 = 2}));

  ASSERT_NO_THROW(FPI.wrapDatabase());
  struct StripeViaCase
  {
    eccdb::TechRoutingLayerId layer;
    eccdb::Point begin;
    eccdb::Point end;
    eccdb::Point via_origin;
    int32_t via_width;
    int32_t via_height;
  };
  // The stripe width covers the via across the line but not beyond the
  // endpoint. Each case therefore requires the iFP enclosure adjustment to
  // extend the correct endpoint before the native shape is created.
  const std::array<StripeViaCase, 4> cases{{
      {routing_layer, {80, 20}, {140, 20}, {80, 20}, 40, 20},
      {routing_layer, {140, 60}, {80, 60}, {80, 60}, 40, 20},
      {upper, {220, 140}, {220, 200}, {220, 140}, 20, 40},
      {upper, {260, 200}, {260, 140}, {260, 140}, 20, 40},
  }};

  auto& database = FPDM.getDatabase();
  for (const auto& test_case : cases) {
    ifp::PGSegment stripe;
    stripe.set_net_name("VDD");
    stripe.set_layer_name(test_case.layer == routing_layer ? "M1" : "M2");
    stripe.set_type(ifp::PGSegmentType::kStripe);
    stripe.set_width(40);
    stripe.set_start_coord(test_case.begin.x, test_case.begin.y);
    stripe.set_end_coord(test_case.end.x, test_case.end.y);
    stripe.set_generated(true);
    database.get_pg_segment_list().push_back(std::move(stripe));

    ifp::PGSegment via;
    via.set_net_name("VDD");
    via.set_type(ifp::PGSegmentType::kVia);
    via.set_bottom_layer_name("M1");
    via.set_top_layer_name("M2");
    via.set_via_width(test_case.via_width);
    via.set_via_height(test_case.via_height);
    via.set_start_coord(test_case.via_origin.x, test_case.via_origin.y);
    via.set_generated(true);
    database.get_pg_segment_list().push_back(std::move(via));
  }

  ASSERT_NO_THROW(FPI.output());
  const eccdb::PhysicalDesignService physical{context};
  for (const auto& test_case : cases) {
    bool found_stripe = false;
    eccdb::Rect stripe_bounds;
    const eccdb::DesignRouteVia* output_via = nullptr;
    for (const auto wire : design.routingStorage().wires(vdd)) {
      design.routingStorage().forEachShape(
          wire, [&](eccdb::GeometryShapeId, const eccdb::DesignRouteShape& shape,
                    const eccdb::DesignRouteShapeAttributes* attributes) {
            if (const auto* via = std::get_if<eccdb::DesignRouteVia>(&shape.geometry)) {
              if (via->origin.position == test_case.via_origin) {
                output_via = via;
              }
              return;
            }
            const auto* segment = std::get_if<eccdb::DesignRouteSegment>(&shape.geometry);
            if (segment == nullptr || attributes == nullptr || shape.layer != test_case.layer) {
              return;
            }
            const auto begin_x = std::min(test_case.begin.x, test_case.end.x);
            const auto end_x = std::max(test_case.begin.x, test_case.end.x);
            const auto begin_y = std::min(test_case.begin.y, test_case.end.y);
            const auto end_y = std::max(test_case.begin.y, test_case.end.y);
            const bool expected_horizontal = test_case.begin.y == test_case.end.y;
            const bool same_line = expected_horizontal
                                       ? segment->begin.position.y == test_case.begin.y && segment->end.position.y == test_case.begin.y
                                       : segment->begin.position.x == test_case.begin.x && segment->end.position.x == test_case.begin.x;
            const bool covers_original_line = std::min(segment->begin.position.x, segment->end.position.x) <= begin_x
                                              && std::max(segment->begin.position.x, segment->end.position.x) >= end_x
                                              && std::min(segment->begin.position.y, segment->end.position.y) <= begin_y
                                              && std::max(segment->begin.position.y, segment->end.position.y) >= end_y;
            if (!same_line || !covers_original_line) {
              return;
            }
            const auto half_width = attributes->width / 2;
            stripe_bounds = {std::min(segment->begin.position.x, segment->end.position.x) - half_width,
                             std::min(segment->begin.position.y, segment->end.position.y) - half_width,
                             std::max(segment->begin.position.x, segment->end.position.x) + half_width,
                             std::max(segment->begin.position.y, segment->end.position.y) + half_width};
            found_stripe = true;
          });
    }
    ASSERT_TRUE(found_stripe);
    ASSERT_NE(output_via, nullptr);
    ASSERT_TRUE(output_via->via.design_via);

    std::vector<eccdb::Rect> via_enclosures;
    physical.forEachDesignViaGeometry(
        output_via->via.design_via, output_via->origin.position, eccdb::DesignOrientation::kN,
        {.rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
          if (rectangle.layer == eccdb::TechLayerId{test_case.layer.entity()}) {
            via_enclosures.push_back(rectangle.rectangle);
          }
        }});
    ASSERT_EQ(via_enclosures.size(), 1u);
    const auto& enclosure = via_enclosures.front();
    EXPECT_LE(stripe_bounds.ll_x, enclosure.ll_x);
    EXPECT_LE(stripe_bounds.ll_y, enclosure.ll_y);
    EXPECT_GE(stripe_bounds.ur_x, enclosure.ur_x);
    EXPECT_GE(stripe_bounds.ur_y, enclosure.ur_y);
  }
}

TEST_F(IfpNativeAdapterTest, RepeatedCompletedOutputDoesNotDuplicateUpdatedIoOrGeneratedPowerGeometry)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10, .pitch_x = 20, .pitch_y = 20,
                       .offset_x = 10, .offset_y = 10});
  static_cast<void>(technology.viaRuleGenerateStorage().createViaRuleGenerate(
      {.name = "VIA12", .flags = eccdb::TechViaRuleGenerateFlag::kDefault},
      {.layer = routing_layer, .enclosure_overhang1 = 2, .enclosure_overhang2 = 2},
      {.layer = cut,
       .flags = eccdb::TechViaRuleGenerateCutLayerFlag::kHasRect | eccdb::TechViaRuleGenerateCutLayerFlag::kHasSpacing,
       .cut_rect = {-1, -1, 1, 1},
       .spacing_x = 2,
       .spacing_y = 2},
      {.layer = upper, .enclosure_overhang1 = 2, .enclosure_overhang2 = 2}));

  ASSERT_NO_THROW(FPI.wrapDatabase());
  auto& database = FPDM.getDatabase();
  auto& pin = database.get_io_pin_list().front();
  pin.set_coord(40, 50);
  pin.set_placed(true);
  pin.set_updated(true);
  ifp::IOPort port;
  port.set_layer_name("M1");
  port.set_coord(40, 50);
  port.set_rect(-5, -2, 5, 2);
  port.set_placed(true);
  pin.get_new_port_list().push_back(std::move(port));

  ifp::PGSegment stripe;
  stripe.set_net_name("VDD");
  stripe.set_layer_name("M1");
  stripe.set_type(ifp::PGSegmentType::kStripe);
  stripe.set_width(10);
  stripe.set_start_coord(10, 50);
  stripe.set_end_coord(90, 50);
  stripe.set_generated(true);
  database.get_pg_segment_list().push_back(std::move(stripe));

  ifp::PGSegment via;
  via.set_net_name("VDD");
  via.set_type(ifp::PGSegmentType::kVia);
  via.set_bottom_layer_name("M1");
  via.set_top_layer_name("M2");
  via.set_via_width(10);
  via.set_via_height(10);
  via.set_start_coord(40, 50);
  via.set_generated(true);
  database.get_pg_segment_list().push_back(std::move(via));

  ASSERT_NO_THROW(FPI.output());
  EXPECT_FALSE(pin.is_updated());
  EXPECT_TRUE(pin.get_new_port_list().empty());
  const auto term = design.netlistStorage().findBlockTerm("VDD");
  const auto count_shapes = [&]() {
    std::size_t count = 0;
    for (const auto wire : design.routingStorage().wires(vdd)) {
      design.routingStorage().forEachShape(wire, [&](auto, const auto&, const auto*) { ++count; });
    }
    return count;
  };
  const auto expected_wires = design.routingStorage().wires(vdd).size();
  const auto expected_shapes = count_shapes();
  const auto expected_vias = design.routingStorage().viaCount();
  const auto expected_ports = design.netlistStorage().blockPins(term).size();
  const auto expected_terms = design.netlistStorage().instanceTerms(vdd).size();
  ASSERT_EQ(expected_wires, 2u);
  ASSERT_EQ(expected_shapes, 3u);
  ASSERT_EQ(expected_vias, 1u);
  ASSERT_EQ(expected_ports, 1u);
  ASSERT_EQ(expected_terms, 1u);

  ASSERT_NO_THROW(FPI.output());
  EXPECT_EQ(design.routingStorage().wires(vdd).size(), expected_wires);
  EXPECT_EQ(count_shapes(), expected_shapes);
  EXPECT_EQ(design.routingStorage().viaCount(), expected_vias);
  EXPECT_EQ(design.netlistStorage().blockPins(term).size(), expected_ports);
  EXPECT_EQ(design.netlistStorage().instanceTerms(vdd).size(), expected_terms);
}

TEST_F(IfpNativeAdapterTest, ReusesFixedDesignViaWithoutGenerateRule)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  const auto existing = createFixedVia("V1_10x10", routing_layer, cut, upper);
  eccdb::RoutingService routing{context};
  const auto original_geometry = routing.via(existing).rectangles;
  ASSERT_EQ(original_geometry.size(), 3u);

  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M2", {20, 20});
  requestVia("M1", "M2", {40, 20});
  ASSERT_NO_THROW(FPI.output());
  auto shapes = placedVias();
  ASSERT_EQ(shapes.size(), 2u);
  for (const auto& shape : shapes) {
    EXPECT_EQ(shape.entry, routing_layer);
    EXPECT_EQ(shape.geometry.exit_layer, upper);
    EXPECT_EQ(shape.geometry.via.design_via, existing);
    EXPECT_FALSE(shape.geometry.via.tech_via);
  }
  EXPECT_EQ(design.routingStorage().viaCount(), 1u);
  EXPECT_EQ(routing.findVia("V1_10x10"), existing);
  EXPECT_EQ(routing.via(existing).rectangles, original_geometry);

  // A later writeback has a fresh adapter cache but must still reuse the
  // persistent VIA definition rather than creating another one.
  requestVia("M1", "M2", {60, 20});
  ASSERT_NO_THROW(FPI.output());
  shapes = placedVias();
  ASSERT_EQ(shapes.size(), 3u);
  for (const auto& shape : shapes) {
    EXPECT_EQ(shape.geometry.via.design_via, existing);
  }
  EXPECT_EQ(design.routingStorage().viaCount(), 1u);
  EXPECT_EQ(routing.via(existing).rectangles, original_geometry);

  std::vector<eccdb::Rect> rectangles;
  const eccdb::PhysicalDesignService physical{context};
  physical.forEachDesignViaGeometry(existing, {.rectangle = [&](const eccdb::PhysicalLayerRect& rect) {
                                      rectangles.push_back(rect.rectangle);
                                    }});
  EXPECT_EQ(rectangles, (std::vector<eccdb::Rect>{{-5, -5, 5, 5}, {-1, -1, 1, 1}, {-5, -5, 5, 5}}));
}

TEST_F(IfpNativeAdapterTest, ReusesFixedTechViaWithoutGenerateRule)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  const auto existing = technology.viaMasterStorage().createFixedViaMaster(
      eccdb::TechViaMaster{.name = "V1_10x10"},
      eccdb::TechViaMasterShapeInput{.bottom_layer = routing_layer,
                                    .bottom_geometry = {.rects = {{-5, -5, 5, 5}}},
                                    .cut_layer = cut,
                                    .cut_geometry = {.rects = {{-1, -1, 1, 1}}},
                                    .top_layer = upper,
                                    .top_geometry = {.rects = {{-5, -5, 5, 5}}}});

  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M2", {20, 20});
  ASSERT_NO_THROW(FPI.output());
  const auto shapes = placedVias();
  ASSERT_EQ(shapes.size(), 1u);
  EXPECT_EQ(shapes.front().geometry.via.tech_via, existing);
  EXPECT_FALSE(shapes.front().geometry.via.design_via);
  EXPECT_EQ(shapes.front().entry, routing_layer);
  EXPECT_EQ(shapes.front().geometry.exit_layer, upper);
  EXPECT_EQ(design.routingStorage().viaCount(), 0u);
}

TEST_F(IfpNativeAdapterTest, IncompatibleNamedViaIsPreservedWhenGeneratingFallback)
{
  const auto cut1 = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto middle = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  const auto cut2 = technology.createCutLayer({.name = "V2"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M3"}, {.direction = eccdb::TechRoutingDirection::kHorizontal, .width = 10, .min_width = 10});
  const auto existing = createFixedVia("V1_10x10", middle, cut2, upper);
  const auto rule = createViaRule("GEN12", routing_layer, cut1, middle);
  eccdb::RoutingService routing{context};
  const auto original_geometry = routing.via(existing).rectangles;

  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M2", {20, 20});
  ASSERT_NO_THROW(FPI.output());
  const auto shapes = placedVias();
  ASSERT_EQ(shapes.size(), 1u);
  const auto generated = shapes.front().geometry.via.design_via;
  ASSERT_TRUE(generated);
  EXPECT_NE(generated, existing);
  EXPECT_EQ(routing.via(generated).generated.via_rule, rule);
  EXPECT_NE(routing.via(generated).name, "V1_10x10");
  EXPECT_EQ(routing.findVia("V1_10x10"), existing);
  EXPECT_EQ(routing.via(existing).rectangles, original_geometry);
  EXPECT_EQ(design.routingStorage().viaCount(), 2u);
}

TEST_F(IfpNativeAdapterTest, ConflictingDesignAndTechViaNamesUseAnIsolatedGeneratedDefinition)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  const auto local = createFixedVia("V1_10x10", routing_layer, cut, upper);
  const auto master = technology.viaMasterStorage().createFixedViaMaster(
      eccdb::TechViaMaster{.name = "V1_10x10"},
      eccdb::TechViaMasterShapeInput{.bottom_layer = routing_layer,
                                    .bottom_geometry = {.rects = {{-4, -4, 4, 4}}},
                                    .cut_layer = cut,
                                    .cut_geometry = {.rects = {{-1, -1, 1, 1}}},
                                    .top_layer = upper,
                                    .top_geometry = {.rects = {{-4, -4, 4, 4}}}});
  const auto rule = createViaRule("GEN12", routing_layer, cut, upper);
  const eccdb::RoutingService routing{context};
  const auto original_geometry = routing.via(local).rectangles;

  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M2", {20, 20});
  ASSERT_NO_THROW(FPI.output());
  const auto shapes = placedVias();
  ASSERT_EQ(shapes.size(), 1u);
  const auto generated = shapes.front().geometry.via.design_via;
  ASSERT_TRUE(generated);
  EXPECT_NE(generated, local);
  EXPECT_FALSE(shapes.front().geometry.via.tech_via);
  EXPECT_EQ(routing.via(generated).generated.via_rule, rule);
  EXPECT_NE(routing.via(generated).name, "V1_10x10");
  EXPECT_EQ(routing.findVia("V1_10x10"), local);
  EXPECT_EQ(routing.via(local).rectangles, original_geometry);
  EXPECT_EQ(technology.viaMasterStorage().findViaMaster("V1_10x10"), master);
}

TEST_F(IfpNativeAdapterTest, UnresolvableViaFailsWithoutOverwritingExistingDefinition)
{
  const auto cut1 = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto middle = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  const auto cut2 = technology.createCutLayer({.name = "V2"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M3"}, {.direction = eccdb::TechRoutingDirection::kHorizontal, .width = 10, .min_width = 10});
  const auto existing = createFixedVia("V1_10x10", middle, cut2, upper);
  const eccdb::RoutingService routing{context};
  const auto original_geometry = routing.via(existing).rectangles;
  static_cast<void>(cut1);

  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M2", {20, 20});
  EXPECT_THROW(FPI.output(), std::invalid_argument);
  EXPECT_EQ(routing.findVia("V1_10x10"), existing);
  EXPECT_EQ(routing.via(existing).rectangles, original_geometry);
  EXPECT_EQ(design.routingStorage().viaCount(), 1u);
  EXPECT_TRUE(placedVias().empty());
}

TEST_F(IfpNativeAdapterTest, GeneratedViaEnclosureFillsRequestedArea)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  const auto rule = createViaRule("GEN12", routing_layer, cut, upper, true);
  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M2", {40, 20}, 14, 14);
  ASSERT_NO_THROW(FPI.output());

  const auto shapes = placedVias();
  ASSERT_EQ(shapes.size(), 1u);
  const auto definition = shapes.front().geometry.via.design_via;
  ASSERT_TRUE(definition);
  const eccdb::RoutingService routing{context};
  const auto& generated = routing.via(definition).generated;
  EXPECT_EQ(generated.via_rule, rule);
  EXPECT_EQ(generated.row_count, 2u);
  EXPECT_EQ(generated.column_count, 2u);
  EXPECT_EQ(generated.cut_spacing_x, 4);
  EXPECT_EQ(generated.cut_spacing_y, 4);
  EXPECT_EQ(generated.bottom_enclosure_x, 3);
  EXPECT_EQ(generated.bottom_enclosure_y, 3);
  EXPECT_EQ(generated.top_enclosure_x, 3);
  EXPECT_EQ(generated.top_enclosure_y, 3);

  std::vector<eccdb::Rect> metal_rectangles;
  std::vector<eccdb::Rect> cut_rectangles;
  const eccdb::PhysicalDesignService physical{context};
  physical.forEachDesignViaGeometry(definition, {.rectangle = [&](const eccdb::PhysicalLayerRect& rect) {
                                      if (rect.layer == eccdb::TechLayerId{cut.entity()}) {
                                        cut_rectangles.push_back(rect.rectangle);
                                      } else {
                                        metal_rectangles.push_back(rect.rectangle);
                                      }
                                    }});
  EXPECT_EQ(cut_rectangles.size(), 4u);
  ASSERT_EQ(metal_rectangles.size(), 2u);
  EXPECT_EQ(metal_rectangles[0], (eccdb::Rect{-7, -7, 7, 7}));
  EXPECT_EQ(metal_rectangles[1], (eccdb::Rect{-7, -7, 7, 7}));
}

TEST_F(IfpNativeAdapterTest, GeneratedViaUsesIdbCompatibleName)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  const auto rule = createViaRule("GEN12", routing_layer, cut, upper, true);

  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M2", {40, 20}, 14, 14);
  ASSERT_NO_THROW(FPI.output());

  const auto shapes = placedVias();
  ASSERT_EQ(shapes.size(), 1u);
  ASSERT_TRUE(shapes.front().geometry.via.design_via);
  const eccdb::RoutingService routing{context};
  const auto definition = shapes.front().geometry.via.design_via;
  EXPECT_EQ(routing.via(definition).name, "V1_14x14");
  EXPECT_EQ(routing.via(definition).generated.via_rule, rule);
  EXPECT_EQ(routing.via(definition).generated.row_count, 2u);
  EXPECT_EQ(routing.via(definition).generated.column_count, 2u);
}

TEST_F(IfpNativeAdapterTest, GeneratedViaNameUsesCutLayerAndRequestedDimensions)
{
  // OpenROAD/iDB uses this convention for generated PG VIAs.  Keep the
  // regression at the adaptor boundary so the native iFP implementation is
  // not coupled to EccDB's internal Via IDs or generated-rule names.
  const auto cut = technology.createCutLayer({.name = "VIA1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  const auto rule = createViaRule("GEN12", routing_layer, cut, upper, true);

  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M2", {40, 20}, 1000, 160);
  ASSERT_NO_THROW(FPI.output());

  const auto shapes = placedVias();
  ASSERT_EQ(shapes.size(), 1u);
  ASSERT_TRUE(shapes.front().geometry.via.design_via);
  const eccdb::RoutingService routing{context};
  const auto definition = shapes.front().geometry.via.design_via;
  EXPECT_EQ(routing.via(definition).name, "VIA1_1000x160");
  EXPECT_EQ(routing.via(definition).generated.via_rule, rule);
  EXPECT_EQ(routing.via(definition).generated.row_count, 26u);
  EXPECT_EQ(routing.via(definition).generated.column_count, 166u);
}

TEST_F(IfpNativeAdapterTest, ViaRulesUseNameOrderAndPreferDefault)
{
  const auto cut1 = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto m2 = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  const auto cut2 = technology.createCutLayer({.name = "V2"}, {.width = 2});
  const auto m3 = technology.createRoutingLayer(
      {.name = "M3"}, {.direction = eccdb::TechRoutingDirection::kHorizontal, .width = 10, .min_width = 10});
  const auto cut3 = technology.createCutLayer({.name = "V3"}, {.width = 2});
  const auto m4 = technology.createRoutingLayer(
      {.name = "M4"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});

  static_cast<void>(createViaRule("Z1", routing_layer, cut1, m2));
  const auto expected1 = createViaRule("A1", routing_layer, cut1, m2);
  const auto expected2 = createViaRule("A2", m2, cut2, m3);
  static_cast<void>(createViaRule("Z2", m2, cut2, m3));
  static_cast<void>(createViaRule("A3", m3, cut3, m4));
  const auto expected3 = createViaRule("Z3", m3, cut3, m4, true);

  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M2", {20, 20});
  requestVia("M2", "M3", {40, 20});
  requestVia("M3", "M4", {60, 20});
  ASSERT_NO_THROW(FPI.output());
  const auto shapes = placedVias();
  ASSERT_EQ(shapes.size(), 3u);
  const eccdb::RoutingService routing{context};
  const std::array expected{expected1, expected2, expected3};
  for (std::size_t index = 0; index < shapes.size(); ++index) {
    ASSERT_TRUE(shapes[index].geometry.via.design_via);
    EXPECT_EQ(routing.via(shapes[index].geometry.via.design_via).generated.via_rule, expected[index]);
  }
}

TEST_F(IfpNativeAdapterTest, ViaRuleFallsBackWhenPreferredRuleCannotFit)
{
  const auto cut = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  static_cast<void>(createViaRule("PREFERRED_TOO_LARGE", routing_layer, cut, upper, true, 10, 24, 2));
  const auto fallback = createViaRule("SMALL", routing_layer, cut, upper);
  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M2", {20, 20}, 10, 10);
  ASSERT_NO_THROW(FPI.output());
  const auto shapes = placedVias();
  ASSERT_EQ(shapes.size(), 1u);
  ASSERT_TRUE(shapes.front().geometry.via.design_via);
  const eccdb::RoutingService routing{context};
  const auto& generated = routing.via(shapes.front().geometry.via.design_via).generated;
  EXPECT_EQ(generated.via_rule, fallback);
  EXPECT_EQ(generated.cut_size_x, 2);
  EXPECT_EQ(generated.cut_size_y, 2);
}

TEST_F(IfpNativeAdapterTest, StackedViaReusesAndGeneratesInBothDirections)
{
  const auto cut1 = technology.createCutLayer({.name = "V1"}, {.width = 2});
  const auto middle = technology.createRoutingLayer(
      {.name = "M2"}, {.direction = eccdb::TechRoutingDirection::kVertical, .width = 10, .min_width = 10});
  const auto cut2 = technology.createCutLayer({.name = "V2"}, {.width = 2});
  const auto upper = technology.createRoutingLayer(
      {.name = "M3"}, {.direction = eccdb::TechRoutingDirection::kHorizontal, .width = 10, .min_width = 10});
  const auto existing = createFixedVia("V1_10x10", routing_layer, cut1, middle);
  const auto rule = createViaRule("GEN23", middle, cut2, upper);

  ASSERT_NO_THROW(FPI.wrapDatabase());
  requestVia("M1", "M3", {20, 20});
  requestVia("M3", "M1", {40, 20});
  ASSERT_NO_THROW(FPI.output());
  const auto shapes = placedVias();
  ASSERT_EQ(shapes.size(), 4u);
  const eccdb::RoutingService routing{context};
  const auto generated = shapes[1].geometry.via.design_via;
  ASSERT_TRUE(generated);
  EXPECT_EQ(routing.via(generated).generated.via_rule, rule);
  EXPECT_EQ(shapes[0].geometry.via.design_via, existing);
  EXPECT_EQ(shapes[0].entry, routing_layer);
  EXPECT_EQ(shapes[0].geometry.exit_layer, middle);
  EXPECT_EQ(shapes[1].entry, middle);
  EXPECT_EQ(shapes[1].geometry.exit_layer, upper);
  EXPECT_EQ(shapes[2].geometry.via.design_via, generated);
  EXPECT_EQ(shapes[2].entry, upper);
  EXPECT_EQ(shapes[2].geometry.exit_layer, middle);
  EXPECT_EQ(shapes[3].geometry.via.design_via, existing);
  EXPECT_EQ(shapes[3].entry, middle);
  EXPECT_EQ(shapes[3].geometry.exit_layer, routing_layer);
  EXPECT_EQ(design.routingStorage().viaCount(), 2u);
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

TEST_F(IfpNativeAdapterTest, FloorplanRouteHaloServicePreservesInstanceMasterAndTermConnectivity)
{
  const auto term = design.netlistStorage().findInstanceTerm(instance, vdd_term);
  ASSERT_TRUE(term);
  eccdb::FloorplanService floorplan{context};
  const eccdb::DesignInstanceRouteHalo halo{.distance = 7, .min_layer = routing_layer, .max_layer = routing_layer};

  floorplan.setInstanceRouteHalo(instance, halo);
  const auto& configured = design.netlistStorage().instance(instance);
  EXPECT_NE(configured.flags & eccdb::DesignInstanceFlag::kHasRouteHalo, 0u);
  EXPECT_EQ(configured.route_halo.distance, 7);
  EXPECT_EQ(configured.route_halo.min_layer, routing_layer);
  EXPECT_EQ(configured.route_halo.max_layer, routing_layer);
  EXPECT_EQ(configured.master, master);
  EXPECT_EQ(design.netlistStorage().findInstanceTerm(instance, vdd_term), term);
  EXPECT_EQ(design.netlistStorage().instanceTerm(term).instance, instance);
  EXPECT_EQ(design.netlistStorage().instanceTerm(term).master_term, vdd_term);
  EXPECT_EQ(design.netlistStorage().instanceTerm(term).special_net, vdd);

  floorplan.clearInstanceRouteHalo(instance);
  const auto& cleared = design.netlistStorage().instance(instance);
  EXPECT_EQ(cleared.flags & eccdb::DesignInstanceFlag::kHasRouteHalo, 0u);
  EXPECT_EQ(cleared.route_halo.distance, 0);
  EXPECT_FALSE(cleared.route_halo.min_layer);
  EXPECT_FALSE(cleared.route_halo.max_layer);
  EXPECT_EQ(cleared.master, master);
  EXPECT_EQ(design.netlistStorage().findInstanceTerm(instance, vdd_term), term);
  EXPECT_EQ(design.netlistStorage().instanceTerm(term).special_net, vdd);
}

}  // namespace
