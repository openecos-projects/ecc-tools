// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <streambuf>
#include <string>

#include "def/DefDesignExporter.h"
#include "def/DefDesignImporter.h"
#include "library/LibraryStore.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {
namespace {

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
  ASSERT_EQ(routing.pathCount(regular_wires.front()), 2u);
  const auto regular_path0 = routing.path(regular_wires.front(), 0);
  const auto regular_path1 = routing.path(regular_wires.front(), 1);
  EXPECT_EQ(regular_path0.points().size(), 3u);
  EXPECT_NE(regular_path0.points()[1].flags & DesignWirePointFlag::kVirtual, 0u);
  EXPECT_NE(regular_path0.points()[2].flags & DesignWirePointFlag::kHasExtension, 0u);
  EXPECT_EQ(regular_path0.points()[2].extension, 5);
  ASSERT_EQ(regular_path0.vias().size(), 1u);
  EXPECT_EQ(regular_path0.vias()[0].orientation, DesignOrientation::kFN);
  EXPECT_EQ(regular_path0.vias()[0].top_mask, 1u);
  EXPECT_EQ(regular_path0.vias()[0].cut_mask, 2u);
  EXPECT_EQ(regular_path0.vias()[0].bottom_mask, 3u);
  ASSERT_EQ(regular_path0.rectangles().size(), 1u);
  EXPECT_EQ(regular_path0.rectangles()[0].delta, (Rect{-2, -2, 2, 2}));
  EXPECT_NE(regular_path1.flags() & DesignWirePathFlag::kTaper, 0u);

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
  ASSERT_EQ(routing.pathCount(special_wires.front()), 2u);
  const auto special_path0 = routing.path(special_wires.front(), 0);
  EXPECT_EQ(special_path0.width(), 20);
  EXPECT_EQ(special_path0.shape(), "STRIPE");
  EXPECT_EQ(special_path0.style(), 4);
  ASSERT_EQ(special_path0.vias().size(), 1u);
  EXPECT_EQ(special_path0.vias()[0].columns, 2u);
  EXPECT_EQ(special_path0.vias()[0].rows, 3u);
  EXPECT_EQ(special_path0.vias()[0].step_x, 40);
  EXPECT_EQ(special_path0.vias()[0].step_y, 50);

  const auto* geometry = routing.netGeometry(special_net);
  ASSERT_NE(geometry, nullptr);
  ASSERT_EQ(geometry->rectangles.size(), 1u);
  EXPECT_EQ(geometry->rectangles[0].route_status, DesignWireStatus::kShield);
  EXPECT_EQ(geometry->rectangles[0].shield_net, "n1");
  EXPECT_EQ(geometry->rectangles[0].shape, "BLOCKRING");
  EXPECT_EQ(geometry->rectangles[0].rectangle, (Rect{500, 500, 600, 600}));
  ASSERT_EQ(geometry->polygons.size(), 1u);
  EXPECT_EQ(geometry->polygons[0].route_status, DesignWireStatus::kFixed);
  EXPECT_EQ(geometry->polygons[0].mask, 2u);
  EXPECT_EQ(geometry->polygons[0].points, (std::vector<Point>{{300, 300}, {400, 300}, {400, 400}, {300, 400}}));
  ASSERT_EQ(geometry->vias.size(), 1u);
  EXPECT_TRUE(geometry->vias[0].design_via);
  EXPECT_EQ(geometry->vias[0].orientation, DesignOrientation::kFN);
  EXPECT_EQ(geometry->vias[0].top_mask, 1u);
  EXPECT_EQ(geometry->vias[0].cut_mask, 2u);
  EXPECT_EQ(geometry->vias[0].bottom_mask, 3u);
  EXPECT_EQ(geometry->vias[0].origins, (std::vector<Point>{{700, 700}, {800, 800}}));
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
  ASSERT_EQ(io_pin.ports.size(), 2u);
  ASSERT_EQ(io_pin.ports[0].rectangles.size(), 1u);
  ASSERT_EQ(io_pin.ports[0].vias.size(), 1u);
  EXPECT_EQ(io_pin.ports[0].rectangles[0].mask, 2u);
  EXPECT_EQ(io_pin.ports[0].rectangles[0].spacing, 4);
  EXPECT_EQ(io_pin.ports[0].vias[0].top_mask, 1u);
  EXPECT_EQ(io_pin.ports[0].vias[0].cut_mask, 2u);
  EXPECT_EQ(io_pin.ports[0].vias[0].bottom_mask, 3u);
  ASSERT_EQ(io_pin.ports[1].polygons.size(), 1u);
  EXPECT_EQ(io_pin.ports[1].polygons[0].design_rule_width, 6);
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
  EXPECT_EQ(second.netlistStorage().instance(second_instance).region_bounds, instance.region_bounds);
  EXPECT_EQ(second.netlistStorage().instance(second_instance).halo.left, instance.halo.left);
  EXPECT_EQ(second.netlistStorage().instance(second_instance).route_halo.distance, instance.route_halo.distance);
  const auto second_net = second.netlistStorage().findRegularNet("n1");
  ASSERT_TRUE(second_net);
  EXPECT_EQ(second.netlistStorage().net(second_net).non_default_rule, net.non_default_rule);
  const auto second_io_pin = second.netlistStorage().findIoPin("IN");
  ASSERT_TRUE(second_io_pin);
  EXPECT_EQ(second.netlistStorage().ioPin(second_io_pin).ports.size(), io_pin.ports.size());
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
- shared ( PIN IO ) ( u1 A ) + USE POWER ;
END SPECIALNETS
NETS 1 ;
- shared ( PIN IO ) ( u1 A ) + USE SIGNAL ;
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

  const auto text = DefDesignExporter(first).exportText();
  EXPECT_NE(text.find("+ SPECIAL"), std::string::npos);
  const auto output = writeInput(text, "eccdb_shared_regular_special_output.def");
  DesignStore second{tech, library.libraryRegistry()};
  DefDesignImporter(second).import(output);
  EXPECT_TRUE(second.netlistStorage().findRegularNet("shared"));
  EXPECT_TRUE(second.netlistStorage().findSpecialNet("shared"));
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
  EXPECT_EQ(fill.rectangles[0], (Rect{10, 20, 30, 40}));

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
