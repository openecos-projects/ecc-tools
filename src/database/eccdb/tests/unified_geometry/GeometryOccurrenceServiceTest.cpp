// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "library/master_port/storage/MasterPortInput.h"
#include "service/geometry/GeometryOccurrenceService.h"
#include "tech/TechStore.h"
#include "tech/via_master/storage/ViaMasterInput.h"

namespace eccdb {
namespace {

class GeometryOccurrenceServiceTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    m1 = technology.createRoutingLayer(TechLayerInfo{.name = "M1"}, TechRoutingLayer{.width = 12});
    cut = technology.createCutLayer(TechLayerInfo{.name = "V1"}, TechCutLayer{.width = 4});
    m2 = technology.createRoutingLayer(TechLayerInfo{.name = "M2"}, TechRoutingLayer{.width = 14});
    via = technology.viaMasterStorage().createFixedViaMaster(
        TechViaMaster{.name = "VIA12"},
        TechViaMasterShapeInput{.bottom_layer = m1,
                                .bottom_geometry = {.rects = {{-2, -2, 2, 2}}},
                                .cut_layer = cut,
                                .cut_geometry = {.rects = {{-1, -1, 1, 1}}},
                                .top_layer = m2,
                                .top_geometry = {.rects = {{-3, -3, 3, 3}}}});

    const auto site = library.siteStorage().createSite(LibrarySite{.name = "CORE", .width = 20, .height = 40});
    master = library.cellMasterStorage().createCellMaster(
        LibraryCellMaster{.name = "CELL", .site = site, .width = 20, .height = 40});
    term = library.masterTermStorage().createMasterTerm(master, LibraryMasterTerm{.name = "A"});
    first_pin = library.masterPortStorage().createMasterPin(
        term,
        LibraryMasterPortInput{.layer_clauses = {{.layer = TechLayerId{m1.entity()},
                                                   .geometry = {.rects = {{1, 2, 5, 6}},
                                                                .polygons = {{.points = {{2, 1}, {4, 1}, {4, 3}, {2, 3}}}}}}},
                               .vias = {{.via = via, .origin = {10, 20}}}});
    second_pin = library.masterPortStorage().createMasterPin(
        term, LibraryMasterPortInput{.layer_clauses = {{.layer = TechLayerId{m1.entity()}, .geometry = {.rects = {{8, 10, 12, 14}}}}}});

    instance = design.netlistStorage().createInstance(DesignInstance{.name = "u0",
                                                                       .master = master,
                                                                       .origin = {100, 200},
                                                                       .orientation = DesignOrientation::kE,
                                                                       .placement_status = DesignPlacementStatus::kPlaced});
    iterm = design.netlistStorage().findInstanceTerm(instance, "A");
    ASSERT_TRUE(iterm);
    rotated_instance = design.netlistStorage().createInstance(
        DesignInstance{.name = "u1",
                       .master = master,
                       .origin = {300, 400},
                       .orientation = DesignOrientation::kS,
                       .placement_status = DesignPlacementStatus::kPlaced});
    rotated_iterm = design.netlistStorage().findInstanceTerm(rotated_instance, "A");
    ASSERT_TRUE(rotated_iterm);
    service = std::make_unique<GeometryOccurrenceService>(technology, library, design);
  }

  TechStore technology;
  LibraryStore library{technology.techRegistry()};
  DesignStore design{technology.techRegistry(), library.libraryRegistry()};
  std::unique_ptr<GeometryOccurrenceService> service;
  TechRoutingLayerId m1;
  TechCutLayerId cut;
  TechRoutingLayerId m2;
  TechViaMasterId via;
  LibraryCellMasterId master;
  LibraryMasterTermId term;
  LibraryMasterPinId first_pin;
  LibraryMasterPinId second_pin;
  DesignInstanceId instance;
  DesignInstanceTermId iterm;
  DesignInstanceId rotated_instance;
  DesignInstanceTermId rotated_iterm;
};

TEST_F(GeometryOccurrenceServiceTest, FetchesMasterPinInLocalCoordinatesWithoutADesignShapeId)
{
  const auto batch = service->fetchMasterPin(first_pin);

  ASSERT_EQ(batch.headers.size(), 1u);
  const auto& header = batch.headers.front();
  EXPECT_EQ(header.kind, GeometryOccurrenceKind::kMasterPin);
  EXPECT_EQ(header.master_pin, first_pin.packed());
  EXPECT_EQ(header.instance_term, UINT64_MAX);
  EXPECT_EQ(header.instance, UINT64_MAX);
  EXPECT_TRUE(header.has_bounds);
  EXPECT_EQ(header.bounds, (Rect{1, 1, 13, 23}));

  ASSERT_EQ(batch.rectangles.size(), 4u);
  EXPECT_EQ(batch.rectangles.front().header_index, 0u);
  EXPECT_EQ(batch.rectangles.front().layer, m1.packed());
  EXPECT_EQ(batch.rectangles.front().rectangle, (Rect{1, 2, 5, 6}));
  ASSERT_EQ(batch.polygons.size(), 1u);
  EXPECT_EQ(batch.polygons.front().header_index, 0u);
  EXPECT_EQ(batch.polygon_points, (std::vector<Point>{{2, 1}, {4, 1}, {4, 3}, {2, 3}}));
  ASSERT_EQ(batch.vias.size(), 1u);
  EXPECT_EQ(batch.vias.front().header_index, 0u);
  EXPECT_EQ(batch.vias.front().tech_via, via.packed());
  EXPECT_EQ(batch.vias.front().design_via, UINT64_MAX);
  EXPECT_EQ(batch.vias.front().origin, (Point{10, 20}));
}

TEST_F(GeometryOccurrenceServiceTest, FetchesOnePlacedOccurrenceHeaderPerMasterPin)
{
  const auto batch = service->fetchInstancePin(iterm);

  ASSERT_EQ(batch.headers.size(), 2u);
  const auto& first = batch.headers[0];
  EXPECT_EQ(first.kind, GeometryOccurrenceKind::kPlacedInstanceMasterPin);
  EXPECT_EQ(first.instance_term, iterm.packed());
  EXPECT_EQ(first.instance, instance.packed());
  EXPECT_EQ(first.master_pin, first_pin.packed());
  EXPECT_TRUE(first.has_bounds);
  EXPECT_EQ(first.bounds, (Rect{101, 207, 123, 219}));

  const auto& second = batch.headers[1];
  EXPECT_EQ(second.instance_term, iterm.packed());
  EXPECT_EQ(second.instance, instance.packed());
  EXPECT_EQ(second.master_pin, second_pin.packed());
  EXPECT_TRUE(second.has_bounds);
  EXPECT_EQ(second.bounds, (Rect{110, 208, 114, 212}));

  ASSERT_EQ(batch.rectangles.size(), 5u);
  EXPECT_EQ(batch.rectangles.front().header_index, 0u);
  EXPECT_EQ(batch.rectangles.front().rectangle, (Rect{102, 215, 106, 219}));
  EXPECT_EQ(batch.rectangles.back().header_index, 1u);
  EXPECT_EQ(batch.rectangles.back().rectangle, (Rect{110, 208, 114, 212}));
  ASSERT_EQ(batch.vias.size(), 1u);
  EXPECT_EQ(batch.vias.front().header_index, 0u);
  EXPECT_EQ(batch.vias.front().origin, (Point{120, 210}));
}

TEST_F(GeometryOccurrenceServiceTest, AggregatesVisibleInstancesAndPreservesPlacedPortGeometry)
{
  // One call aggregates all dbITerm occurrences below each requested dbInst.
  // The first instance uses a quarter turn; the second verifies that the
  // batch preserves a different placement transform while retaining a
  // separate header for each LEF PORT / dbMPin.
  const std::vector<DesignInstanceId> instances{instance, rotated_instance};
  const auto batch = service->fetchInstancePins(instances);

  ASSERT_EQ(batch.headers.size(), 4u);
  EXPECT_EQ(batch.headers[0].instance, instance.packed());
  EXPECT_EQ(batch.headers[0].instance_term, iterm.packed());
  EXPECT_EQ(batch.headers[0].master_pin, first_pin.packed());
  EXPECT_EQ(batch.headers[1].master_pin, second_pin.packed());

  const auto& rotated_first = batch.headers[2];
  EXPECT_EQ(rotated_first.kind, GeometryOccurrenceKind::kPlacedInstanceMasterPin);
  EXPECT_EQ(rotated_first.instance, rotated_instance.packed());
  EXPECT_EQ(rotated_first.instance_term, rotated_iterm.packed());
  EXPECT_EQ(rotated_first.master_pin, first_pin.packed());
  EXPECT_TRUE(rotated_first.has_bounds);
  EXPECT_EQ(rotated_first.bounds, (Rect{307, 417, 319, 439}));

  const auto& rotated_second = batch.headers[3];
  EXPECT_EQ(rotated_second.instance, rotated_instance.packed());
  EXPECT_EQ(rotated_second.master_pin, second_pin.packed());
  EXPECT_TRUE(rotated_second.has_bounds);
  EXPECT_EQ(rotated_second.bounds, (Rect{308, 426, 312, 430}));

  ASSERT_EQ(batch.rectangles.size(), 10u);
  EXPECT_EQ(batch.rectangles[5].header_index, 2u);
  EXPECT_EQ(batch.rectangles[5].rectangle, (Rect{315, 434, 319, 438}));
  ASSERT_EQ(batch.polygons.size(), 2u);
  EXPECT_EQ(batch.polygons[1].header_index, 2u);
  ASSERT_EQ(batch.vias.size(), 2u);
  EXPECT_EQ(batch.vias[1].header_index, 2u);
  EXPECT_EQ(batch.vias[1].origin, (Point{310, 420}));
}

}  // namespace
}  // namespace eccdb
