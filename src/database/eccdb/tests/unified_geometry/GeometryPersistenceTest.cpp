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
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "binary/BinaryDatabaseExporter.h"
#include "binary/BinaryDatabaseImporter.h"
#include "def/DefDesignExporter.h"
#include "def/DefDesignImporter.h"
#include "design/DesignStore.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

class TemporaryFile
{
 public:
  TemporaryFile(std::string_view stem, std::string_view extension, std::string_view contents = {})
  {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    _path = std::filesystem::temp_directory_path()
            / ("eccdb-unified-geometry-" + std::string(stem) + '-' + std::to_string(nonce) + std::string(extension));
    if (!contents.empty()) {
      std::ofstream output(_path, std::ios::binary);
      output << contents;
      if (!output) {
        throw std::runtime_error("failed to create geometry persistence fixture");
      }
    }
  }

  ~TemporaryFile()
  {
    std::error_code error;
    std::filesystem::remove(_path, error);
  }

  TemporaryFile(const TemporaryFile&) = delete;
  TemporaryFile& operator=(const TemporaryFile&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return _path; }

 private:
  std::filesystem::path _path;
};

constexpr std::string_view kLef = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; SPACING 0.10 ; END M1
LAYER V1 TYPE CUT ; SPACING 0.10 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.20 ; WIDTH 0.10 ; SPACING 0.10 ; END M2
VIA VIA12 DEFAULT
  LAYER M1 ; RECT -0.08 -0.08 0.08 0.08 ;
  LAYER V1 ; RECT -0.04 -0.04 0.04 0.04 ;
  LAYER M2 ; RECT -0.08 -0.08 0.08 0.08 ;
END VIA12
SITE CORE CLASS CORE ; SYMMETRY X Y ; SIZE 0.20 BY 0.40 ; END CORE
MACRO INVX1
  CLASS CORE ; ORIGIN 0 0 ; SIZE 0.20 BY 0.40 ; SYMMETRY X Y ; SITE CORE ;
  PIN A DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.00 0.10 0.05 0.20 ; END END A
  PIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.15 0.10 0.20 0.20 ; END END Y
END INVX1
END LIBRARY
)LEF";

constexpr std::string_view kDef = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN geometry_edit_top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 2000 2000 ) ;
COMPONENTS 1 ;
- u1 INVX1 + PLACED ( 200 200 ) N ;
END COMPONENTS
PINS 1 ;
- IN + NET sig + DIRECTION INPUT + USE SIGNAL
  + LAYER M1 ( -25 -25 ) ( 25 25 ) + FIXED ( 0 200 ) N ;
END PINS
SPECIALNETS 1 ;
- VDD ( u1 Y ) + USE POWER
  + ROUTED M2 40 + SHAPE STRIPE ( 200 200 ) ( 200 600 ) VIA12 ;
END SPECIALNETS
NETS 1 ;
- sig ( PIN IN ) ( u1 A ) + USE SIGNAL
  + ROUTED M1 ( 0 200 ) ( 200 200 ) ;
END NETS
END DESIGN
)DEF";

struct ImportedDesign
{
  TemporaryFile lef{"geometry-edit", ".lef", kLef};
  TemporaryFile def{"geometry-edit", ".def", kDef};
  TechStore technology;
  LibraryStore library{technology.techRegistry()};
  DesignStore design{technology.techRegistry(), library.libraryRegistry()};

  ImportedDesign()
  {
    LefTechImporter(technology).import(lef.path());
    LefLibraryImporter(technology, library).import(lef.path());
    DefDesignImporter importer(design);
    importer.import(def.path());
    if (!importer.diagnostics().empty()) {
      throw std::runtime_error("unexpected diagnostic while importing geometry persistence fixture");
    }
  }
};

[[nodiscard]] TechRoutingLayerId routingLayer(const TechStore& technology, std::string_view name)
{
  const auto layer = technology.findLayer(name);
  return layer ? TechRoutingLayerId{layer.entity()} : TechRoutingLayerId{};
}

[[nodiscard]] GeometryShapeId firstSegment(const DesignStore& design, DesignNetId net)
{
  const auto& routing = design.routingStorage();
  for (const auto wire : routing.wireIds(net)) {
    for (const auto id : routing.wire(wire).shapes) {
      if (std::holds_alternative<DesignRouteSegment>(routing.shape(id).geometry)) {
        return id;
      }
    }
  }
  return {};
}

[[nodiscard]] GeometryShapeId shapeWithWidth(const DesignStore& design, DesignNetId net, int32_t width)
{
  const auto& routing = design.routingStorage();
  for (const auto wire : routing.wireIds(net)) {
    for (const auto id : routing.wire(wire).shapes) {
      const auto* attributes = routing.shapeAttributes(id);
      if (attributes != nullptr && attributes->width == width) {
        return id;
      }
    }
  }
  return {};
}

void expectRectangle(const DesignStore& design, GeometryShapeId id, DesignNetId owner, Rect expected)
{
  const auto view = design.geometryStorage().shape(id);
  ASSERT_TRUE(view.valid());
  ASSERT_NE(view.metadata, nullptr);
  EXPECT_EQ(view.metadata->kind, GeometryShapeKind::kRectangle);
  EXPECT_EQ(view.metadata->owner.domain, GeometryOwnerDomain::kDesignNet);
  EXPECT_EQ(view.metadata->owner.value, owner.packed());
  const auto* rectangle = std::get_if<const DesignGeometryRectangle*>(&view.payload);
  ASSERT_NE(rectangle, nullptr);
  EXPECT_EQ((*rectangle)->rectangle, expected);
  EXPECT_EQ((*rectangle)->mask, 2u);
  EXPECT_EQ((*rectangle)->shape, "STRIPE");
}

void expectSegmentCenterline(const DesignStore& design, GeometryShapeId id, Point begin, Point end)
{
  const auto& shape = design.routingStorage().shape(id);
  const auto* segment = std::get_if<DesignRouteSegment>(&shape.geometry);
  ASSERT_NE(segment, nullptr);
  EXPECT_EQ(segment->begin.position, begin);
  EXPECT_EQ(segment->end.position, end);
}

void populateAllOwnerGeometry(ImportedDesign& fixture)
{
  auto& design = fixture.design;
  auto& netlist = design.netlistStorage();
  const auto m1 = routingLayer(fixture.technology, "M1");
  const auto signal = netlist.findRegularNet("sig");
  const auto pin = netlist.findIoPin("IN");
  const auto instance = netlist.findInstance("u1");
  if (!m1 || !signal || !pin || !instance) {
    throw std::runtime_error("owner geometry fixture did not resolve imported objects");
  }

  const auto local_via = design.routingStorage().createVia(
      DesignViaInput{.name = "OWNER_VIA",
                     .rectangles = {{.layer = TechLayerId{m1.entity()}, .rectangle = {-4, -4, 4, 4}}},
                     .polygons = {{.layer = TechLayerId{m1.entity()}, .points = {{-3, -3}, {3, -3}, {3, 3}, {-3, 3}}}}});

  const auto block_pin = netlist.blockPins(pin).front();
  const auto& existing_block_pin = netlist.blockPin(block_pin);
  netlist.updateBlockPin(
      block_pin,
      DesignBlockPinInput{.flags = existing_block_pin.flags,
                          .placement_status = existing_block_pin.placement_status,
                          .origin = existing_block_pin.origin,
                          .orientation = existing_block_pin.orientation,
                          .rectangles = {{.layer = TechLayerId{m1.entity()},
                                          .rectangle = {-25, -25, 25, 25},
                                          .flags = DesignPinShapeFlag::kHasMask,
                                          .mask = 2}},
                          .polygons = {{.layer = TechLayerId{m1.entity()}, .points = {{30, 0}, {50, 0}, {50, 20}, {30, 20}}}},
                          .vias = {{.design_via = local_via, .origin = {10, 10}}}});

  static_cast<void>(design.constraintStorage().createRegion(
      DesignRegionInput{.name = "OWNER_FENCE", .type = DesignRegionType::kFence, .rectangles = {{0, 0, 500, 500}}}));
  auto updated_instance = netlist.instance(instance);
  updated_instance.flags &= ~DesignInstanceFlag::kHasRegion;
  updated_instance.flags |= DesignInstanceFlag::kHasRegionBounds;
  updated_instance.region = {};
  const std::array<Rect, 2> region_bounds{{{20, 30, 60, 80}, {100, 120, 180, 220}}};
  netlist.updateInstance(instance, std::move(updated_instance), region_bounds);

  static_cast<void>(design.constraintStorage().createBlockage(
      DesignBlockageInput{.kind = DesignBlockageKind::kRouting,
                          .flags = DesignBlockageFlag::kHasLayer,
                          .layer = m1,
                          .rectangles = {{600, 600, 650, 650}},
                          .polygons = {{{700, 700}, {750, 700}, {750, 750}, {700, 750}}}}));
  static_cast<void>(design.fillStorage().createFill(
      DesignFillInput{.layer = TechLayerId{m1.entity()}, .rectangles = {{800, 800, 850, 850}}}));
  design.floorplanStorage().setCoreArea(DesignCoreAreaInput{.boundary = {{10, 10}, {1900, 1900}}});
  design.guideStorage().replaceGuides(signal, {{.box = {0, 300, 200, 400}, .layer = m1}});
  design.guideStorage().replaceTrackAssignments(signal, {{.layer = m1, .box = {0, 450, 200, 450}}});
}

enum class InstanceRegionEncoding
{
  kInlineBounds,
  kNamedFenceRegion,
};

void expectAllOwnerGeometry(const DesignStore& design, bool expect_runtime_context, InstanceRegionEncoding region_encoding)
{
  const auto& netlist = design.netlistStorage();
  const auto local_via = design.routingStorage().findVia("OWNER_VIA");
  ASSERT_TRUE(local_via);
  const auto& via = design.routingStorage().via(local_via);
  ASSERT_EQ(via.rectangles.size(), 1u);
  ASSERT_EQ(via.polygons.size(), 1u);
  const auto& geometry = design.geometryRegistry();
  EXPECT_EQ(geometry.get<const DesignViaRectangle>(via.rectangles.front()).rectangle, (Rect{-4, -4, 4, 4}));
  EXPECT_EQ(geometry.get<const DesignViaPolygon>(via.polygons.front()).points,
            (std::vector<Point>{{-3, -3}, {3, -3}, {3, 3}, {-3, 3}}));

  const auto pin = netlist.findIoPin("IN");
  ASSERT_TRUE(pin);
  const auto block_pin = netlist.blockPins(pin).front();
  const auto& port = netlist.blockPin(block_pin);
  ASSERT_EQ(port.rectangles.size(), 1u);
  ASSERT_EQ(port.polygons.size(), 1u);
  ASSERT_EQ(port.vias.size(), 1u);
  EXPECT_EQ(netlist.blockPinRectangle(port.rectangles.front()).rectangle, (Rect{-25, -25, 25, 25}));
  EXPECT_EQ(netlist.blockPinPolygon(port.polygons.front()).points,
            (std::vector<Point>{{30, 0}, {50, 0}, {50, 20}, {30, 20}}));
  EXPECT_EQ(netlist.blockPinVia(port.vias.front()).design_via, local_via);
  EXPECT_EQ(netlist.blockPinVia(port.vias.front()).origin, (Point{10, 10}));

  const auto region = design.constraintStorage().findRegion("OWNER_FENCE");
  ASSERT_TRUE(region);
  const auto& region_value = design.constraintStorage().region(region);
  ASSERT_EQ(region_value.rectangles.size(), 1u);
  EXPECT_EQ(design.constraintStorage().regionRectangle(region_value.rectangles.front()), (Rect{0, 0, 500, 500}));

  const auto instance = netlist.findInstance("u1");
  ASSERT_TRUE(instance);
  const auto& instance_value = netlist.instance(instance);
  if (region_encoding == InstanceRegionEncoding::kInlineBounds) {
    EXPECT_NE(instance_value.flags & DesignInstanceFlag::kHasRegionBounds, 0u);
    EXPECT_EQ(instance_value.flags & DesignInstanceFlag::kHasRegion, 0u);
    ASSERT_EQ(instance_value.region_bounds.size(), 2u);
    EXPECT_EQ(geometry.get<const GeometryRectangle>(instance_value.region_bounds[0]).rectangle, (Rect{20, 30, 60, 80}));
    EXPECT_EQ(geometry.get<const GeometryRectangle>(instance_value.region_bounds[1]).rectangle, (Rect{100, 120, 180, 220}));
  } else {
    EXPECT_EQ(instance_value.flags & DesignInstanceFlag::kHasRegionBounds, 0u);
    EXPECT_NE(instance_value.flags & DesignInstanceFlag::kHasRegion, 0u);
    ASSERT_TRUE(instance_value.region);
    const auto& normalized_region = design.constraintStorage().region(instance_value.region);
    EXPECT_EQ(normalized_region.type, DesignRegionType::kFence);
    ASSERT_EQ(normalized_region.rectangles.size(), 2u);
    EXPECT_EQ(design.constraintStorage().regionRectangle(normalized_region.rectangles[0]), (Rect{20, 30, 60, 80}));
    EXPECT_EQ(design.constraintStorage().regionRectangle(normalized_region.rectangles[1]), (Rect{100, 120, 180, 220}));
  }

  ASSERT_EQ(design.constraintStorage().blockageCount(), 1u);
  const auto& blockage = design.constraintStorage().blockage(design.constraintStorage().blockages().front());
  ASSERT_EQ(blockage.rectangles.size(), 1u);
  ASSERT_EQ(blockage.polygons.size(), 1u);
  EXPECT_EQ(design.constraintStorage().blockageRectangle(blockage.rectangles.front()), (Rect{600, 600, 650, 650}));
  const auto blockage_polygon = design.constraintStorage().blockagePolygon(blockage.polygons.front());
  EXPECT_EQ((std::vector<Point>{blockage_polygon.begin(), blockage_polygon.end()}),
            (std::vector<Point>{{700, 700}, {750, 700}, {750, 750}, {700, 750}}));

  ASSERT_EQ(design.fillStorage().fillCount(), 1u);
  const auto& fill = design.fillStorage().fill(design.fillStorage().fills().front());
  ASSERT_EQ(fill.rectangles.size(), 1u);
  EXPECT_EQ(design.fillStorage().rectangle(fill.rectangles.front()), (Rect{800, 800, 850, 850}));
  const auto die_boundary = design.globalStorage().dieBoundary();
  EXPECT_EQ((std::vector<Point>{die_boundary.begin(), die_boundary.end()}),
            (std::vector<Point>{{0, 0}, {2000, 2000}}));

  if (!expect_runtime_context) {
    return;
  }
  const auto core_boundary = design.floorplanStorage().coreBoundary();
  EXPECT_EQ((std::vector<Point>{core_boundary.begin(), core_boundary.end()}),
            (std::vector<Point>{{10, 10}, {1900, 1900}}));
  const auto signal = netlist.findRegularNet("sig");
  ASSERT_TRUE(signal);
  const auto guides = design.guideStorage().guides(signal);
  ASSERT_EQ(guides.size(), 1u);
  EXPECT_EQ(design.guideStorage().guide(guides.front()).box, (Rect{0, 300, 200, 400}));
  const auto assignments = design.guideStorage().trackAssignments(signal);
  ASSERT_EQ(assignments.size(), 1u);
  EXPECT_EQ(design.guideStorage().trackAssignment(assignments.front()).box, (Rect{0, 450, 200, 450}));
}

struct EditedGeometry
{
  GeometryShapeId moved_route;
  GeometryShapeId special_rectangle;
  GeometryShapeId removed_polygon;
  GeometryShapeId special_width_shape;
};

[[nodiscard]] EditedGeometry editImportedGeometry(ImportedDesign& fixture)
{
  const auto signal = fixture.design.netlistStorage().findRegularNet("sig");
  const auto special = fixture.design.netlistStorage().findSpecialNet("VDD");
  const auto m1 = routingLayer(fixture.technology, "M1");
  if (!signal || !special || !m1) {
    throw std::runtime_error("geometry persistence fixture did not resolve imported objects");
  }

  const auto moved_route = firstSegment(fixture.design, signal);
  const auto special_width_shape = shapeWithWidth(fixture.design, special, 40);
  if (!moved_route || !special_width_shape) {
    throw std::runtime_error("geometry persistence fixture did not import routed Shapes");
  }

  fixture.design.geometryStorage().moveShape(moved_route, 25, -10);

  const auto special_rectangle = fixture.design.geometryStorage().appendNetShape(
      special, DesignNetRectangle{.layer = m1,
                                   .rectangle = {10, 20, 30, 40},
                                   .route_status = DesignWireStatus::kRouted,
                                   .flags = DesignNetGeometryFlag::kHasMask | DesignNetGeometryFlag::kHasShape,
                                   .mask = 1,
                                   .shape = "BLOCK"});
  fixture.design.geometryStorage().replaceNetShape(
      special_rectangle, DesignNetRectangle{.layer = m1,
                                             .rectangle = {50, 60, 90, 100},
                                             .route_status = DesignWireStatus::kRouted,
                                             .flags = DesignNetGeometryFlag::kHasMask | DesignNetGeometryFlag::kHasShape,
                                             .mask = 2,
                                             .shape = "STRIPE"});

  const auto removed_polygon = fixture.design.geometryStorage().appendNetShape(
      special, DesignNetPolygon{.layer = m1,
                                 .points = {{300, 300}, {340, 300}, {340, 340}, {300, 340}},
                                 .route_status = DesignWireStatus::kRouted});
  if (!fixture.design.geometryStorage().destroy(removed_polygon)) {
    throw std::runtime_error("failed to remove editable SPECIALNET polygon");
  }
  return {.moved_route = moved_route,
          .special_rectangle = special_rectangle,
          .removed_polygon = removed_polygon,
          .special_width_shape = special_width_shape};
}

TEST(GeometryPersistenceTest, BinarySnapshotPreservesEditedCanonicalEntitiesAndRelations)
{
  ImportedDesign fixture;
  const auto edits = editImportedGeometry(fixture);
  const auto signal = fixture.design.netlistStorage().findRegularNet("sig");
  const auto special = fixture.design.netlistStorage().findSpecialNet("VDD");
  // M1 has DEF/LEF default width 100, so this is the metal bbox rather than
  // the moved segment centerline.
  const Rect moved_bounds{-25, 140, 275, 240};

  ASSERT_TRUE(signal);
  ASSERT_TRUE(special);
  ASSERT_FALSE(fixture.design.geometryStorage().contains(edits.removed_polygon));
  EXPECT_EQ(fixture.design.geometryStorage().bounds(edits.moved_route), moved_bounds);
  expectSegmentCenterline(fixture.design, edits.moved_route, {25, 190}, {225, 190});
  expectRectangle(fixture.design, edits.special_rectangle, special, Rect{50, 60, 90, 100});

  const TemporaryFile archive{"geometry-snapshot", ".edb"};
  BinaryDatabaseExporter::saveDesign(archive.path(), fixture.design);
  auto restored = BinaryDatabaseImporter::loadDesign(archive.path(), fixture.technology, fixture.library);

  const auto restored_signal = restored->netlistStorage().findRegularNet("sig");
  const auto restored_special = restored->netlistStorage().findSpecialNet("VDD");
  ASSERT_TRUE(restored_signal);
  ASSERT_TRUE(restored_special);
  EXPECT_EQ(restored_signal, signal);
  EXPECT_EQ(restored_special, special);
  EXPECT_TRUE(restored->geometryStorage().contains(edits.moved_route));
  EXPECT_FALSE(restored->geometryStorage().contains(edits.removed_polygon));
  EXPECT_EQ(restored->geometryStorage().bounds(edits.moved_route), moved_bounds);
  expectSegmentCenterline(*restored, edits.moved_route, {25, 190}, {225, 190});
  const auto restored_route_wire = restored->routingStorage().shape(edits.moved_route).wire;
  EXPECT_EQ(restored_route_wire, fixture.design.routingStorage().shape(edits.moved_route).wire);
  const auto& restored_route_ids = restored->routingStorage().wire(restored_route_wire).shapes;
  EXPECT_NE(std::find(restored_route_ids.begin(), restored_route_ids.end(), edits.moved_route), restored_route_ids.end());
  const auto* width_attributes = restored->routingStorage().shapeAttributes(edits.special_width_shape);
  ASSERT_NE(width_attributes, nullptr);
  EXPECT_EQ(width_attributes->width, 40);

  const auto* relation = restored->routingStorage().netGeometry(restored_special);
  ASSERT_NE(relation, nullptr);
  ASSERT_EQ(relation->shapes.size(), 1u);
  EXPECT_EQ(relation->shapes.front(), edits.special_rectangle);
  expectRectangle(*restored, edits.special_rectangle, restored_special, Rect{50, 60, 90, 100});
  EXPECT_NO_THROW(restored->routingStorage().validateRestoredRoutingState());
}

TEST(GeometryPersistenceTest, BinarySnapshotPreservesAllOwnerGeometryPayloads)
{
  ImportedDesign fixture;
  populateAllOwnerGeometry(fixture);
  expectAllOwnerGeometry(fixture.design, true, InstanceRegionEncoding::kInlineBounds);

  const TemporaryFile archive{"all-owner-geometry", ".edb"};
  BinaryDatabaseExporter::saveDesign(archive.path(), fixture.design);
  auto restored = BinaryDatabaseImporter::loadDesign(archive.path(), fixture.technology, fixture.library);

  expectAllOwnerGeometry(*restored, true, InstanceRegionEncoding::kInlineBounds);
  EXPECT_NO_THROW(restored->geometryStorage().validateRestoredState());
}

TEST(GeometryPersistenceTest, DefRoundTripRebuildsEditedCanonicalGeometryAndRouteWidth)
{
  ImportedDesign fixture;
  static_cast<void>(editImportedGeometry(fixture));
  const TemporaryFile exported{"geometry-roundtrip", ".def", DefDesignExporter(fixture.design).exportText()};

  DesignStore reimported{fixture.technology.techRegistry(), fixture.library.libraryRegistry()};
  DefDesignImporter importer(reimported);
  importer.import(exported.path());
  ASSERT_TRUE(importer.diagnostics().empty());

  const auto signal = reimported.netlistStorage().findRegularNet("sig");
  const auto special = reimported.netlistStorage().findSpecialNet("VDD");
  ASSERT_TRUE(signal);
  ASSERT_TRUE(special);

  const auto segment = firstSegment(reimported, signal);
  ASSERT_TRUE(segment);
  EXPECT_EQ(reimported.geometryStorage().bounds(segment), (Rect{-25, 140, 275, 240}));
  expectSegmentCenterline(reimported, segment, {25, 190}, {225, 190});

  const auto width_shape = shapeWithWidth(reimported, special, 40);
  ASSERT_TRUE(width_shape);
  ASSERT_NE(reimported.routingStorage().shapeAttributes(width_shape), nullptr);
  EXPECT_EQ(reimported.routingStorage().shapeAttributes(width_shape)->width, 40);

  const auto* relation = reimported.routingStorage().netGeometry(special);
  ASSERT_NE(relation, nullptr);
  ASSERT_EQ(relation->shapes.size(), 1u);
  const auto rectangle = relation->shapes.front();
  EXPECT_TRUE(reimported.geometryStorage().contains(rectangle));
  expectRectangle(reimported, rectangle, special, Rect{50, 60, 90, 100});
}

TEST(GeometryPersistenceTest, DefRoundTripRebuildsAllDefOwnerGeometryPayloads)
{
  ImportedDesign fixture;
  populateAllOwnerGeometry(fixture);
  const TemporaryFile exported{"all-owner-geometry", ".def", DefDesignExporter(fixture.design).exportText()};

  DesignStore reimported{fixture.technology.techRegistry(), fixture.library.libraryRegistry()};
  DefDesignImporter importer(reimported);
  importer.import(exported.path());
  ASSERT_TRUE(importer.diagnostics().empty());

  expectAllOwnerGeometry(reimported, false, InstanceRegionEncoding::kNamedFenceRegion);
}

}  // namespace
}  // namespace eccdb
