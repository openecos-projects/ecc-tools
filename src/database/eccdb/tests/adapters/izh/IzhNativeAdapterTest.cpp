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

#include <any>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "ZHInterface.hpp"
#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/site/model/SiteComponents.h"
#include "service/connectivity/ConnectivityService.h"
#include "service/floorplan/FloorplanService.h"
#include "service/instance/InstanceService.h"
#include "service/routing/RoutingService.h"
#include "tech/TechStore.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace {

class IzhNativeAdapterTest : public testing::Test
{
 protected:
  IzhNativeAdapterTest()
      : library(technology.techRegistry()),
        design(technology.techRegistry(), library.libraryRegistry(), &technology.geometryRegistry()),
        context(technology, library, design)
  {
  }

  void SetUp() override
  {
    routing_layer = technology.createRoutingLayer(
        {.name = "M1"},
        {.direction = eccdb::TechRoutingDirection::kHorizontal, .width = 10, .min_width = 10, .pitch_x = 40, .pitch_y = 40, .offset_x = 20, .offset_y = 20});
    technology.globalStorage().setManufacturingGrid(1);
    site = library.siteStorage().createSite(
        {.name = "CORE", .width = 10, .height = 10, .site_class = eccdb::LibrarySiteClass::kCore});
    filler_two = library.cellMasterStorage().createCellMaster(
        {.name = "FILL2", .type = eccdb::LibraryCellMasterType::kCoreSpacer, .core_filler = true, .site = site, .width = 20, .height = 10});
    filler_one = library.cellMasterStorage().createCellMaster(
        {.name = "FILL1", .type = eccdb::LibraryCellMasterType::kCoreSpacer, .core_filler = true, .site = site, .width = 10, .height = 10});
    blocker = library.cellMasterStorage().createCellMaster(
        {.name = "BLOCK", .type = eccdb::LibraryCellMasterType::kCore, .site = site, .width = 20, .height = 10});

    design.globalStorage().setInfo({.name = "izh-native", .database_units_per_micron = 1000});
    eccdb::FloorplanService floorplan{context};
    floorplan.setDieArea({.boundary = {{0, 0}, {100, 0}, {100, 100}, {0, 100}}});
    floorplan.setCoreArea({.boundary = {{0, 0}, {100, 0}, {100, 10}, {0, 10}}});
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
        {.axis = eccdb::DesignAxis::kY, .start = 20, .track_count = 2, .step = 40, .layers = {routing_layer}}));
    eccdb::InstanceService instances{context};
    static_cast<void>(instances.create({.name = "u_block",
                                        .master = blocker,
                                        .origin = {30, 0},
                                        .orientation = eccdb::DesignOrientation::kN,
                                        .placement_status = eccdb::DesignPlacementStatus::kFixed}));
    ZHI.setDesignSource(context);
  }

  void TearDown() override
  {
    ZHI.setDesignSource(static_cast<eccdb::DatabaseContext*>(nullptr));
    izh::ZHInterface::destroyInst();
  }

  eccdb::TechStore technology;
  eccdb::LibraryStore library;
  eccdb::DesignStore design;
  eccdb::DatabaseContext context;
  eccdb::TechRoutingLayerId routing_layer;
  eccdb::LibrarySiteId site;
  eccdb::LibraryCellMasterId filler_two;
  eccdb::LibraryCellMasterId filler_one;
  eccdb::LibraryCellMasterId blocker;
};

TEST_F(IzhNativeAdapterTest, FillerReadsRowsAndWritesDistributionInstances)
{
  std::map<std::string, std::any> config{{"-filler", std::string{"FILL2 FILL1"}}, {"-min_filler_width", int32_t{1}}};

  ASSERT_NO_THROW(ZHI.insertFiller(config));

  std::vector<eccdb::DesignInstance> fillers;
  design.netlistStorage().forEachInstance(
      [&](eccdb::DesignInstanceId, const eccdb::DesignInstance& instance, std::span<const eccdb::DesignInstanceTermId>) {
        if (instance.source == eccdb::DesignInstanceSource::kDist) {
          fillers.push_back(instance);
        }
      });
  ASSERT_EQ(fillers.size(), 5u);
  EXPECT_EQ(design.netlistStorage().instanceCount(), 6u);
  for (const auto& filler : fillers) {
    EXPECT_EQ(filler.placement_status, eccdb::DesignPlacementStatus::kPlaced);
    EXPECT_NE(filler.origin.x, 30);
  }
}

TEST_F(IzhNativeAdapterTest, FillerSpatialRowsPreserveRotatedCrossRowAndTouchingSemantics)
{
  eccdb::FloorplanService floorplan{context};
  const auto create_row = [&](std::string name, int32_t y) {
    return floorplan.createRow({.name = std::move(name),
                                .site = site,
                                .origin = {0, y},
                                .orientation = eccdb::DesignOrientation::kN,
                                .repeat_count_x = 10,
                                .repeat_count_y = 1,
                                .step_x = 10,
                                .step_y = 0,
                                .flags = eccdb::DesignRowFlag::kHasDo | eccdb::DesignRowFlag::kHasStep});
  };
  static_cast<void>(create_row("ROW10", 10));
  static_cast<void>(create_row("ROW20", 20));
  eccdb::InstanceService instances{context};
  // E rotates the 20x10 master into a 10x20 obstacle spanning both rows.
  static_cast<void>(instances.create({.name = "u_rotated",
                                      .master = blocker,
                                      .origin = {70, 10},
                                      .orientation = eccdb::DesignOrientation::kE,
                                      .placement_status = eccdb::DesignPlacementStatus::kFixed}));
  static_cast<void>(instances.create({.name = "u_unplaced",
                                      .master = blocker,
                                      .origin = {0, 10},
                                      .orientation = eccdb::DesignOrientation::kN,
                                      .placement_status = eccdb::DesignPlacementStatus::kUnplaced}));
  // This fixed instance starts exactly on the upper boundary of ROW_GRID's
  // final physical row and must not consume its rightmost site.
  static_cast<void>(instances.create({.name = "u_touching",
                                      .master = blocker,
                                      .origin = {90, 30},
                                      .orientation = eccdb::DesignOrientation::kN,
                                      .placement_status = eccdb::DesignPlacementStatus::kFixed}));

  std::map<std::string, std::any> config{{"-filler", std::string{"FILL1"}}, {"-min_filler_width", int32_t{1}}};
  ASSERT_NO_THROW(ZHI.insertFiller(config));

  std::vector<eccdb::Point> origins;
  design.netlistStorage().forEachInstance(
      [&](eccdb::DesignInstanceId, const eccdb::DesignInstance& instance, std::span<const eccdb::DesignInstanceTermId>) {
        if (instance.source == eccdb::DesignInstanceSource::kDist) {
          origins.push_back(instance.origin);
        }
      });
  for (const auto row_y : {10, 20}) {
    for (int32_t row_x = 0; row_x < 100; row_x += 10) {
      const auto count = std::count(origins.begin(), origins.end(), eccdb::Point{row_x, row_y});
      EXPECT_EQ(count, row_x == 70 ? 0 : 1) << "row=" << row_y << " x=" << row_x;
    }
  }
}

TEST_F(IzhNativeAdapterTest, FillerBlocksHardPlacementBlockagesButNotInitialPlacementOnlyBlockages)
{
  eccdb::InstanceService instances{context};
  const auto cover = instances.create({.name = "u_cover",
                                       .master = blocker,
                                       .origin = {60, 0},
                                       .orientation = eccdb::DesignOrientation::kN,
                                       .placement_status = eccdb::DesignPlacementStatus::kCover});
  static_cast<void>(instances.create({.name = "u_unplaced",
                                      .master = blocker,
                                      .origin = {80, 0},
                                      .orientation = eccdb::DesignOrientation::kN,
                                      .placement_status = eccdb::DesignPlacementStatus::kUnplaced}));

  auto& constraints = design.constraintStorage();
  static_cast<void>(constraints.createBlockage(
      {.kind = eccdb::DesignBlockageKind::kPlacement, .rectangles = {{0, 0, 10, 10}}}));
  static_cast<void>(constraints.createBlockage(
      {.kind = eccdb::DesignBlockageKind::kPlacement,
       .flags = eccdb::DesignBlockageFlag::kSoft,
       .rectangles = {{10, 0, 20, 10}}}));
  static_cast<void>(constraints.createBlockage(
      {.kind = eccdb::DesignBlockageKind::kPlacement,
       .flags = eccdb::DesignBlockageFlag::kHasPartial,
       .partial = 50.0,
       .rectangles = {{20, 0, 30, 10}}}));
  static_cast<void>(constraints.createBlockage(
      {.kind = eccdb::DesignBlockageKind::kPlacement,
       .flags = eccdb::DesignBlockageFlag::kHasComponent | eccdb::DesignBlockageFlag::kPushdown,
       .component = cover,
       .rectangles = {{50, 0, 60, 10}}}));

  std::map<std::string, std::any> config{{"-filler", std::string{"FILL1"}}, {"-min_filler_width", int32_t{1}}};
  ASSERT_NO_THROW(ZHI.insertFiller(config));

  std::vector<eccdb::Point> origins;
  design.netlistStorage().forEachInstance(
      [&](eccdb::DesignInstanceId, const eccdb::DesignInstance& instance, std::span<const eccdb::DesignInstanceTermId>) {
        if (instance.source == eccdb::DesignInstanceSource::kDist) {
          origins.push_back(instance.origin);
        }
      });
  std::sort(origins.begin(), origins.end(), [](const eccdb::Point& lhs, const eccdb::Point& rhs) {
    return lhs.x == rhs.x ? lhs.y < rhs.y : lhs.x < rhs.x;
  });
  EXPECT_EQ(origins, (std::vector<eccdb::Point>{{10, 0}, {20, 0}, {80, 0}, {90, 0}}));
}

TEST_F(IzhNativeAdapterTest, MetalReadsTracksAndWritesNativeFill)
{
  std::map<std::string, std::any> config{{"-min_fill_layer", std::string{"M1"}}, {"-max_fill_layer", std::string{"M1"}}};

  ASSERT_NO_THROW(ZHI.insertMetal(config));

  ASSERT_EQ(design.fillStorage().fillCount(), 1u);
  const auto fills = design.fillStorage().fills();
  ASSERT_EQ(fills.size(), 1u);
  const auto& fill = design.fillStorage().fill(fills.front());
  EXPECT_EQ(fill.layer, eccdb::TechLayerId{routing_layer.entity()});
  EXPECT_FALSE(fill.rectangles.empty());
}

TEST_F(IzhNativeAdapterTest, AntennaReadsNativeConnectivityAndRouteWithExternalRules)
{
  const auto signal_master = library.cellMasterStorage().createCellMaster(
      {.name = "SIGNAL_CELL", .type = eccdb::LibraryCellMasterType::kCore, .site = site, .width = 10, .height = 10});
  const auto signal_term = library.masterTermStorage().createMasterTerm(
      signal_master,
      {.name = "A", .direction = eccdb::LibraryMasterTermDirection::kInput, .use = eccdb::LibraryMasterTermUse::kSignal});
  eccdb::InstanceService instances{context};
  const auto signal_instance = instances.create({.name = "u_signal",
                                                  .master = signal_master,
                                                  .origin = {0, 0},
                                                  .orientation = eccdb::DesignOrientation::kN,
                                                  .placement_status = eccdb::DesignPlacementStatus::kPlaced});
  eccdb::ConnectivityService connectivity{context};
  const auto net = connectivity.createNet({.name = "N_SIGNAL", .use = eccdb::DesignSignalUse::kSignal});
  connectivity.connect(design.netlistStorage().findInstanceTerm(signal_instance, signal_term), net);
  eccdb::RoutingService routing{context};
  const auto wire = routing.createWire({.net = net, .status = eccdb::DesignWireStatus::kRouted});
  static_cast<void>(routing.createShape(
      wire,
      {.wire = wire,
       .layer = routing_layer,
       .geometry = eccdb::DesignRouteSegment{.begin = {.position = {0, 20}}, .end = {.position = {100, 20}}}}));

  const auto rule_file = std::filesystem::temp_directory_path() / "ecc_izh_native_antenna.rules";
  std::error_code cleanup_error;
  std::filesystem::remove(rule_file, cleanup_error);
  {
    std::ofstream output(rule_file);
    ASSERT_TRUE(output.good());
    output << "GATE_AREA 1\nLAYER M1 2000\n";
  }
  std::map<std::string, std::any> config{{"-antenna_rule_file", rule_file.string()}};
  ASSERT_NO_THROW(ZHI.checkAntenna(config));
  const auto& clean_summary = ZHI.lastAntennaSummary();
  EXPECT_TRUE(clean_summary.checked);
  EXPECT_EQ(clean_summary.net_count, 1u);
  EXPECT_EQ(clean_summary.connected_net_count, 1u);
  EXPECT_EQ(clean_summary.wire_count, 1u);
  EXPECT_EQ(clean_summary.violation_count, 0u);

  {
    std::ofstream output(rule_file, std::ios::trunc);
    ASSERT_TRUE(output.good());
    output << "GATE_AREA 1\nLAYER M1 0.5\n";
  }
  ASSERT_NO_THROW(ZHI.checkAntenna(config));
  const auto& violating_summary = ZHI.lastAntennaSummary();
  EXPECT_EQ(violating_summary.violation_count, 1u);
  ASSERT_EQ(violating_summary.violations.size(), 1u);
  EXPECT_NE(violating_summary.violations.front().find("N_SIGNAL"), std::string::npos);
  std::filesystem::remove(rule_file, cleanup_error);
}

TEST_F(IzhNativeAdapterTest, AntennaRequiresExplicitExternalRules)
{
  EXPECT_THROW(ZHI.checkAntenna({}), std::invalid_argument);
}

}  // namespace
