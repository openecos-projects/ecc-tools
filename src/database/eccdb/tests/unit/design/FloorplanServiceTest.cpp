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

#include <array>
#include <vector>

#include "DesignTestFixture.h"
#include "common/GeometryBounds.h"
#include "context/DatabaseContext.h"
#include "floorplan/FloorplanService.h"
#include "geometry/GeometryEditService.h"

namespace eccdb {
namespace {

class ChangeRecorder final : public DatabaseObserver
{
 public:
  void afterChange(const DatabaseChange& change) noexcept override
  {
    domains.push_back(change.domains);
    geometry_replacements.emplace_back(change.geometry_replacements.begin(), change.geometry_replacements.end());
  }

  std::vector<DatabaseChangeDomain> domains;
  std::vector<std::vector<GeometryOwner>> geometry_replacements;
};

TEST_F(DesignStorageTest, FloorplanServiceUpdatesDieAndCoreAreasAsOneGeometryChangeEach)
{
  DatabaseContext context{design};
  ChangeRecorder changes;
  context.addObserver(changes);
  FloorplanService service{context};

  service.setDieArea(DesignDieAreaInput{.boundary = {{0, 0}, {1000, 0}, {1000, 800}, {0, 800}}});
  EXPECT_EQ(design.globalStorage().dieBounds(), (Rect{0, 0, 1000, 800}));
  ASSERT_EQ(changes.domains.size(), 1u);
  EXPECT_TRUE(includes(changes.domains.back(), DatabaseChangeDomain::kFloorplan));
  EXPECT_TRUE(includes(changes.domains.back(), DatabaseChangeDomain::kGeometry));

  service.setCoreArea(DesignCoreAreaInput{.boundary = {{100, 100}, {900, 100}, {900, 700}, {100, 700}}});
  EXPECT_EQ(design.floorplanStorage().coreBounds(), (Rect{100, 100, 900, 700}));
  ASSERT_EQ(changes.domains.size(), 2u);
  EXPECT_TRUE(includes(changes.domains.back(), DatabaseChangeDomain::kFloorplan));
  EXPECT_TRUE(includes(changes.domains.back(), DatabaseChangeDomain::kGeometry));
}

TEST_F(DesignStorageTest, FloorplanServiceReplacesRowsWithoutDestroyingSameNameIds)
{
  DatabaseContext context{design};
  ChangeRecorder changes;
  context.addObserver(changes);
  FloorplanService service{context};

  const auto old = service.createRow(DesignRow{.name = "ROW0", .site = site, .origin = {0, 0}});
  const auto old_value = design.floorplanStorage().row(old);

  EXPECT_THROW((void) service.replaceRows({DesignRow{.name = "DUP", .site = site}, DesignRow{.name = "DUP", .site = site}}),
               std::invalid_argument);
  ASSERT_EQ(design.floorplanStorage().rows(), (std::vector<DesignRowId>{old}));
  EXPECT_EQ(design.floorplanStorage().row(old).origin, old_value.origin);

  const auto ids = service.replaceRows(
      {DesignRow{.name = "ROW0", .site = site, .origin = {100, 0}}, DesignRow{.name = "ROW1", .site = site, .origin = {200, 0}}});
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], old);
  EXPECT_EQ(design.floorplanStorage().row(old).origin, (Point{100, 0}));
  EXPECT_TRUE(design.floorplanStorage().contains(ids[1]));
  EXPECT_EQ(design.floorplanStorage().rowCount(), 2u);
  ASSERT_FALSE(changes.domains.empty());
  EXPECT_TRUE(includes(changes.domains.back(), DatabaseChangeDomain::kFloorplan));
}

TEST_F(DesignStorageTest, FloorplanServiceStagesTrackAndGCellReplacementBeforeRemovingOldCollections)
{
  DatabaseContext context{design};
  FloorplanService service{context};
  const auto old_track = service.createTrackGrid(
      DesignTrackGrid{.axis = DesignAxis::kX, .start = 0, .track_count = 2, .step = 20, .layers = {routing_layer}});
  const auto old_gcell = service.createGCellGrid(DesignGCellGrid{.axis = DesignAxis::kY, .start = 0, .line_count = 2, .step = 40});

  EXPECT_THROW((void) service.replaceTrackGrids({DesignTrackGrid{.axis = DesignAxis::kX, .track_count = 0, .step = 20}}),
               std::invalid_argument);
  EXPECT_TRUE(design.floorplanStorage().contains(old_track));
  EXPECT_THROW((void) service.replaceGCellGrids({DesignGCellGrid{.axis = DesignAxis::kY, .line_count = 0, .step = 40}}),
               std::invalid_argument);
  EXPECT_TRUE(design.floorplanStorage().contains(old_gcell));

  const auto tracks = service.replaceTrackGrids(
      {DesignTrackGrid{.axis = DesignAxis::kY, .start = 10, .track_count = 3, .step = 30, .layers = {routing_layer}}});
  const auto gcells = service.replaceGCellGrids({DesignGCellGrid{.axis = DesignAxis::kX, .start = 10, .line_count = 3, .step = 50}});
  ASSERT_EQ(tracks.size(), 1u);
  ASSERT_EQ(gcells.size(), 1u);
  EXPECT_FALSE(design.floorplanStorage().contains(old_track));
  EXPECT_FALSE(design.floorplanStorage().contains(old_gcell));
  EXPECT_TRUE(design.floorplanStorage().contains(tracks.front()));
  EXPECT_TRUE(design.floorplanStorage().contains(gcells.front()));
}

TEST_F(DesignStorageTest, FloorplanServicePlacementPreservesBlockPinPayloadAndInvalidatesPinAccess)
{
  DatabaseContext context{design};
  FloorplanService service{context};
  auto& netlist = design.netlistStorage();
  const auto term = netlist.createBlockTerm(
      DesignBlockTerm{.name = "IN"},
      {DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                           .rectangles = {{.layer = layer,
                                           .rectangle = {1, 2, 11, 12},
                                           .flags = DesignPinShapeFlag::kHasMask,
                                           .mask = 2}}}});
  const auto pin = netlist.blockPins(term).front();
  design.pinAccessStorage().setBlockPinCandidateGroup(
      pin, 7u, {DesignPinAccessPoint{.point = {4, 5}, .layer = routing_layer}});
  const auto epoch = design.geometryStorage().geometryEpoch();

  service.placeBlockPin(pin, {100, 200}, DesignOrientation::kFS, DesignPlacementStatus::kFixed);

  const auto& placed = netlist.blockPin(pin);
  EXPECT_EQ(placed.origin, (Point{100, 200}));
  EXPECT_EQ(placed.orientation, DesignOrientation::kFS);
  EXPECT_EQ(placed.placement_status, DesignPlacementStatus::kFixed);
  EXPECT_NE(placed.flags & DesignIoPinPortFlag::kHasPlacement, 0u);
  ASSERT_EQ(placed.rectangles.size(), 1u);
  const auto& rectangle = netlist.blockPinRectangle(placed.rectangles.front());
  EXPECT_EQ(rectangle.layer, layer);
  EXPECT_EQ(rectangle.rectangle, (Rect{1, 2, 11, 12}));
  EXPECT_EQ(rectangle.mask, 2u);
  EXPECT_TRUE(design.pinAccessStorage().blockPinCandidateGroups(pin).empty());
  EXPECT_GT(design.geometryStorage().geometryEpoch(), epoch);
  const auto* bounds = design.designRegistry().registry().try_get<const GeometryBounds>(pin.entity());
  ASSERT_NE(bounds, nullptr);
  EXPECT_TRUE(bounds->dirty);
}

TEST_F(DesignStorageTest, FloorplanServiceCreatesAndUpdatesPhysicalPortsWithoutChangingConnectedBlockTerm)
{
  DatabaseContext context{design};
  ChangeRecorder changes;
  context.addObserver(changes);
  FloorplanService service{context};
  auto& netlist = design.netlistStorage();
  const auto net = netlist.createNet(DesignNet{.name = "N"});
  const auto term = netlist.createBlockTerm(
      DesignBlockTerm{.name = "IO"},
      {DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                           .rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}}}}});
  netlist.connect(term, net);
  const auto original = netlist.blockPins(term).front();
  const auto revision = context.revision();
  auto& term_bounds = design.designRegistry().registry().get<GeometryBounds>(term.entity());
  term_bounds.set({0, 0, 10, 10}, BoundsSpace::kGlobal, design.geometryStorage().geometryEpoch());

  const auto port = service.createBlockPin(
      term,
      DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                          .rectangles = {{.layer = layer, .rectangle = {20, 0, 30, 10}},
                                         {.layer = layer, .rectangle = {32, 0, 42, 10}}},
                          .polygons = {{.layer = layer, .points = {{20, 20}, {30, 20}, {25, 30}}}},
                          .vias = {{.tech_via = tech_via, .origin = {25, 5}}}});

  EXPECT_TRUE(design.designRegistry().registry().get<const GeometryBounds>(term.entity()).dirty);
  EXPECT_EQ(context.revision(), revision + 1u);
  ASSERT_EQ(changes.domains.size(), 1u);
  EXPECT_TRUE(includes(changes.domains.back(), DatabaseChangeDomain::kFloorplan));
  EXPECT_TRUE(includes(changes.domains.back(), DatabaseChangeDomain::kGeometry));
  EXPECT_TRUE(includes(changes.domains.back(), DatabaseChangeDomain::kPinAccess));
  EXPECT_EQ(netlist.blockTerm(term).net, net);
  EXPECT_EQ((std::vector<DesignBlockTermId>{netlist.blockTerms(net).begin(), netlist.blockTerms(net).end()}),
            (std::vector<DesignBlockTermId>{term}));
  EXPECT_EQ((std::vector<DesignBlockPinId>{netlist.blockPins(term).begin(), netlist.blockPins(term).end()}),
            (std::vector<DesignBlockPinId>{original, port}));
  const auto& stored = netlist.blockPin(port);
  ASSERT_EQ(stored.rectangles.size(), 2u);
  ASSERT_EQ(stored.polygons.size(), 1u);
  ASSERT_EQ(stored.vias.size(), 1u);
  for (const auto shape : stored.rectangles) {
    EXPECT_EQ(design.geometryStorage().owner(shape), designBlockPinOwner(port));
  }
  EXPECT_EQ(design.geometryStorage().owner(stored.polygons.front()), designBlockPinOwner(port));
  EXPECT_EQ(design.geometryStorage().owner(stored.vias.front()), designBlockPinOwner(port));
  EXPECT_EQ(netlist.blockPinVia(stored.vias.front()).tech_via, tech_via);

  design.pinAccessStorage().setBlockPinCandidateGroup(
      port, 2u, {DesignPinAccessPoint{.point = {24, 5}, .layer = routing_layer}});
  term_bounds = {};
  term_bounds.set({0, 0, 42, 30}, BoundsSpace::kGlobal, design.geometryStorage().geometryEpoch());
  service.updateBlockPin(port,
                         DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                                             .rectangles = {{.layer = layer, .rectangle = {50, 0, 60, 10}}}});
  EXPECT_TRUE(netlist.contains(port));
  EXPECT_EQ(netlist.blockTerm(term).net, net);
  EXPECT_EQ((std::vector<DesignBlockPinId>{netlist.blockPins(term).begin(), netlist.blockPins(term).end()}),
            (std::vector<DesignBlockPinId>{original, port}));
  ASSERT_EQ(netlist.blockPin(port).rectangles.size(), 1u);
  EXPECT_EQ(netlist.blockPinRectangle(netlist.blockPin(port).rectangles.front()).rectangle, (Rect{50, 0, 60, 10}));
  EXPECT_TRUE(design.pinAccessStorage().blockPinCandidateGroups(port).empty());
  EXPECT_EQ(context.revision(), revision + 2u);
  ASSERT_EQ(changes.domains.size(), 2u);
  const auto* bounds = design.designRegistry().registry().try_get<const GeometryBounds>(port.entity());
  ASSERT_NE(bounds, nullptr);
  EXPECT_TRUE(bounds->dirty);
  EXPECT_TRUE(design.designRegistry().registry().get<const GeometryBounds>(term.entity()).dirty);

  term_bounds = {};
  term_bounds.set({0, 0, 60, 30}, BoundsSpace::kGlobal, design.geometryStorage().geometryEpoch());
  EXPECT_TRUE(service.destroyBlockPin(port));
  EXPECT_FALSE(netlist.contains(port));
  EXPECT_EQ((std::vector<DesignBlockPinId>{netlist.blockPins(term).begin(), netlist.blockPins(term).end()}),
            (std::vector<DesignBlockPinId>{original}));
  EXPECT_EQ(netlist.blockTerm(term).net, net);
  EXPECT_TRUE(design.designRegistry().registry().get<const GeometryBounds>(term.entity()).dirty);
  EXPECT_EQ(context.revision(), revision + 3u);
  ASSERT_EQ(changes.domains.size(), 3u);
  EXPECT_FALSE(service.destroyBlockPin(port));
  EXPECT_EQ(context.revision(), revision + 3u);
  ASSERT_EQ(changes.domains.size(), 3u);
}

TEST_F(DesignStorageTest, FloorplanServiceConvertsImplicitPortToExplicitPortsWithoutDisconnectingBlockTerm)
{
  DatabaseContext context{design};
  ChangeRecorder changes;
  context.addObserver(changes);
  FloorplanService service{context};
  auto& netlist = design.netlistStorage();
  const auto net = netlist.createNet(DesignNet{.name = "N"});
  const auto term = netlist.createBlockTerm(
      DesignBlockTerm{.name = "IN"},
      {DesignBlockPinInput{.rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}}}}});
  netlist.connect(term, net);
  const auto implicit = netlist.blockPins(term).front();
  const auto old_shape = netlist.blockPin(implicit).rectangles.front();
  const auto revision = context.revision();
  auto& term_bounds = design.designRegistry().registry().get<GeometryBounds>(term.entity());
  term_bounds.set({0, 0, 10, 10}, BoundsSpace::kGlobal, design.geometryStorage().geometryEpoch());

  const auto ports = service.replaceAllBlockPinPorts(
      term,
      {DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                           .rectangles = {{.layer = layer, .rectangle = {100, 0, 110, 10}}}},
       DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                           .rectangles = {{.layer = layer, .rectangle = {120, 0, 130, 10}}}}});

  ASSERT_EQ(ports.size(), 2u);
  EXPECT_FALSE(netlist.contains(implicit));
  EXPECT_FALSE(design.geometryStorage().contains(old_shape));
  EXPECT_EQ((std::vector<DesignBlockPinId>{netlist.blockPins(term).begin(), netlist.blockPins(term).end()}), ports);
  EXPECT_EQ(netlist.blockTerm(term).net, net);
  EXPECT_TRUE(design.designRegistry().registry().get<const GeometryBounds>(term.entity()).dirty);
  EXPECT_EQ((std::vector<DesignBlockTermId>{netlist.blockTerms(net).begin(), netlist.blockTerms(net).end()}),
            (std::vector<DesignBlockTermId>{term}));
  for (const auto port : ports) {
    EXPECT_NE(netlist.blockPin(port).flags & DesignIoPinPortFlag::kExplicit, 0u);
  }
  EXPECT_EQ(context.revision(), revision + 1u);
  ASSERT_EQ(changes.domains.size(), 1u);
  EXPECT_TRUE(includes(changes.domains.front(), DatabaseChangeDomain::kFloorplan));
  EXPECT_TRUE(includes(changes.domains.front(), DatabaseChangeDomain::kGeometry));
  EXPECT_TRUE(includes(changes.domains.front(), DatabaseChangeDomain::kPinAccess));
  ASSERT_EQ(changes.geometry_replacements.front(), (std::vector<GeometryOwner>{designBlockPinOwner(implicit)}));
}

TEST_F(DesignStorageTest, FloorplanServiceReplacesOnlySelectedPortsAndReleasesTheirDetachedGeometry)
{
  DatabaseContext context{design};
  FloorplanService service{context};
  GeometryEditService edit{context};
  auto& netlist = design.netlistStorage();
  const auto net = netlist.createNet(DesignNet{.name = "N"});
  const auto term = netlist.createBlockTerm(
      DesignBlockTerm{.name = "IO"},
      {DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                           .rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}}}},
       DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                           .rectangles = {{.layer = layer, .rectangle = {20, 0, 30, 10}},
                                          {.layer = layer, .rectangle = {32, 0, 42, 10}}}},
       DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                           .rectangles = {{.layer = layer, .rectangle = {50, 0, 60, 10}}}}});
  netlist.connect(term, net);
  const auto original_ports = netlist.blockPins(term);
  ASSERT_EQ(original_ports.size(), 3u);
  const auto first = original_ports[0];
  const auto replaced = original_ports[1];
  const auto last = original_ports[2];
  const auto first_shape = netlist.blockPin(first).rectangles.front();
  const auto detached_shape = netlist.blockPin(replaced).rectangles.front();
  design.pinAccessStorage().setBlockPinCandidateGroup(
      first, 1u, {DesignPinAccessPoint{.point = {5, 5}, .layer = routing_layer}});
  design.pinAccessStorage().setBlockPinCandidateGroup(
      replaced, 1u, {DesignPinAccessPoint{.point = {25, 5}, .layer = routing_layer}});
  ASSERT_TRUE(edit.removeShape(detached_shape.packed()));

  const auto new_ports = service.replaceSelectedBlockPinPorts(
      term, std::array{replaced},
      {DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                           .rectangles = {{.layer = layer, .rectangle = {70, 0, 80, 10}}}}});

  ASSERT_EQ(new_ports.size(), 1u);
  const auto replacement = new_ports.front();
  EXPECT_EQ((std::vector<DesignBlockPinId>{netlist.blockPins(term).begin(), netlist.blockPins(term).end()}),
            (std::vector<DesignBlockPinId>{first, replacement, last}));
  EXPECT_TRUE(netlist.contains(first));
  EXPECT_TRUE(netlist.contains(last));
  EXPECT_FALSE(netlist.contains(replaced));
  EXPECT_EQ(netlist.blockPin(first).rectangles.front(), first_shape);
  EXPECT_EQ(netlist.blockTerm(term).net, net);
  EXPECT_FALSE(design.pinAccessStorage().blockPinCandidateGroups(first).empty());
  EXPECT_FALSE(edit.restoreShape(detached_shape.packed()));
  EXPECT_FALSE(design.geometryRegistry().registry().valid(detached_shape.entity()));
  EXPECT_NO_THROW(design.geometryStorage().validateRestoredState());
}

TEST_F(DesignStorageTest, FloorplanServiceDoesNotNotifyOrRetirePortsWhenReplacementValidationFails)
{
  DatabaseContext context{design};
  ChangeRecorder changes;
  context.addObserver(changes);
  FloorplanService service{context};
  auto& netlist = design.netlistStorage();
  const auto net = netlist.createNet(DesignNet{.name = "N"});
  const auto term = netlist.createBlockTerm(
      DesignBlockTerm{.name = "IN"},
      {DesignBlockPinInput{.rectangles = {{.layer = layer, .rectangle = {0, 0, 10, 10}}}}});
  netlist.connect(term, net);
  const auto implicit = netlist.blockPins(term).front();
  const auto old_shape = netlist.blockPin(implicit).rectangles.front();
  const auto revision = context.revision();

  // Adding an explicit PORT beside an implicit physical pin is invalid. The
  // storage must reject the final relation before retiring the existing port.
  EXPECT_THROW((void) service.replaceSelectedBlockPinPorts(
                   term, {},
                   {DesignBlockPinInput{.flags = DesignIoPinPortFlag::kExplicit,
                                        .rectangles = {{.layer = layer, .rectangle = {20, 0, 30, 10}}}}}),
               std::invalid_argument);

  EXPECT_EQ(context.revision(), revision);
  EXPECT_TRUE(changes.domains.empty());
  EXPECT_TRUE(netlist.contains(implicit));
  EXPECT_EQ((std::vector<DesignBlockPinId>{netlist.blockPins(term).begin(), netlist.blockPins(term).end()}),
            (std::vector<DesignBlockPinId>{implicit}));
  EXPECT_TRUE(design.geometryStorage().contains(old_shape));
  EXPECT_EQ(netlist.blockTerm(term).net, net);
}

TEST_F(DesignStorageTest, FloorplanServiceUpdatesHaloAndInvalidatesInstanceBounds)
{
  DatabaseContext context{design};
  FloorplanService service{context};
  const auto instance = design.netlistStorage().createInstance(createInstance("u_halo", {10, 20}));
  const auto epoch = design.geometryStorage().geometryEpoch();

  service.setInstanceHalo(instance, DesignInstanceHalo{.left = 1, .bottom = 2, .right = 3, .top = 4}, true);
  const auto& with_halo = design.netlistStorage().instance(instance);
  EXPECT_NE(with_halo.flags & DesignInstanceFlag::kHasHalo, 0u);
  EXPECT_NE(with_halo.flags & DesignInstanceFlag::kHaloSoft, 0u);
  EXPECT_EQ(with_halo.halo.top, 4);
  EXPECT_GT(design.geometryStorage().geometryEpoch(), epoch);

  service.clearInstanceHalo(instance);
  const auto& cleared = design.netlistStorage().instance(instance);
  EXPECT_EQ(cleared.flags & (DesignInstanceFlag::kHasHalo | DesignInstanceFlag::kHaloSoft), 0u);
  EXPECT_EQ(cleared.halo.left, 0);
  const auto* bounds = design.designRegistry().registry().try_get<const GeometryBounds>(instance.entity());
  ASSERT_NE(bounds, nullptr);
  EXPECT_TRUE(bounds->dirty);
}

TEST_F(DesignStorageTest, TrackReplacementInvalidatesDerivedPinAccessAndAssignmentsOnlyAfterSuccess)
{
  DatabaseContext context(design);
  FloorplanService service(context);
  const auto track = service.createTrackGrid(
      DesignTrackGrid{.axis = DesignAxis::kX, .track_count = 3, .step = 20, .layers = {routing_layer}});
  const auto port = library.masterPortStorage().createMasterPort(input_term);
  const auto instance = design.netlistStorage().createInstance(createInstance("u1"));
  const auto term = design.netlistStorage().findInstanceTerm(instance, input_term);
  auto& access = design.pinAccessStorage();
  access.setMasterPortCandidateGroup(port, 1u, {{.point = {10, 20}, .layer = routing_layer}});
  access.setInstancePinAccessGroup(instance, 1u);
  access.setPreferredInstancePinAccess(term, {{.master_port = port, .group_index = 1u, .point_index = 0u}});
  const auto net = design.netlistStorage().createNet(DesignNet{.name = "N"});
  design.guideStorage().replaceTrackAssignments(net, {{.layer = routing_layer, .box = {0, 20, 100, 20}}});
  design.guideStorage().replaceGuides(net, {{.box = {0, 0, 100, 100}, .layer = routing_layer}});
  const auto assignment = design.guideStorage().trackAssignments(net).front();

  EXPECT_THROW((void) service.replaceTrackGrids({DesignTrackGrid{.track_count = 0}}), std::invalid_argument);
  EXPECT_TRUE(access.instancePinAccessGroup(instance).has_value());
  EXPECT_TRUE(design.guideStorage().hasTrackAssignments(net));
  EXPECT_TRUE(design.floorplanStorage().contains(track));

  const auto revision = context.revision();
  static_cast<void>(service.replaceTrackGrids(
      {DesignTrackGrid{.axis = DesignAxis::kX, .start = 5, .track_count = 3, .step = 20, .layers = {routing_layer}}}));
  EXPECT_EQ(context.revision(), revision + 1);
  EXPECT_EQ(access.catalog(), nullptr);
  EXPECT_FALSE(access.instancePinAccessGroup(instance).has_value());
  EXPECT_TRUE(access.preferredInstancePinAccess(term).empty());
  EXPECT_FALSE(design.guideStorage().hasTrackAssignments(net));
  EXPECT_FALSE(design.geometryStorage().contains(assignment));
  EXPECT_TRUE(design.guideStorage().hasGuides(net));
  EXPECT_TRUE(design.netlistStorage().contains(instance));
  EXPECT_NO_THROW(access.validateRestoredState());
}

}  // namespace
}  // namespace eccdb
