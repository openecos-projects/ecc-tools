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
#include <filesystem>
#include <fstream>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

#include "def/DefDesignExporter.h"
#include "def/DefDesignImporter.h"
#include "geometry/DomainGeometryComponents.h"
#include "library/LibraryStore.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {
namespace {

std::vector<Rect> instanceRegionBounds(const DesignStore& design, const DesignInstance& instance)
{
  std::vector<Rect> result;
  result.reserve(instance.region_bounds.size());
  const auto& geometry = design.geometryRegistry();
  for (const auto id : instance.region_bounds) {
    result.push_back(geometry.get<const GeometryRectangle>(id).rectangle);
  }
  return result;
}

class RejectingStreamBuffer : public std::streambuf
{
 protected:
  std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
  int_type overflow(int_type) override { return traits_type::eof(); }
};

class DefDesignRoundTripTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    const auto layer_entity = tech.registry().create();
    tech.registry().emplace<TechLayerInfo>(layer_entity, TechLayerInfo{.name = "M1", .mask_count = 3});
    tech.registry().emplace<TechRoutingLayer>(layer_entity, TechRoutingLayer{});

    const auto cut_entity = tech.registry().create();
    tech.registry().emplace<TechLayerInfo>(cut_entity, TechLayerInfo{.name = "V1"});
    tech.registry().emplace<TechCutLayer>(cut_entity, TechCutLayer{});

    const auto top_entity = tech.registry().create();
    tech.registry().emplace<TechLayerInfo>(top_entity, TechLayerInfo{.name = "M2"});
    tech.registry().emplace<TechRoutingLayer>(top_entity, TechRoutingLayer{});

    const auto via_rule_entity = tech.registry().create();
    tech.registry().emplace<TechViaRuleGenerate>(via_rule_entity, TechViaRuleGenerate{.name = "GEN12", .properties = {}});
    tech.registry().emplace<TechViaRuleGenerateBottomLayer>(via_rule_entity,
                                                            TechViaRuleGenerateBottomLayer{.layer = TechRoutingLayerId{layer_entity}});
    tech.registry().emplace<TechViaRuleGenerateCutLayer>(
        via_rule_entity, TechViaRuleGenerateCutLayer{.layer = TechCutLayerId{cut_entity}, .cut_rect = {}});
    tech.registry().emplace<TechViaRuleGenerateTopLayer>(via_rule_entity,
                                                         TechViaRuleGenerateTopLayer{.layer = TechRoutingLayerId{top_entity}});

    const auto via_entity = tech.registry().create();
    tech.registry().emplace<TechViaMaster>(via_entity, TechViaMaster{.name = "VIA12", .properties = {}});

    const auto ndr_entity = tech.registry().create();
    tech.registry().emplace<TechNonDefaultRule>(ndr_entity, TechNonDefaultRule{.name = "WIDE"});

    const auto site = library.siteStorage().createSite(
        LibrarySite{.name = "CORE", .width = 20, .height = 40, .site_class = LibrarySiteClass::kCore, .symmetry_r90 = true});
    const auto master = library.cellMasterStorage().createCellMaster(
        LibraryCellMaster{.name = "INVX1", .site = site, .width = 20, .height = 40, .terms = {}});
    static_cast<void>(library.masterTermStorage().createMasterTerm(
        master, LibraryMasterTerm{.name = "A", .direction = LibraryMasterTermDirection::kInput, .master = {}, .ports = {}}));
    static_cast<void>(library.masterTermStorage().createMasterTerm(
        master, LibraryMasterTerm{.name = "Y", .direction = LibraryMasterTermDirection::kOutput, .master = {}, .ports = {}}));
  }

  std::filesystem::path writeInput(std::string_view text, std::string_view name)
  {
    const auto path = std::filesystem::path(testing::TempDir()) / std::string(name);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    output.close();
    return path;
  }

  TechRegistry tech;
  LibraryStore library{tech};
};

constexpr std::string_view kComplexDef = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;

PROPERTYDEFINITIONS
  ROW ROW_KIND STRING ;
  ROW ROW_INDEX INTEGER ;
END PROPERTYDEFINITIONS

DIEAREA ( 0 0 ) ( 2000 2000 ) ;
ROW ROW0 CORE 0 0 N DO 10 BY 1 + PROPERTY ROW_KIND "logic" ROW_INDEX 2 ;
TRACKS X 0 DO 20 STEP 100 MASK 1 SAMEMASK LAYER M1 ;
GCELLGRID X 0 DO 4 STEP 500 ;

VIAS 1 ;
- LOCAL
  + RECT M1 ( -5 -5 ) ( 5 5 )
  ;
END VIAS

COMPONENTS 1 ;
- u1 INVX1
  + SOURCE NETLIST
  + WEIGHT 2
  + REGION FENCE
  + HALO SOFT 10 20 30 40
  + ROUTEHALO 50 M1 M1
  + PLACED ( 100 200 ) N
  ;
END COMPONENTS

PINS 1 ;
- IN
  + NET n1
  + DIRECTION INPUT
  + USE SIGNAL
  + PORT
    + LAYER M1 MASK 2 SPACING 4 ( -10 -10 ) ( 10 10 )
    + VIA LOCAL MASK 123 ( 0 0 )
    + FIXED ( 0 100 ) N
  + PORT
    + POLYGON M1 DESIGNRULEWIDTH 6 ( 20 20 ) ( 40 20 ) ( 40 40 ) ( 20 40 )
    + PLACED ( 100 100 ) S
  ;
END PINS

BLOCKAGES 1 ;
- LAYER M1 + SLOTS + EXCEPTPGNET + SPACING 5 + MASK 2
  RECT ( 300 300 ) ( 400 400 )
  POLYGON ( 500 500 ) ( 600 500 ) ( 600 600 ) ( 500 600 ) ;
END BLOCKAGES

REGIONS 1 ;
- FENCE ( 0 0 ) ( 1000 1000 ) + TYPE FENCE ;
END REGIONS

GROUPS 1 ;
- logic u* + REGION FENCE ;
END GROUPS

SPECIALNETS 1 ;
- VDD
  ( u1 Y )
  + USE POWER
  + ORIGINAL source_vdd
  + PATTERN TRUNK
  + FREQUENCY 60.5
  + STYLE 6
  + VOLTAGE 900
  + SPACING M1 12 RANGE 20 40
  + FIXED + SHAPE RING + MASK 2 + POLYGON M1
    ( 300 300 ) ( 400 300 ) ( 400 400 ) ( 300 400 )
  + SHIELD n1 + SHAPE BLOCKRING + RECT M1 ( 500 500 ) ( 600 600 )
  + COVER + MASK 123 + VIA LOCAL FN ( 700 700 ) ( 800 800 )
  + ROUTED M1 20 + SHAPE STRIPE + STYLE 4
    ( 0 200 ) ( 100 200 ) MASK 123 VIA12 N DO 2 BY 3 STEP 40 50
    NEW M1 30 ( 100 200 ) ( 200 200 )
  ;
END SPECIALNETS

NETS 1 ;
- n1
  ( PIN IN )
  ( u1 A )
  + USE SIGNAL
  + SOURCE NETLIST
  + WEIGHT 3
  + NONDEFAULTRULE WIDE
  + ORIGINAL source_n1
  + PATTERN STEINER
  + ESTCAP 1.25
  + FREQUENCY 2.5
  + XTALK 4
  + STYLE 7
  + ROUTED M1 STYLE 3
    ( 0 100 ) VIRTUAL ( 50 100 ) ( 100 100 5 ) MASK 123 LOCAL FN RECT ( -2 -2 2 2 )
    NEW M1 TAPER ( 100 100 ) ( 100 300 ) VIA12 S
  ;
END NETS

END DESIGN
)DEF";

constexpr std::string_view kLocalVia12 = R"DEF(VIAS 1 ;
- LOCAL12
  + RECT M1 ( -5 -5 ) ( 5 5 )
  + RECT V1 ( -2 -2 ) ( 2 2 )
  + RECT M2 ( -5 -5 ) ( 5 5 )
  ;
END VIAS
)DEF";

std::string singleRouteDef(std::string_view route, std::string_view via_section = {})
{
  std::string text = "VERSION 5.8 ;\n"
                     "DESIGN route_semantics ;\n"
                     "UNITS DISTANCE MICRONS 1000 ;\n"
                     "DIEAREA ( 0 0 ) ( 1000 1000 ) ;\n";
  text += via_section;
  text += "NETS 1 ;\n- signal + ROUTED ";
  text += route;
  text += " ;\nEND NETS\nEND DESIGN\n";
  return text;
}

void expectComplexRouting(const DesignStore& design)
{
  const auto& netlist = design.netlistStorage();
  const auto& routing = design.routingStorage();

  const auto regular_net = netlist.findNet("n1");
  ASSERT_TRUE(regular_net);
  ASSERT_FALSE(netlist.isSpecialNet(regular_net));
  const auto* regular_options = netlist.netOptions(regular_net);
  ASSERT_NE(regular_options, nullptr);
  EXPECT_EQ(regular_options->original, "source_n1");
  EXPECT_EQ(regular_options->pattern, DesignNetPattern::kSteiner);
  EXPECT_DOUBLE_EQ(regular_options->estimated_capacitance, 1.25);
  EXPECT_DOUBLE_EQ(regular_options->frequency, 2.5);
  EXPECT_EQ(regular_options->xtalk, 4);
  EXPECT_EQ(regular_options->style, 7);
  const auto regular_wires = routing.wires(regular_net);
  ASSERT_EQ(regular_wires.size(), 1u);
  std::vector<DesignRouteShapeId> regular_path0;
  std::vector<DesignRouteShapeId> regular_path1;
  for (const auto shape_id : routing.wire(regular_wires.front()).shapes) {
    const auto source_path = routing.shape(shape_id).source_path;
    (source_path == 0u ? regular_path0 : regular_path1).push_back(shape_id);
  }
  ASSERT_FALSE(regular_path0.empty());
  ASSERT_FALSE(regular_path1.empty());
  std::size_t regular_segments = 0;
  std::size_t regular_vias = 0;
  std::size_t regular_patches = 0;
  bool saw_virtual_point = false;
  bool saw_extended_point = false;
  for (const auto shape_id : regular_path0) {
    const auto& shape = routing.shape(shape_id);
    if (const auto* segment = std::get_if<DesignRouteSegment>(&shape.geometry); segment != nullptr) {
      ++regular_segments;
      saw_virtual_point = saw_virtual_point || (segment->begin.flags & DesignWirePointFlag::kVirtual) != 0u
                          || (segment->end.flags & DesignWirePointFlag::kVirtual) != 0u;
      saw_extended_point = saw_extended_point || ((segment->begin.flags & DesignWirePointFlag::kHasExtension) != 0u
                                                   && segment->begin.extension == 5)
                           || ((segment->end.flags & DesignWirePointFlag::kHasExtension) != 0u
                               && segment->end.extension == 5);
    } else if (const auto* via = std::get_if<DesignRouteVia>(&shape.geometry); via != nullptr) {
      ++regular_vias;
      EXPECT_EQ(via->via.orientation, DesignOrientation::kFN);
      EXPECT_EQ(via->via.top_mask, 1u);
      EXPECT_EQ(via->via.cut_mask, 2u);
      EXPECT_EQ(via->via.bottom_mask, 3u);
    } else if (std::holds_alternative<DesignRoutePatch>(shape.geometry)) {
      ++regular_patches;
      EXPECT_EQ(std::get<DesignRoutePatch>(shape.geometry).rectangle, (Rect{98, 98, 102, 102}));
    }
  }
  EXPECT_EQ(regular_segments, 2u);
  EXPECT_EQ(regular_vias, 1u);
  EXPECT_EQ(regular_patches, 1u);
  EXPECT_TRUE(saw_virtual_point);
  EXPECT_TRUE(saw_extended_point);
  EXPECT_TRUE(std::all_of(regular_path1.begin(), regular_path1.end(), [&](const auto shape_id) {
    return (routing.shape(shape_id).flags & DesignWirePathFlag::kTaper) != 0u;
  }));

  const auto special_net = netlist.findNet("VDD");
  ASSERT_TRUE(special_net);
  ASSERT_TRUE(netlist.isSpecialNet(special_net));
  const auto* special_options = netlist.netOptions(special_net);
  ASSERT_NE(special_options, nullptr);
  EXPECT_EQ(special_options->original, "source_vdd");
  EXPECT_EQ(special_options->pattern, DesignNetPattern::kTrunk);
  EXPECT_DOUBLE_EQ(special_options->frequency, 60.5);
  EXPECT_EQ(special_options->style, 6);
  EXPECT_EQ(special_options->voltage, 900);
  ASSERT_EQ(special_options->spacing_rules.size(), 1u);
  EXPECT_EQ(special_options->spacing_rules[0].spacing, 12);
  EXPECT_NE(special_options->spacing_rules[0].flags & DesignNetSpacingRuleFlag::kHasRange, 0u);
  EXPECT_EQ(special_options->spacing_rules[0].range_left, 20);
  EXPECT_EQ(special_options->spacing_rules[0].range_right, 40);
  const auto special_wires = routing.wires(special_net);
  ASSERT_EQ(special_wires.size(), 1u);
  std::vector<DesignRouteShapeId> special_path0;
  for (const auto shape_id : routing.wire(special_wires.front()).shapes) {
    if (routing.shape(shape_id).source_path == 0u) special_path0.push_back(shape_id);
  }
  ASSERT_FALSE(special_path0.empty());
  bool saw_special_attributes = false;
  bool saw_special_via = false;
  for (const auto shape_id : special_path0) {
    const auto& shape = routing.shape(shape_id);
    if (const auto* attributes = routing.shapeAttributes(shape_id); attributes != nullptr) {
      saw_special_attributes = true;
      EXPECT_EQ(attributes->width, 20);
      EXPECT_EQ(attributes->shape, "STRIPE");
      EXPECT_EQ(attributes->style, 4);
    }
    if (const auto* via = std::get_if<DesignRouteVia>(&shape.geometry); via != nullptr) {
      saw_special_via = true;
      EXPECT_EQ(via->via.columns, 2u);
      EXPECT_EQ(via->via.rows, 3u);
      EXPECT_EQ(via->via.step_x, 40);
      EXPECT_EQ(via->via.step_y, 50);
    }
  }
  EXPECT_TRUE(saw_special_attributes);
  EXPECT_TRUE(saw_special_via);

  const auto& geometry_storage = design.geometryStorage();
  const auto geometry_ids = geometry_storage.netShapeIds(special_net);
  ASSERT_EQ(geometry_ids.size(), 3u);
  const DesignGeometryRectangle* rectangle = nullptr;
  const DesignGeometryPolygon* polygon = nullptr;
  const DesignGeometryNetVia* via = nullptr;
  for (const auto shape_id : geometry_ids) {
    const auto view = geometry_storage.shape(shape_id);
    if (const auto* value = std::get_if<const DesignGeometryRectangle*>(&view.payload)) {
      rectangle = *value;
    } else if (const auto* value = std::get_if<const DesignGeometryPolygon*>(&view.payload)) {
      polygon = *value;
    } else if (const auto* value = std::get_if<const DesignGeometryNetVia*>(&view.payload)) {
      via = *value;
    }
  }
  ASSERT_NE(rectangle, nullptr);
  EXPECT_EQ(rectangle->route_status, DesignWireStatus::kShield);
  EXPECT_EQ(rectangle->shield_net, "n1");
  EXPECT_EQ(rectangle->shape, "BLOCKRING");
  EXPECT_EQ(rectangle->rectangle, (Rect{500, 500, 600, 600}));
  ASSERT_NE(polygon, nullptr);
  EXPECT_EQ(polygon->route_status, DesignWireStatus::kFixed);
  EXPECT_EQ(polygon->mask, 2u);
  EXPECT_EQ(polygon->points, (std::vector<Point>{{300, 300}, {400, 300}, {400, 400}, {300, 400}}));
  ASSERT_NE(via, nullptr);
  EXPECT_TRUE(via->design_via);
  EXPECT_EQ(via->orientation, DesignOrientation::kFN);
  EXPECT_EQ(via->top_mask, 1u);
  EXPECT_EQ(via->cut_mask, 2u);
  EXPECT_EQ(via->bottom_mask, 3u);
  EXPECT_EQ(via->origins, (std::vector<Point>{{700, 700}, {800, 800}}));
}

TEST_F(DefDesignRoundTripTest, ImportsAndExportsComplexNewVirtualFlushAndViaPathTokens)
{
  const auto input = writeInput(kComplexDef, "eccdb_complex_input.def");
  DesignStore first{tech, library.libraryRegistry()};
  DefDesignImporter importer(first);
  importer.import(input);
  EXPECT_TRUE(importer.diagnostics().empty());

  EXPECT_EQ(first.globalStorage().info().name, "top");
  EXPECT_EQ(first.floorplanStorage().rowCount(), 1u);
  EXPECT_EQ(first.floorplanStorage().trackGridCount(), 1u);
  EXPECT_EQ(first.floorplanStorage().gcellGridCount(), 1u);
  const auto& first_row = first.floorplanStorage().row(first.floorplanStorage().rows().front());
  EXPECT_EQ(first_row.step_x, 20);
  EXPECT_NE(first_row.flags & DesignRowFlag::kHasDo, 0u);
  EXPECT_EQ(first_row.flags & DesignRowFlag::kHasStep, 0u);
  ASSERT_EQ(first_row.properties.size(), 2u);
  EXPECT_EQ(first_row.properties[0].name, "ROW_KIND");
  EXPECT_EQ(first_row.properties[0].value, "logic");
  EXPECT_EQ(first_row.properties[0].type, DesignPropertyType::kString);
  EXPECT_EQ(first_row.properties[1].type, DesignPropertyType::kInteger);
  EXPECT_EQ(first.netlistStorage().instanceCount(), 1u);
  EXPECT_EQ(first.netlistStorage().ioPinCount(), 1u);
  EXPECT_EQ(first.netlistStorage().netCount(), 2u);
  EXPECT_EQ(first.routingStorage().viaCount(), 1u);
  EXPECT_EQ(first.constraintStorage().regionCount(), 1u);
  EXPECT_EQ(first.constraintStorage().groupCount(), 1u);
  EXPECT_EQ(first.constraintStorage().blockageCount(), 1u);
  const auto& blockage = first.constraintStorage().blockage(first.constraintStorage().blockages().front());
  EXPECT_NE(blockage.flags & DesignBlockageFlag::kSlots, 0u);
  EXPECT_NE(blockage.flags & DesignBlockageFlag::kExceptPgNet, 0u);
  EXPECT_EQ(blockage.spacing, 5);
  EXPECT_EQ(blockage.mask, 2u);
  ASSERT_EQ(blockage.polygons.size(), 1u);
  const auto io_pin_id = first.netlistStorage().findIoPin("IN");
  ASSERT_TRUE(io_pin_id);
  const auto& io_pin = first.netlistStorage().ioPin(io_pin_id);
  ASSERT_EQ(io_pin.pins.size(), 2u);
  const auto& first_bpin = first.netlistStorage().blockPin(io_pin.pins[0]);
  const auto& second_bpin = first.netlistStorage().blockPin(io_pin.pins[1]);
  ASSERT_EQ(first_bpin.rectangles.size(), 1u);
  ASSERT_EQ(first_bpin.vias.size(), 1u);
  const auto& first_rectangle = first.netlistStorage().blockPinRectangle(first_bpin.rectangles[0]);
  const auto& first_via = first.netlistStorage().blockPinVia(first_bpin.vias[0]);
  EXPECT_EQ(first_rectangle.mask, 2u);
  EXPECT_EQ(first_rectangle.spacing, 4);
  EXPECT_EQ(first_via.top_mask, 1u);
  EXPECT_EQ(first_via.cut_mask, 2u);
  EXPECT_EQ(first_via.bottom_mask, 3u);
  ASSERT_EQ(second_bpin.polygons.size(), 1u);
  EXPECT_EQ(first.netlistStorage().blockPinPolygon(second_bpin.polygons[0]).design_rule_width, 6);
  const auto instance_id = first.netlistStorage().findInstance("u1");
  ASSERT_TRUE(instance_id);
  const auto& instance = first.netlistStorage().instance(instance_id);
  EXPECT_NE(instance.flags & DesignInstanceFlag::kHasRegion, 0u);
  EXPECT_NE(instance.flags & DesignInstanceFlag::kHasHalo, 0u);
  EXPECT_NE(instance.flags & DesignInstanceFlag::kHaloSoft, 0u);
  EXPECT_NE(instance.flags & DesignInstanceFlag::kHasRouteHalo, 0u);
  EXPECT_EQ(first.constraintStorage().region(instance.region).name, "FENCE");
  EXPECT_EQ(instance.halo.left, 10);
  EXPECT_EQ(instance.halo.bottom, 20);
  EXPECT_EQ(instance.halo.right, 30);
  EXPECT_EQ(instance.halo.top, 40);
  EXPECT_EQ(instance.route_halo.distance, 50);
  const auto net_id = first.netlistStorage().findRegularNet("n1");
  ASSERT_TRUE(net_id);
  const auto& net = first.netlistStorage().net(net_id);
  EXPECT_NE(net.flags & DesignNetFlag::kHasNonDefaultRule, 0u);
  EXPECT_EQ(tech.registry().get<const TechNonDefaultRule>(net.non_default_rule.entity()).name, "WIDE");
  expectComplexRouting(first);

  DefDesignExporter exporter(first);
  const auto text = exporter.exportText();
  std::ostringstream streamed;
  exporter.write(streamed);
  EXPECT_EQ(streamed.str(), text);
  EXPECT_EQ(exporter.exportText(), text);
  EXPECT_NE(text.find(" NEW M1"), std::string::npos);
  EXPECT_NE(text.find("( 0 100 ) VIRTUAL ( 50 * ) ( 100 * 5 )"), std::string::npos);
  EXPECT_NE(text.find("NEW M1 TAPER ( 100 100 ) ( * 300 )"), std::string::npos);
  EXPECT_NE(text.find("( 0 200 ) ( 100 * )"), std::string::npos);
  EXPECT_NE(text.find(" RECT ( -2 -2 2 2 )"), std::string::npos);
  EXPECT_NE(text.find(" DO 2 BY 3 STEP 40 50"), std::string::npos);
  EXPECT_NE(text.find("+ REGION FENCE"), std::string::npos);
  EXPECT_NE(text.find("+ HALO SOFT 10 20 30 40"), std::string::npos);
  EXPECT_NE(text.find("+ ROUTEHALO 50 M1 M1"), std::string::npos);
  EXPECT_NE(text.find("+ NONDEFAULTRULE WIDE"), std::string::npos);
  EXPECT_NE(text.find("+ LAYER M1 MASK 2 SPACING 4"), std::string::npos);
  EXPECT_NE(text.find("+ VIA LOCAL MASK 123"), std::string::npos);
  EXPECT_NE(text.find("+ POLYGON M1 DESIGNRULEWIDTH 6"), std::string::npos);
  EXPECT_NE(text.find("+ SLOTS"), std::string::npos);
  EXPECT_NE(text.find("+ EXCEPTPGNET"), std::string::npos);
  EXPECT_NE(text.find("+ SPACING 5"), std::string::npos);
  EXPECT_NE(text.find("POLYGON ( 500 500 )"), std::string::npos);
  EXPECT_NE(text.find("+ FIXED + SHAPE RING + MASK 2 + POLYGON M1"), std::string::npos);
  EXPECT_NE(text.find("+ SHIELD n1 + SHAPE BLOCKRING + RECT M1"), std::string::npos);
  EXPECT_NE(text.find("+ COVER + MASK 123 + VIA LOCAL FN"), std::string::npos);
  EXPECT_NE(text.find("+ ORIGINAL source_n1"), std::string::npos);
  EXPECT_NE(text.find("+ PATTERN STEINER"), std::string::npos);
  EXPECT_NE(text.find("+ ESTCAP 1.25"), std::string::npos);
  EXPECT_NE(text.find("+ FREQUENCY 60.5"), std::string::npos);
  EXPECT_NE(text.find("+ VOLTAGE 900"), std::string::npos);
  EXPECT_NE(text.find("+ SPACING M1 12 RANGE 20 40"), std::string::npos);
  EXPECT_NE(text.find("ROW ROW0 CORE 0 0 N DO 10 BY 1 + PROPERTY ROW_KIND \"logic\" ROW_INDEX 2"), std::string::npos);

  const auto output = std::filesystem::path(testing::TempDir()) / "eccdb_complex_output.def";
  exporter.write(output);
  DesignStore second{tech, library.libraryRegistry()};
  DefDesignImporter reimporter(second);
  reimporter.import(output);
  EXPECT_TRUE(reimporter.diagnostics().empty());
  const auto& second_row = second.floorplanStorage().row(second.floorplanStorage().rows().front());
  EXPECT_EQ(second_row.step_x, 20);
  EXPECT_EQ(second_row.flags & DesignRowFlag::kHasStep, 0u);
  ASSERT_EQ(second_row.properties.size(), 2u);
  EXPECT_EQ(second_row.properties[0].value, "logic");
  EXPECT_EQ(second_row.properties[1].value, "2");
  expectComplexRouting(second);
  EXPECT_EQ(second.netlistStorage().instanceCount(), first.netlistStorage().instanceCount());
  EXPECT_EQ(second.netlistStorage().instancePinCount(), first.netlistStorage().instancePinCount());
  EXPECT_EQ(second.routingStorage().wireCount(), first.routingStorage().wireCount());
  const auto second_instance = second.netlistStorage().findInstance("u1");
  ASSERT_TRUE(second_instance);
  EXPECT_EQ(instanceRegionBounds(second, second.netlistStorage().instance(second_instance)), instanceRegionBounds(first, instance));
  EXPECT_EQ(second.netlistStorage().instance(second_instance).halo.left, instance.halo.left);
  EXPECT_EQ(second.netlistStorage().instance(second_instance).route_halo.distance, instance.route_halo.distance);
  const auto second_net = second.netlistStorage().findRegularNet("n1");
  ASSERT_TRUE(second_net);
  EXPECT_EQ(second.netlistStorage().net(second_net).non_default_rule, net.non_default_rule);
  const auto second_io_pin = second.netlistStorage().findIoPin("IN");
  ASSERT_TRUE(second_io_pin);
  EXPECT_EQ(second.netlistStorage().ioPin(second_io_pin).pins.size(), io_pin.pins.size());
}

TEST_F(DefDesignRoundTripTest, EditedDisconnectedShapesExportAsNewPathsWithoutAddingCopper)
{
  const auto input = writeInput(singleRouteDef("M1 ( 0 0 ) ( 100 0 ) ( 100 100 )"), "shape_edit_input.def");
  DesignStore design{tech, library.libraryRegistry()};
  DefDesignImporter{design}.import(input);
  auto& routing = design.routingStorage();
  const auto net = design.netlistStorage().findNet("signal");
  ASSERT_TRUE(net);
  const auto wire = routing.wires(net).front();
  const auto ids = routing.wire(wire).shapes;
  ASSERT_EQ(ids.size(), 2u);

  auto edited = routing.shape(ids[1]);
  auto& segment = std::get<DesignRouteSegment>(edited.geometry);
  segment.begin.position = {200, 0};
  segment.end.position = {200, 100};
  routing.updateShape(ids[1], edited);
  EXPECT_EQ(routing.wire(wire).shapes, ids);
  EXPECT_EQ(routing.wire(wire).shapes.size(), 2u);

  const auto text = DefDesignExporter{design}.exportText();
  EXPECT_NE(text.find("NEW M1"), std::string::npos);
  DesignStore restored{tech, library.libraryRegistry()};
  DefDesignImporter{restored}.import(writeInput(text, "shape_edit_output.def"));
  const auto restored_net = restored.netlistStorage().findNet("signal");
  ASSERT_TRUE(restored_net);
  const auto restored_wire = restored.routingStorage().wires(restored_net).front();
  const auto& restored_ids = restored.routingStorage().wire(restored_wire).shapes;
  ASSERT_EQ(restored_ids.size(), 2u);
  for (std::size_t index = 0; index < ids.size(); ++index) {
    const auto& expected = std::get<DesignRouteSegment>(routing.shape(ids[index]).geometry);
    const auto& actual = std::get<DesignRouteSegment>(restored.routingStorage().shape(restored_ids[index]).geometry);
    EXPECT_EQ(actual.begin.position, expected.begin.position);
    EXPECT_EQ(actual.end.position, expected.end.position);
    EXPECT_EQ(restored.routingStorage().shape(restored_ids[index]).layer, routing.shape(ids[index]).layer);
  }
}

TEST_F(DefDesignRoundTripTest, KeepsDefinedViaBetweenSegmentsInOnePath)
{
  const auto input = writeInput(singleRouteDef("M1 ( 100 100 ) ( 200 100 ) LOCAL12 ( 200 300 )", kLocalVia12),
                                "eccdb_cross_layer_path.def");
  DesignStore design{tech, library.libraryRegistry()};
  DefDesignImporter(design).import(input);

  const auto local_via = design.routingStorage().findVia("LOCAL12");
  ASSERT_TRUE(local_via);
  const auto& via_geometry = design.routingStorage().via(local_via).rectangles;
  ASSERT_EQ(via_geometry.size(), 3u);
  const auto& geometry = design.geometryRegistry();
  EXPECT_EQ(tech.registry().get<const TechLayerInfo>(geometry.get<const DesignViaRectangle>(via_geometry[0]).layer.entity()).name, "M1");
  EXPECT_EQ(tech.registry().get<const TechLayerInfo>(geometry.get<const DesignViaRectangle>(via_geometry[2]).layer.entity()).name, "M2");

  const auto net = design.netlistStorage().findRegularNet("signal");
  ASSERT_TRUE(net);
  const auto wires = design.routingStorage().wires(net);
  ASSERT_EQ(wires.size(), 1u);
  const auto& wire_shapes = design.routingStorage().wire(wires.front()).shapes;
  ASSERT_EQ(wire_shapes.size(), 3u);
  const auto local_via_shape = std::find_if(wire_shapes.begin(), wire_shapes.end(), [&](const auto shape_id) {
    return std::holds_alternative<DesignRouteVia>(design.routingStorage().shape(shape_id).geometry);
  });
  ASSERT_NE(local_via_shape, wire_shapes.end());
  const auto& via_shape = std::get<DesignRouteVia>(design.routingStorage().shape(*local_via_shape).geometry);
  EXPECT_EQ(via_shape.via.design_via, local_via);
  EXPECT_EQ(via_shape.origin.position, (Point{200, 100}));
  const auto final_segment = std::find_if(wire_shapes.begin(), wire_shapes.end(), [&](const auto shape_id) {
    const auto* segment = std::get_if<DesignRouteSegment>(&design.routingStorage().shape(shape_id).geometry);
    return segment != nullptr && segment->end.position == Point{200, 300};
  });
  ASSERT_NE(final_segment, wire_shapes.end());

  const auto output = DefDesignExporter(design).exportText();
  const auto via = output.find(" LOCAL12", output.find("NETS 1 ;"));
  ASSERT_NE(via, std::string::npos);
  EXPECT_NE(output.find("( * 300 )", via), std::string::npos);

  DesignStore reimported{tech, library.libraryRegistry()};
  DefDesignImporter(reimported).import(writeInput(output, "eccdb_cross_layer_path_roundtrip.def"));
  const auto reimported_net = reimported.netlistStorage().findRegularNet("signal");
  ASSERT_TRUE(reimported_net);
  const auto reimported_wire = reimported.routingStorage().wires(reimported_net);
  ASSERT_EQ(reimported_wire.size(), 1u);
  const auto& reimported_shapes = reimported.routingStorage().wire(reimported_wire.front()).shapes;
  ASSERT_EQ(reimported_shapes.size(), 3u);
  const auto reimported_via = std::find_if(reimported_shapes.begin(), reimported_shapes.end(), [&](const auto shape_id) {
    return std::holds_alternative<DesignRouteVia>(reimported.routingStorage().shape(shape_id).geometry);
  });
  ASSERT_NE(reimported_via, reimported_shapes.end());
  EXPECT_EQ(std::get<DesignRouteVia>(reimported.routingStorage().shape(*reimported_via).geometry).via.design_via,
            reimported.routingStorage().findVia("LOCAL12"));
  const auto reimported_segment = std::find_if(reimported_shapes.begin(), reimported_shapes.end(), [&](const auto shape_id) {
    const auto* segment = std::get_if<DesignRouteSegment>(&reimported.routingStorage().shape(shape_id).geometry);
    return segment != nullptr && segment->end.position == Point{200, 300};
  });
  ASSERT_NE(reimported_segment, reimported_shapes.end());
}

TEST_F(DefDesignRoundTripTest, DISABLED_PreservesRectAfterViaAtTheSamePoint)
{
  const auto input = writeInput(singleRouteDef("M1 ( 100 100 ) LOCAL12 RECT ( -2 -2 2 2 ) ( 100 200 )", kLocalVia12),
                                "eccdb_rect_after_via.def");
  DesignStore design{tech, library.libraryRegistry()};
  DefDesignImporter(design).import(input);

  const auto output = DefDesignExporter(design).exportText();
  const auto via = output.find(" LOCAL12", output.find("NETS 1 ;"));
  const auto rectangle = output.find(" RECT ( -2 -2 2 2 )");
  ASSERT_NE(via, std::string::npos);
  ASSERT_NE(rectangle, std::string::npos);
  EXPECT_LT(via, rectangle) << "RECT follows VIA and belongs to the exit layer";
}

TEST_F(DefDesignRoundTripTest, DISABLED_PreservesMaskChangesWithinOnePath)
{
  const auto input = writeInput(singleRouteDef("M1 ( 100 100 ) MASK 1 ( 200 100 ) MASK 2 ( 300 100 )"),
                                "eccdb_mid_path_masks.def");
  DesignStore design{tech, library.libraryRegistry()};
  DefDesignImporter(design).import(input);

  const auto output = DefDesignExporter(design).exportText();
  const auto first_mask = output.find(" MASK 1");
  const auto second_mask = output.find(" MASK 2");
  ASSERT_NE(first_mask, std::string::npos) << "the first MASK was overwritten during import";
  ASSERT_NE(second_mask, std::string::npos);
  EXPECT_LT(first_mask, second_mask);
}

TEST_F(DefDesignRoundTripTest, ReportsBufferedOutputFailure)
{
  const auto input = writeInput(kComplexDef, "eccdb_failed_output_input.def");
  DesignStore design{tech, library.libraryRegistry()};
  DefDesignImporter(design).import(input);

  RejectingStreamBuffer buffer;
  std::ostream output(&buffer);
  EXPECT_THROW(DefDesignExporter(design).write(output), std::runtime_error);
}

TEST_F(DefDesignRoundTripTest, RollsBackWhenReferenceResolutionFailsDuringCommit)
{
  const auto input = writeInput(R"DEF(VERSION 5.8 ;
DESIGN broken_reference ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 100 100 ) ;
COMPONENTS 1 ;
- u1 INVX1 + PLACED ( 0 0 ) N ;
END COMPONENTS
NETS 1 ;
- n1 ( u1 UNKNOWN_PIN ) ;
END NETS
END DESIGN
)DEF",
                                "eccdb_unknown_pin.def");
  DesignStore design{tech, library.libraryRegistry()};
  DefDesignImporter importer(design);
  EXPECT_THROW(importer.import(input), std::runtime_error);
  EXPECT_TRUE(design.globalStorage().containsRoot());
  EXPECT_FALSE(design.globalStorage().hasInfo());
  EXPECT_FALSE(design.globalStorage().hasDieArea());
  EXPECT_EQ(design.netlistStorage().instanceCount(), 0u);
  EXPECT_EQ(design.netlistStorage().instancePinCount(), 0u);
  const auto& registry = std::as_const(design.designRegistry().registry());
  const auto terms = registry.view<const DesignInstanceTerm>();
  EXPECT_EQ(terms.begin(), terms.end());

  const auto recovery = writeInput(R"DEF(VERSION 5.8 ;
DESIGN recovered ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 100 100 ) ;
COMPONENTS 1 ;
- u1 INVX1 + PLACED ( 0 0 ) N ;
END COMPONENTS
NETS 1 ;
- n1 ( u1 A ) ;
END NETS
END DESIGN
)DEF",
                                   "eccdb_recovered_after_rollback.def");
  EXPECT_NO_THROW(DefDesignImporter(design).import(recovery));
  EXPECT_TRUE(design.netlistStorage().findInstance("u1"));
  EXPECT_TRUE(design.netlistStorage().findRegularNet("n1"));
  EXPECT_EQ(design.netlistStorage().instancePinCount(), 2u);
  EXPECT_NO_THROW(design.netlistStorage().rebuildLibraryCaches());
}

TEST_F(DefDesignRoundTripTest, ImportsAndExportsGeneratedDefVia)
{
  const auto input = writeInput(R"DEF(VERSION 5.8 ;
DESIGN generated_via ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 100 100 ) ;
VIAS 1 ;
- GENERATED
  + VIARULE GEN12
  + CUTSIZE 10 12
  + LAYERS M1 V1 M2
  + CUTSPACING 20 22
  + ENCLOSURE 3 4 5 6
  + ROWCOL 2 3
  + PATTERN CUT_PATTERN
  + ORIGIN 7 -8
  + OFFSET 9 10 -11 -12
  ;
END VIAS
END DESIGN
)DEF",
                                "eccdb_generated_via.def");
  DesignStore first{tech, library.libraryRegistry()};
  DefDesignImporter(first).import(input);
  const auto id = first.routingStorage().findVia("GENERATED");
  ASSERT_TRUE(id);
  const auto& via = first.routingStorage().via(id);
  EXPECT_NE(via.flags & DesignViaFlag::kGenerated, 0u);
  EXPECT_EQ(via.generated.cut_size_x, 10);
  EXPECT_EQ(via.generated.cut_size_y, 12);
  EXPECT_EQ(via.generated.row_count, 2u);
  EXPECT_EQ(via.generated.column_count, 3u);
  EXPECT_EQ(via.generated.cut_pattern, "CUT_PATTERN");
  EXPECT_EQ(via.generated.origin, (Point{7, -8}));
  EXPECT_EQ(via.generated.bottom_offset, (Point{9, 10}));
  EXPECT_EQ(via.generated.top_offset, (Point{-11, -12}));

  const auto text = DefDesignExporter(first).exportText();
  EXPECT_NE(text.find("+ VIARULE GEN12"), std::string::npos);
  EXPECT_NE(text.find("+ ROWCOL 2 3"), std::string::npos);
  EXPECT_NE(text.find("+ PATTERN CUT_PATTERN"), std::string::npos);
  const auto output = writeInput(text, "eccdb_generated_via_output.def");
  DesignStore second{tech, library.libraryRegistry()};
  DefDesignImporter(second).import(output);
  const auto second_id = second.routingStorage().findVia("GENERATED");
  ASSERT_TRUE(second_id);
  EXPECT_EQ(second.routingStorage().via(second_id).generated.cut_pattern, via.generated.cut_pattern);
  EXPECT_EQ(second.routingStorage().via(second_id).generated.top_offset, via.generated.top_offset);
}

TEST_F(DefDesignRoundTripTest, PreservesRegularAndSpecialConnectionsOnTheSamePins)
{
  const auto input = writeInput(R"DEF(VERSION 5.8 ;
DESIGN shared_connections ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 100 100 ) ;
COMPONENTS 1 ;
- u1 INVX1 ;
END COMPONENTS
PINS 1 ;
- IO + NET shared + SPECIAL + DIRECTION INOUT ;
END PINS
SPECIALNETS 1 ;
- shared ( PIN IO ) ( u1 A ) ( PIN IO ) ( u1 A ) + USE POWER ;
END SPECIALNETS
NETS 1 ;
- shared ( PIN IO ) ( u1 A ) ( PIN IO ) ( u1 A ) + USE SIGNAL ;
END NETS
END DESIGN
)DEF",
                                "eccdb_shared_regular_special.def");

  DesignStore first{tech, library.libraryRegistry()};
  DefDesignImporter importer(first);
  importer.import(input);
  EXPECT_TRUE(importer.diagnostics().empty());

  const auto regular = first.netlistStorage().findRegularNet("shared");
  const auto special = first.netlistStorage().findSpecialNet("shared");
  ASSERT_TRUE(regular);
  ASSERT_TRUE(special);
  ASSERT_NE(regular, special);
  const auto io = first.netlistStorage().findIoPin("IO");
  const auto instance = first.netlistStorage().findInstance("u1");
  const auto instance_pin = first.netlistStorage().findInstancePin(instance, "A");
  ASSERT_TRUE(io);
  ASSERT_TRUE(instance_pin);
  EXPECT_EQ(first.netlistStorage().ioPin(io).net, regular);
  EXPECT_EQ(first.netlistStorage().ioPin(io).special_net, special);
  EXPECT_EQ(first.netlistStorage().instancePin(instance_pin).net, regular);
  EXPECT_EQ(first.netlistStorage().instancePin(instance_pin).special_net, special);
  for (const auto net : {regular, special}) {
    ASSERT_EQ(first.netlistStorage().instancePins(net).size(), 1u);
    EXPECT_EQ(first.netlistStorage().instancePins(net).front(), instance_pin);
    ASSERT_EQ(first.netlistStorage().ioPins(net).size(), 1u);
    EXPECT_EQ(first.netlistStorage().ioPins(net).front(), io);
  }

  const auto text = DefDesignExporter(first).exportText();
  EXPECT_NE(text.find("+ SPECIAL"), std::string::npos);
  const auto output = writeInput(text, "eccdb_shared_regular_special_output.def");
  DesignStore second{tech, library.libraryRegistry()};
  DefDesignImporter(second).import(output);
  EXPECT_TRUE(second.netlistStorage().findRegularNet("shared"));
  EXPECT_TRUE(second.netlistStorage().findSpecialNet("shared"));
}

TEST_F(DefDesignRoundTripTest, ReconnectsRepeatedPinDeclarationsWithoutStaleReverseConnections)
{
  const auto input = writeInput(R"DEF(VERSION 5.8 ;
DESIGN reconnected_pins ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 100 100 ) ;
COMPONENTS 1 ;
- u1 INVX1 ;
END COMPONENTS
PINS 1 ;
- IO + NET n2 + DIRECTION INPUT ;
END PINS
NETS 2 ;
- n1 ( PIN IO ) ( u1 A ) ;
- n2 ( PIN IO ) ( u1 A ) ;
END NETS
END DESIGN
)DEF",
                                "eccdb_reconnected_pins.def");
  DesignStore design{tech, library.libraryRegistry()};
  DefDesignImporter importer(design);
  importer.import(input);
  auto& netlist = design.netlistStorage();
  const auto first = netlist.findRegularNet("n1");
  const auto second = netlist.findRegularNet("n2");
  const auto pin = netlist.findInstancePin(netlist.findInstance("u1"), "A");
  const auto io = netlist.findIoPin("IO");
  EXPECT_TRUE(netlist.instancePins(first).empty());
  EXPECT_TRUE(netlist.ioPins(first).empty());
  ASSERT_EQ(netlist.instancePins(second).size(), 1u);
  EXPECT_EQ(netlist.instancePins(second).front(), pin);
  ASSERT_EQ(netlist.ioPins(second).size(), 1u);
  EXPECT_EQ(netlist.ioPins(second).front(), io);
  EXPECT_EQ(netlist.instancePin(pin).net, second);
  EXPECT_EQ(netlist.ioPin(io).net, second);
}

TEST_F(DefDesignRoundTripTest, RejectsMustJoinIoPinBeforeConnecting)
{
  const auto input = writeInput(R"DEF(VERSION 5.8 ;
DESIGN invalid_mustjoin ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 100 100 ) ;
PINS 1 ;
- IO + NET n1 + DIRECTION INPUT ;
END PINS
NETS 1 ;
- MUSTJOIN ( PIN IO ) ;
END NETS
END DESIGN
)DEF",
                                "eccdb_invalid_mustjoin_io.def");
  DesignStore design{tech, library.libraryRegistry()};
  EXPECT_THROW(DefDesignImporter(design).import(input), std::runtime_error);
  EXPECT_EQ(design.netlistStorage().netCount(), 0u);
  EXPECT_EQ(design.netlistStorage().ioPinCount(), 0u);
}

TEST_F(DefDesignRoundTripTest, ImportsAndExportsRectangularFillsAndMustJoinNets)
{
  const auto input = writeInput(R"DEF(VERSION 5.8 ;
DESIGN fill_and_mustjoin ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 1000 1000 ) ;
COMPONENTS 2 ;
- u1 INVX1 + PLACED ( 0 0 ) N ;
- u2 INVX1 + PLACED ( 100 0 ) N ;
END COMPONENTS
FILLS 1 ;
- LAYER M1 + MASK 2 + OPC
  RECT ( 10 20 ) ( 30 40 )
  RECT ( 50 60 ) ( 80 90 ) ;
END FILLS
NETS 1 ;
- MUSTJOIN ( u1 Y ) ;
END NETS
END DESIGN
)DEF",
                                "eccdb_fill_mustjoin.def");

  DesignStore first{tech, library.libraryRegistry()};
  DefDesignImporter importer(first);
  importer.import(input);
  EXPECT_TRUE(importer.diagnostics().empty());

  ASSERT_EQ(first.fillStorage().fillCount(), 1u);
  const auto fill_id = first.fillStorage().fills().front();
  const auto& fill = first.fillStorage().fill(fill_id);
  EXPECT_EQ(fill.mask, 2u);
  EXPECT_NE(fill.flags & DesignFillFlag::kOpc, 0u);
  ASSERT_EQ(fill.rectangles.size(), 2u);
  EXPECT_EQ(first.fillStorage().rectangle(fill.rectangles[0]), (Rect{10, 20, 30, 40}));

  const auto nets = first.netlistStorage().regularNets();
  ASSERT_EQ(nets.size(), 1u);
  EXPECT_NE(first.netlistStorage().net(nets.front()).flags & DesignNetFlag::kMustJoin, 0u);
  EXPECT_EQ(first.netlistStorage().instancePins(nets.front()).size(), 1u);

  const auto text = DefDesignExporter(first).exportText();
  EXPECT_NE(text.find("FILLS 1 ;"), std::string::npos);
  EXPECT_NE(text.find("- LAYER M1 + MASK 2 + OPC"), std::string::npos);
  EXPECT_NE(text.find("- MUSTJOIN ( "), std::string::npos);
  EXPECT_EQ(text.find("- __idb_mustjoin_"), std::string::npos);

  const auto output = writeInput(text, "eccdb_fill_mustjoin_output.def");
  DesignStore second{tech, library.libraryRegistry()};
  DefDesignImporter reimporter(second);
  reimporter.import(output);
  EXPECT_TRUE(reimporter.diagnostics().empty());
  EXPECT_EQ(second.fillStorage().fillCount(), 1u);
  const auto second_nets = second.netlistStorage().regularNets();
  ASSERT_EQ(second_nets.size(), 1u);
  EXPECT_NE(second.netlistStorage().net(second_nets.front()).flags & DesignNetFlag::kMustJoin, 0u);
  EXPECT_EQ(second.netlistStorage().instancePins(second_nets.front()).size(), 1u);
}

TEST_F(DefDesignRoundTripTest, ResolvesGroupRegionDeclaredAfterGroups)
{
  const auto input = writeInput(R"DEF(VERSION 5.8 ;
DESIGN forward_group_region ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 1000 1000 ) ;
COMPONENTS 1 ;
- u1 INVX1 + PLACED ( 0 0 ) N ;
END COMPONENTS
GROUPS 1 ;
- logic u1 + REGION late_region ;
END GROUPS
REGIONS 1 ;
- late_region ( 0 0 ) ( 500 500 ) + TYPE FENCE ;
END REGIONS
END DESIGN
)DEF",
                                "eccdb_forward_group_region.def");

  DesignStore design{tech, library.libraryRegistry()};
  DefDesignImporter(design).import(input);

  const auto group_id = design.constraintStorage().findGroup("logic");
  const auto region_id = design.constraintStorage().findRegion("late_region");
  ASSERT_TRUE(group_id);
  ASSERT_TRUE(region_id);
  const auto& group = design.constraintStorage().group(group_id);
  EXPECT_NE(group.flags & DesignGroupFlag::kHasRegion, 0u);
  EXPECT_EQ(group.region, region_id);
  ASSERT_EQ(group.instances.size(), 1u);
  EXPECT_EQ(design.netlistStorage().instance(group.instances.front()).name, "u1");
}

TEST_F(DefDesignRoundTripTest, ReportsUnsupportedPolygonAndViaFills)
{
  const auto input = writeInput(R"DEF(VERSION 5.8 ;
DESIGN unsupported_fills ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 1000 1000 ) ;
FILLS 2 ;
- LAYER M1
  POLYGON ( 10 10 ) ( 30 10 ) ( 30 30 ) ( 10 30 ) ;
- VIA VIA12
  ( 50 60 ) ;
END FILLS
END DESIGN
)DEF",
                                "eccdb_unsupported_fills.def");

  DesignStore design{tech, library.libraryRegistry()};
  DefDesignImporter importer(design);
  importer.import(input);
  EXPECT_EQ(design.fillStorage().fillCount(), 0u);
  ASSERT_EQ(importer.diagnostics().size(), 2u);
  EXPECT_EQ(importer.diagnostics()[0].statement, "FILL POLYGON");
  EXPECT_EQ(importer.diagnostics()[0].occurrence_count, 1u);
  EXPECT_EQ(importer.diagnostics()[1].statement, "FILL VIA");
  EXPECT_EQ(importer.diagnostics()[1].occurrence_count, 1u);
}

}  // namespace
}  // namespace eccdb
