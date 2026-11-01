// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>

#include <any>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "MJInterface.hpp"
#include "MJInterfaceEccdb.h"
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
#include "library/master_port/storage/MasterPortInput.h"
#include "library/site/model/SiteComponents.h"
#include "service/antenna/AntennaService.h"
#include "service/connectivity/ConnectivityService.h"
#include "service/floorplan/FloorplanService.h"
#include "service/instance/InstanceService.h"
#include "service/routing/RoutingService.h"
#include "tech/common/TechLayerAntennaComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/TechStore.h"
#include "tech/masterslice_layer/model/MastersliceLayerComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace {

class ImjNativeAdapterTest : public testing::Test
{
 protected:
  ImjNativeAdapterTest()
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

    design.globalStorage().setInfo({.name = "imj-native", .database_units_per_micron = 1000});
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
    eccdb::adapters::bindMjSource(MJI, &context);
  }

  void TearDown() override
  {
    eccdb::adapters::bindMjSource(MJI, nullptr);
    imj::MJInterface::destroyInst();
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

  struct RoutedAntennaNet
  {
    eccdb::DesignNetId net;
    eccdb::LibraryMasterTermId term;
  };

  void setAntennaRule(eccdb::TechLayerId layer, std::vector<eccdb::AntennaValue> values)
  {
    eccdb::AntennaModel model{.flags = eccdb::AntennaModelFlag::kHasValues, .values = std::move(values)};
    eccdb::AntennaService antenna{context};
    antenna.setLayerAntenna(layer, {.flags = eccdb::TechLayerAntennaFlag::kHasModels, .models = {std::move(model)}});
  }

  void setM1AntennaRule(std::vector<eccdb::AntennaValue> values)
  {
    setAntennaRule(eccdb::TechLayerId{routing_layer.entity()}, std::move(values));
  }

  RoutedAntennaNet createRoutedAntennaNet(std::string name,
                                           eccdb::LibraryMasterTermDirection direction,
                                           eccdb::LibraryMasterTermAntenna antenna_metadata)
  {
    const auto master = library.cellMasterStorage().createCellMaster(
        {.name = name + "_CELL", .type = eccdb::LibraryCellMasterType::kCore, .site = site, .width = 10, .height = 10});
    const auto term = library.masterTermStorage().createMasterTerm(
        master, {.name = "A", .direction = direction, .use = eccdb::LibraryMasterTermUse::kSignal});
    static_cast<void>(library.masterPortStorage().createMasterPort(
        term,
        {.layer_clauses = {{.layer = eccdb::TechLayerId{routing_layer.entity()}, .geometry = {.rects = {{0, 15, 10, 25}}}}}}));

    eccdb::AntennaService antenna{context};
    antenna.setMasterTermAntenna(term, std::move(antenna_metadata));

    eccdb::InstanceService instances{context};
    const auto instance = instances.create({.name = name + "_inst",
                                            .master = master,
                                            .origin = {0, 0},
                                            .orientation = eccdb::DesignOrientation::kN,
                                            .placement_status = eccdb::DesignPlacementStatus::kPlaced});
    eccdb::ConnectivityService connectivity{context};
    const auto net = connectivity.createNet({.name = std::move(name), .use = eccdb::DesignSignalUse::kSignal});
    connectivity.connect(design.netlistStorage().findInstanceTerm(instance, term), net);
    eccdb::RoutingService routing{context};
    const auto wire = routing.createWire({.net = net, .status = eccdb::DesignWireStatus::kRouted});
    static_cast<void>(routing.createShape(
        wire,
        {.wire = wire,
         .layer = routing_layer,
         .geometry = eccdb::DesignRouteSegment{.begin = {.position = {0, 20}}, .end = {.position = {100, 20}}}}));
    return {.net = net, .term = term};
  }
};

TEST_F(ImjNativeAdapterTest, FillerReadsRowsAndWritesDistributionInstances)
{
  std::map<std::string, std::any> config{{"-filler", std::string{"FILL2 FILL1"}}, {"-min_filler_width", int32_t{1}}};

  ASSERT_NO_THROW(MJI.insertFiller(config));

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

TEST_F(ImjNativeAdapterTest, ExternalSourceRejectsUnsupportedLegacyStages)
{
  EXPECT_TRUE(MJI.hasDesignOperations());
  EXPECT_THROW(MJI.flattenDef({}), std::logic_error);

  eccdb::adapters::bindMjSource(MJI, nullptr);
  EXPECT_FALSE(MJI.hasDesignOperations());
  eccdb::adapters::bindMjSource(MJI, &context);
}

TEST_F(ImjNativeAdapterTest, FillerSpatialRowsPreserveRotatedCrossRowAndTouchingSemantics)
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
  ASSERT_NO_THROW(MJI.insertFiller(config));

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

TEST_F(ImjNativeAdapterTest, FillerBlocksHardPlacementBlockagesButNotInitialPlacementOnlyBlockages)
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
  ASSERT_NO_THROW(MJI.insertFiller(config));

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

TEST_F(ImjNativeAdapterTest, MetalReadsTracksAndWritesNativeFill)
{
  std::map<std::string, std::any> config{{"-min_fill_layer", std::string{"M1"}}, {"-max_fill_layer", std::string{"M1"}}};

  ASSERT_NO_THROW(MJI.insertMetal(config));

  ASSERT_EQ(design.fillStorage().fillCount(), 1u);
  const auto fills = design.fillStorage().fills();
  ASSERT_EQ(fills.size(), 1u);
  const auto& fill = design.fillStorage().fill(fills.front());
  EXPECT_EQ(fill.layer, eccdb::TechLayerId{routing_layer.entity()});
  EXPECT_FALSE(fill.rectangles.empty());
}

TEST_F(ImjNativeAdapterTest, AntennaReadsNativeMetadataAndRouteGeometry)
{
  createRoutedAntennaNet("N_SIGNAL", eccdb::LibraryMasterTermDirection::kInput,
                          {.flags = eccdb::LibraryMasterTermAntennaFlag::kHasValues,
                           .values = {{.metric = eccdb::AntennaMetric::kGateArea, .value = 0.0001}}});
  setM1AntennaRule({{.metric = eccdb::AntennaMetric::kAreaRatio, .value = 20.0}});

  ASSERT_NO_THROW(MJI.checkAntenna({}));
  const auto& clean_summary = MJI.lastAntennaSummary();
  EXPECT_TRUE(clean_summary.checked);
  EXPECT_TRUE(clean_summary.coverage_complete);
  EXPECT_EQ(clean_summary.net_count, 1u);
  EXPECT_EQ(clean_summary.connected_net_count, 1u);
  EXPECT_EQ(clean_summary.wire_count, 1u);
  EXPECT_EQ(clean_summary.violation_count, 0u);
  EXPECT_EQ(clean_summary.pins_missing_antenna_info, 0u);
  EXPECT_EQ(clean_summary.nets_without_terminal_antenna_info, 0u);

  setM1AntennaRule({{.metric = eccdb::AntennaMetric::kAreaRatio, .value = 1.0}});
  ASSERT_NO_THROW(MJI.checkAntenna({}));
  const auto& violating_summary = MJI.lastAntennaSummary();
  EXPECT_EQ(violating_summary.violation_count, 1u);
  ASSERT_EQ(violating_summary.violations.size(), 1u);
  EXPECT_NE(violating_summary.violations.front().find("N_SIGNAL"), std::string::npos);
}

TEST_F(ImjNativeAdapterTest, AntennaWritesReportWhenTechnologyHasNoRules)
{
  const auto report = std::filesystem::temp_directory_path() / "ecc_izh_native_antenna_no_rules.report";
  std::error_code cleanup_error;
  std::filesystem::remove(report, cleanup_error);

  ASSERT_NO_THROW(MJI.checkAntenna({{"-antenna_report_file", report.string()}}));
  const auto& summary = MJI.lastAntennaSummary();
  EXPECT_FALSE(summary.checked);
  EXPECT_FALSE(summary.coverage_complete);
  EXPECT_EQ(summary.rule_layer_count, 0u);
  EXPECT_EQ(summary.report_path, report.string());
  ASSERT_TRUE(std::filesystem::exists(report));
  std::ifstream input(report);
  const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  EXPECT_NE(text.find("checked=0 coverage_complete=0"), std::string::npos);
  std::filesystem::remove(report, cleanup_error);
}

TEST_F(ImjNativeAdapterTest, AntennaTreatsDiffusionOnlyTerminalAsValidCoverage)
{
  createRoutedAntennaNet("N_DIFF", eccdb::LibraryMasterTermDirection::kOutput,
                          {.flags = eccdb::LibraryMasterTermAntennaFlag::kHasValues,
                           .values = {{.metric = eccdb::AntennaMetric::kDiffArea, .value = 0.0001}}});
  setM1AntennaRule({{.metric = eccdb::AntennaMetric::kAreaRatio, .value = 20.0},
                    {.metric = eccdb::AntennaMetric::kGatePlusDiff, .value = 1.0}});

  ASSERT_NO_THROW(MJI.checkAntenna({}));
  const auto& summary = MJI.lastAntennaSummary();
  EXPECT_TRUE(summary.checked);
  EXPECT_TRUE(summary.coverage_complete);
  EXPECT_EQ(summary.nets_without_gate, 1u);
  EXPECT_EQ(summary.nets_without_terminal_antenna_info, 0u);
  EXPECT_EQ(summary.pins_missing_antenna_info, 0u);
  EXPECT_EQ(summary.violation_count, 0u);
}

TEST_F(ImjNativeAdapterTest, AntennaMarksMultipleOxideModelsAsIncomplete)
{
  createRoutedAntennaNet("N_OXIDE", eccdb::LibraryMasterTermDirection::kInput,
                          {.flags = eccdb::LibraryMasterTermAntennaFlag::kHasValues,
                           .values = {{.metric = eccdb::AntennaMetric::kGateArea, .value = 0.0001}}});
  eccdb::AntennaService antenna{context};
  antenna.setLayerAntenna(
      eccdb::TechLayerId{routing_layer.entity()},
      {.flags = eccdb::TechLayerAntennaFlag::kHasModels,
       .models = {{.flags = eccdb::AntennaModelFlag::kHasValues,
                   .values = {{.metric = eccdb::AntennaMetric::kAreaRatio, .value = 20.0}}},
                  {.flags = eccdb::AntennaModelFlag::kHasValues,
                   .oxide = "OXIDE2",
                   .values = {{.metric = eccdb::AntennaMetric::kAreaRatio, .value = 10.0}}}}});

  ASSERT_NO_THROW(MJI.checkAntenna({}));
  const auto& summary = MJI.lastAntennaSummary();
  EXPECT_TRUE(summary.checked);
  EXPECT_FALSE(summary.coverage_complete);
  EXPECT_EQ(summary.unsupported_oxide_model_count, 1u);
  EXPECT_EQ(summary.pins_missing_antenna_info, 0u);
  EXPECT_EQ(summary.nets_without_terminal_antenna_info, 0u);
}

TEST_F(ImjNativeAdapterTest, AntennaDoesNotTreatTopIoPortGeometryAsRoutedWire)
{
  setM1AntennaRule({{.metric = eccdb::AntennaMetric::kAreaRatio, .value = 2.0}});
  eccdb::ConnectivityService connectivity{context};
  const auto net = connectivity.createNet({.name = "N_TOP_IO_ONLY", .use = eccdb::DesignSignalUse::kSignal});
  const auto top_io = connectivity.createBlockTerm(
      {.name = "TOP_IO", .direction = eccdb::DesignIoPinDirection::kInput, .use = eccdb::DesignSignalUse::kSignal},
      std::vector<eccdb::DesignBlockPinInput>{eccdb::DesignBlockPinInput{
          .rectangles = {{.layer = eccdb::TechLayerId{routing_layer.entity()}, .rectangle = {0, 0, 10, 10}}}}});
  connectivity.connect(top_io, net);

  ASSERT_NO_THROW(MJI.checkAntenna({}));
  const auto& summary = MJI.lastAntennaSummary();
  EXPECT_TRUE(summary.checked);
  EXPECT_TRUE(summary.coverage_complete);
  EXPECT_EQ(summary.net_count, 1u);
  EXPECT_EQ(summary.connected_net_count, 1u);
  EXPECT_EQ(summary.wire_count, 0u);
  EXPECT_EQ(summary.nets_without_gate, 0u);
  EXPECT_EQ(summary.nets_without_terminal_antenna_info, 0u);
  EXPECT_EQ(summary.pins_missing_antenna_info, 0u);
}

TEST_F(ImjNativeAdapterTest, AntennaMarksRoutedWireWithoutTerminalMetadataIncomplete)
{
  createRoutedAntennaNet("N_WIRE_NO_METADATA", eccdb::LibraryMasterTermDirection::kInput, {});
  setM1AntennaRule({{.metric = eccdb::AntennaMetric::kAreaRatio, .value = 2.0}});

  ASSERT_NO_THROW(MJI.checkAntenna({}));
  const auto& summary = MJI.lastAntennaSummary();
  EXPECT_TRUE(summary.checked);
  EXPECT_FALSE(summary.coverage_complete);
  EXPECT_EQ(summary.wire_count, 1u);
  EXPECT_EQ(summary.pins_missing_antenna_info, 1u);
  EXPECT_EQ(summary.nets_without_gate, 1u);
  EXPECT_EQ(summary.nets_without_terminal_antenna_info, 1u);
}

TEST_F(ImjNativeAdapterTest, AntennaUsesMastersliceRuleForNativePinGeometry)
{
  // This is a native Service/binary compatibility fixture. LEF parsing still
  // rejects antenna declarations on MASTERSLICE; do not infer LEF legality
  // from this typed API regression.
  const auto poly = technology.createMastersliceLayer(
      {.name = "POLY"}, {.subtype = eccdb::TechMastersliceType::kTrimPoly});
  const auto master = library.cellMasterStorage().createCellMaster(
      {.name = "POLY_CELL", .type = eccdb::LibraryCellMasterType::kCore, .site = site, .width = 3000, .height = 1000});
  const auto term = library.masterTermStorage().createMasterTerm(
      master, {.name = "A", .direction = eccdb::LibraryMasterTermDirection::kInput, .use = eccdb::LibraryMasterTermUse::kSignal});
  static_cast<void>(library.masterPortStorage().createMasterPort(
      term,
      {.layer_clauses = {{.layer = eccdb::TechLayerId{poly.entity()}, .geometry = {.rects = {{0, 0, 3000, 1000}}}}}}));
  eccdb::AntennaService antenna{context};
  antenna.setMasterTermAntenna(term,
                                {.flags = eccdb::LibraryMasterTermAntennaFlag::kHasValues,
                                 .values = {{.metric = eccdb::AntennaMetric::kGateArea, .value = 1.0}}});
  setAntennaRule(eccdb::TechLayerId{poly.entity()}, {{.metric = eccdb::AntennaMetric::kAreaRatio, .value = 2.0}});

  eccdb::InstanceService instances{context};
  const auto instance = instances.create({.name = "u_poly",
                                            .master = master,
                                            .origin = {0, 0},
                                            .orientation = eccdb::DesignOrientation::kN,
                                            .placement_status = eccdb::DesignPlacementStatus::kPlaced});
  eccdb::ConnectivityService connectivity{context};
  const auto net = connectivity.createNet({.name = "N_POLY", .use = eccdb::DesignSignalUse::kSignal});
  connectivity.connect(design.netlistStorage().findInstanceTerm(instance, term), net);

  ASSERT_NO_THROW(MJI.checkAntenna({}));
  const auto& summary = MJI.lastAntennaSummary();
  EXPECT_TRUE(summary.checked);
  EXPECT_TRUE(summary.coverage_complete);
  EXPECT_EQ(summary.rule_layer_count, 1u);
  ASSERT_EQ(summary.violation_count, 1u);
  ASSERT_EQ(summary.violations.size(), 1u);
  const std::string expected_violation{"N_POLY:POLY PAR ratio=3 threshold=2 bbox=(0,0,3,1)"};
  EXPECT_EQ(summary.violations.front(), expected_violation);
}

}  // namespace
