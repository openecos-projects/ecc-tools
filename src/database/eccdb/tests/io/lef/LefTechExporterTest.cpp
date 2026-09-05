// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "lef/LefTechExporter.h"
#include "lef/LefTechImporter.h"

namespace eccdb {
namespace {

class LefTechExporterTest : public testing::Test
{
 protected:
  std::filesystem::path writeFile(std::string_view stem, std::string_view contents)
  {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path
        = std::filesystem::temp_directory_path()
          / ("idb-refactor-export-" + std::string(stem) + '-' + std::to_string(nonce) + '-' + std::to_string(_paths.size()) + ".lef");
    std::ofstream output(path);
    if (!output) {
      throw std::runtime_error("failed to create temporary LEF test input");
    }
    output << contents;
    output.close();
    _paths.push_back(path);
    return path;
  }

  std::filesystem::path newPath(std::string_view stem)
  {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path
        = std::filesystem::temp_directory_path()
          / ("idb-refactor-export-" + std::string(stem) + '-' + std::to_string(nonce) + '-' + std::to_string(_paths.size()) + ".lef");
    _paths.push_back(path);
    return path;
  }

  void TearDown() override
  {
    for (const auto& path : _paths) {
      std::error_code error;
      std::filesystem::remove(path, error);
    }
  }

  std::vector<std::filesystem::path> _paths;
};

TEST_F(LefTechExporterTest, RoundTripsGlobalsLayersAndNativeRules)
{
  const auto source_path = writeFile("source", R"LEF(
VERSION 5.8 ;
UNITS
  TIME NANOSECONDS 1 ;
  CAPACITANCE PICOFARADS 2 ;
  RESISTANCE OHMS 3 ;
  POWER MILLIWATTS 4 ;
  CURRENT MILLIAMPS 5 ;
  VOLTAGE VOLTS 6 ;
  DATABASE MICRONS 1000 ;
  FREQUENCY MEGAHERTZ 7 ;
END UNITS
MANUFACTURINGGRID 0.005 ;
PROPERTYDEFINITIONS
  LAYER LEF58_TYPE STRING ;
  LAYER LEF58_BACKSIDE STRING ;
  LAYER LEF58_ENCLOSURE STRING ;
  LAYER LEF58_SPACINGTABLE STRING ;
  LAYER LEF58_CORNERSPACING STRING ;
  LAYER USER STRING ;
END PROPERTYDEFINITIONS
LAYER NW
  TYPE MASTERSLICE ;
  PROPERTY LEF58_TYPE "TYPE NWELL ;" ;
END NW
LAYER NIMP
  TYPE IMPLANT ;
  WIDTH 0.10 ;
  SPACING 0.20 LAYER PIMP ;
  PROPERTY USER "implant" ;
END NIMP
LAYER PIMP
  TYPE IMPLANT ;
  WIDTH 0.11 ;
END PIMP
LAYER M1
  TYPE ROUTING ;
  MASK 2 ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 0.30 ;
  OFFSET 0.10 0.15 ;
  WIDTH 0.10 ;
  MINWIDTH 0.08 ;
  MAXWIDTH 1.50 ;
  THICKNESS 0.07 ;
  AREA 0.05 ;
  RESISTANCE RPERSQ 2.5 ;
  CAPACITANCE CPERSQDIST 0.001 ;
  EDGECAPACITANCE 0.002 ;
  MINIMUMDENSITY 20 ;
  MAXIMUMDENSITY 80 ;
  DENSITYCHECKWINDOW 1.0 2.0 ;
  DENSITYCHECKSTEP 0.5 ;
  SPACING 0.10 ;
  SPACING 0.20 RANGE 0.30 0.40 ;
  SPACING 0.12 NOTCHLENGTH 0.15 ;
  SPACING 0.13 ENDOFLINE 0.14 WITHIN 0.15 PARALLELEDGE 0.16 WITHIN 0.17 TWOEDGES ;
  MINENCLOSEDAREA 0.04 ;
  MINENCLOSEDAREA 0.05 WIDTH 0.25 ;
  MINSTEP 0.05 INSIDECORNER LENGTHSUM 0.20 ;
  MINSTEP 0.06 MAXEDGES 3 ;
  MINIMUMCUT 2 WIDTH 0.50 WITHIN 0.10 FROMABOVE LENGTH 0.60 WITHIN 0.20 ;
  DCCURRENTDENSITY AVERAGE 5.5 ;
  PROPERTY LEF58_TYPE "TYPE POLYROUTING ;" ;
  PROPERTY LEF58_BACKSIDE "BACKSIDE ;" ;
  PROPERTY LEF58_CORNERSPACING "CORNERSPACING CONVEXCORNER CORNERTOCORNER EXCEPTEOL 0.09 WIDTH 0.10 SPACING 0.11 WIDTH 0.20 SPACING 0.21 ;" ;
  PROPERTY USER "routing" ;
END M1
LAYER V0
  TYPE CUT ;
  WIDTH 0.04 ;
END V0
LAYER V1
  TYPE CUT ;
  WIDTH 0.05 ;
  RESISTANCE 4.5 ;
  SPACING 0.10 ;
  SPACING 0.11 SAMENET ;
  SPACING 0.20 ADJACENTCUTS 3 WITHIN 0.30 ;
  SPACING 0.21 CENTERTOCENTER SAMENET AREA 0.02 ;
  SPACING 0.22 SAMENET PGONLY ;
  SPACING 0.23 SAMENET LAYER V0 STACK ;
  SPACING 0.24 ADJACENTCUTS 2 WITHIN 0.25 EXCEPTSAMEPGNET ;
  SPACING 0.26 PARALLELOVERLAP ;
  ENCLOSURE 0.005 0.006 LENGTH 0.007 ;
  ENCLOSURE BELOW 0.01 0.02 ;
  ENCLOSURE BELOW 0.015 0.025 WIDTH 0.08 EXCEPTEXTRACUT 0.09 ;
  ENCLOSURE ABOVE 0.03 0.04 ;
  ARRAYSPACING LONGARRAY WIDTH 0.08 CUTSPACING 0.05
    ARRAYCUTS 2 SPACING 0.06
    ARRAYCUTS 3 SPACING 0.07 ;
  SPACINGTABLE ORTHOGONAL
    WITHIN 0.30 SPACING 0.20
    WITHIN 0.10 SPACING 0.15 ;
  DCCURRENTDENSITY AVERAGE 0.40 ;
  PROPERTY LEF58_TYPE "TYPE SPECIALCUT ;" ;
  PROPERTY LEF58_BACKSIDE "BACKSIDE ;" ;
  PROPERTY LEF58_ENCLOSURE
    "ENCLOSURE CUTCLASS C1 ABOVE 0.11 0.12 WIDTH 0.13 INCLUDEABUTTED EXCEPTEXTRACUT 0.14 NOSHAREDEDGE ;" ;
  PROPERTY LEF58_SPACINGTABLE
    "SPACINGTABLE ORTHOGONAL WITHIN 0.21 SPACING 0.22 WITHIN 0.23 SPACING 0.24 ;" ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.40 ;
  OFFSET 0.20 ;
  WIDTH 0.20 ;
END M2
LAYER OVERLAP
  TYPE OVERLAP ;
END OVERLAP
MAXVIASTACK 2 RANGE M1 M2 ;
END LIBRARY
)LEF");

  TechStore source;
  LefTechImporter source_importer(source);
  ASSERT_NO_THROW(source_importer.import(source_path));

  std::ostringstream text;
  ASSERT_NO_THROW(LefTechExporter::write(text, source));
  EXPECT_NE(text.str().find("LAYER M1"), std::string::npos);
  EXPECT_NE(text.str().find("ARRAYSPACING LONGARRAY"), std::string::npos);
  EXPECT_NE(text.str().find("ARRAYSPACING LONGARRAY WIDTH 0.08"), std::string::npos);
  EXPECT_NE(text.str().find("INCLUDEABUTTED"), std::string::npos);
  EXPECT_NE(text.str().find("SPACINGTABLE ORTHOGONAL"), std::string::npos);
  EXPECT_NE(text.str().find("CENTERTOCENTER SAMENET AREA 0.02"), std::string::npos);
  EXPECT_NE(text.str().find("SAMENET LAYER V0 STACK"), std::string::npos);
  EXPECT_NE(text.str().find("EXCEPTSAMEPGNET"), std::string::npos);
  EXPECT_NE(text.str().find("ENDOFLINE 0.14 WITHIN 0.15 PARALLELEDGE 0.16 WITHIN 0.17 TWOEDGES"), std::string::npos);
  EXPECT_NE(text.str().find("LEF58_CORNERSPACING"), std::string::npos);
  EXPECT_NE(text.str().find("MAXVIASTACK 2 RANGE M1 M2"), std::string::npos);

  const auto exported_path = newPath("roundtrip");
  ASSERT_NO_THROW(LefTechExporter::write(exported_path, source));

  TechStore roundtripped;
  LefTechImporter roundtrip_importer(roundtripped);
  ASSERT_NO_THROW(roundtrip_importer.import(exported_path));

  ASSERT_TRUE(roundtripped.globalStorage().hasUnits());
  EXPECT_EQ(roundtripped.globalStorage().getUnits().flags, source.globalStorage().getUnits().flags);
  EXPECT_EQ(roundtripped.globalStorage().getUnits().database_units_per_micron, 1000);
  ASSERT_TRUE(roundtripped.globalStorage().hasManufacturingGrid());
  EXPECT_EQ(roundtripped.globalStorage().getManufacturingGrid().value, 5);
  ASSERT_TRUE(roundtripped.globalStorage().hasMaxViaStack());
  EXPECT_EQ(roundtripped.globalStorage().getMaxViaStack().max_stack_count, 2u);

  EXPECT_EQ(roundtripped.layerSequence().size(), source.layerSequence().size());
  EXPECT_EQ(roundtripped.findLayer("OVERLAP").entity() != entt::null, true);

  const auto roundtrip_m1 = TechRoutingLayerId{roundtripped.findLayer("M1").entity()};
  const auto& routing = roundtripped.routingLayerStorage().routingLayer(roundtrip_m1);
  EXPECT_EQ(routing.direction, TechRoutingDirection::kHorizontal);
  EXPECT_EQ(routing.width, 100);
  EXPECT_EQ(routing.pitch_x, 200);
  EXPECT_EQ(routing.pitch_y, 300);
  const auto& m1_info = roundtripped.layerInfo(TechLayerId{roundtrip_m1.entity()});
  EXPECT_EQ(m1_info.lef58_type, TechLef58LayerType::kPolyRouting);
  EXPECT_NE(m1_info.flags & TechLayerInfoFlag::kLef58Backside, 0u);
  EXPECT_EQ(roundtripped.routingLayerStorage().spacingRules(roundtrip_m1).size(), 2u);
  EXPECT_EQ(roundtripped.routingLayerStorage().spacingNotchLengthRules(roundtrip_m1).size(), 1u);
  EXPECT_EQ(roundtripped.routingLayerStorage().endOfLineSpacingRules(roundtrip_m1).size(), 1u);
  EXPECT_EQ(roundtripped.routingLayerStorage().lef58CornerSpacingRules(roundtrip_m1).size(), 1u);
  EXPECT_EQ(roundtripped.routingLayerStorage().minEncloseAreaRules(roundtrip_m1).size(), 2u);
  EXPECT_EQ(roundtripped.routingLayerStorage().minStepRules(roundtrip_m1).size(), 2u);
  EXPECT_EQ(roundtripped.routingLayerStorage().minimumCutRules(roundtrip_m1).size(), 1u);
  ASSERT_EQ(roundtripped.routingLayerStorage().currentDensityRules(roundtrip_m1).size(), 1u);
  EXPECT_DOUBLE_EQ(
      roundtripped.routingLayerStorage().rule(roundtripped.routingLayerStorage().currentDensityRules(roundtrip_m1).front()).scalar, 5.5);

  const auto roundtrip_v1 = TechCutLayerId{roundtripped.findLayer("V1").entity()};
  ASSERT_EQ(roundtripped.cutLayerStorage().spacingRules(roundtrip_v1).size(), 8u);
  EXPECT_NE(roundtripped.cutLayerStorage().spacingRule(roundtripped.cutLayerStorage().spacingRules(roundtrip_v1)[1]).flags
                & TechCutSpacingRuleFlag::kSameNet,
            0u);
  EXPECT_NE(roundtripped.cutLayerStorage().spacingRule(roundtripped.cutLayerStorage().spacingRules(roundtrip_v1)[3]).flags
                & TechCutSpacingRuleFlag::kCenterToCenter,
            0u);
  EXPECT_EQ(roundtripped.cutLayerStorage().spacingRule(roundtripped.cutLayerStorage().spacingRules(roundtrip_v1)[3]).cut_area, 20000);
  const auto& cut_storage = roundtripped.cutLayerStorage();
  const auto native_enclosures = cut_storage.enclosureRules(roundtrip_v1);
  ASSERT_EQ(native_enclosures.size(), 4u);
  EXPECT_EQ(cut_storage.enclosureRule(native_enclosures[0]).side, CutLayerSide::kUnknown);
  EXPECT_EQ(cut_storage.enclosureRule(native_enclosures[0]).min_length, 7);
  EXPECT_EQ(cut_storage.enclosureRule(native_enclosures[2]).min_width, 80);
  EXPECT_EQ(cut_storage.enclosureRule(native_enclosures[2]).cut_within, 90);
  const auto array = roundtripped.cutLayerStorage().arraySpacingRule(roundtrip_v1);
  ASSERT_TRUE(array);
  EXPECT_NE(cut_storage.arraySpacingRule(array).flags & TechCutArraySpacingRuleFlag::kHasViaWidth, 0u);
  EXPECT_EQ(cut_storage.arraySpacingRule(array).via_width, 80);
  EXPECT_EQ(cut_storage.arraySpacingRule(array).items.size(), 2u);
  const auto orthogonal = cut_storage.orthogonalSpacingTableRules(roundtrip_v1);
  ASSERT_EQ(orthogonal.size(), 2u);
  EXPECT_EQ(cut_storage.orthogonalSpacingTableRule(orthogonal[0]).flags, 0u);
  EXPECT_NE(cut_storage.orthogonalSpacingTableRule(orthogonal[1]).flags
                & TechCutOrthogonalSpacingTableRuleFlag::kLef58Property,
            0u);
  const auto lef58_enclosures = cut_storage.lef58EnclosureRules(roundtrip_v1);
  ASSERT_EQ(lef58_enclosures.size(), 1u);
  const auto& lef58_enclosure = cut_storage.lef58EnclosureRule(lef58_enclosures.front());
  EXPECT_NE(lef58_enclosure.flags & TechCutLef58EnclosureRuleFlag::kIncludeAbutted, 0u);
  EXPECT_NE(lef58_enclosure.flags & TechCutLef58EnclosureRuleFlag::kNoSharedEdge, 0u);
  const auto& v1_info = roundtripped.layerInfo(TechLayerId{roundtrip_v1.entity()});
  EXPECT_EQ(v1_info.lef58_type, TechLef58LayerType::kSpecialCut);
  EXPECT_NE(v1_info.flags & TechLayerInfoFlag::kLef58Backside, 0u);
  ASSERT_EQ(roundtripped.cutLayerStorage().currentDensityRules(roundtrip_v1).size(), 1u);
  EXPECT_DOUBLE_EQ(
      roundtripped.cutLayerStorage().currentDensityRule(roundtripped.cutLayerStorage().currentDensityRules(roundtrip_v1).front()).scalar,
      0.40);

  const auto nimp = TechImplantLayerId{roundtripped.findLayer("NIMP").entity()};
  const auto implant_spacing = roundtripped.implantLayerStorage().spacingRules(nimp);
  ASSERT_EQ(implant_spacing.size(), 1u);
  EXPECT_EQ(implant_spacing.front().other_layer, TechImplantLayerId{roundtripped.findLayer("PIMP").entity()});

  const auto nw = TechMastersliceLayerId{roundtripped.findLayer("NW").entity()};
  EXPECT_EQ(roundtripped.mastersliceLayerStorage().mastersliceLayer(nw).subtype, TechMastersliceType::kNWell);
}

TEST_F(LefTechExporterTest, SynthesizesMaterializedCutPropertiesWithoutRawText)
{
  TechStore source;
  source.globalStorage().setUnits(TechGlobalUnits{.flags = TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron,
                                                  .database_units_per_micron = 1000});
  const auto cut = source.createCutLayer(
      TechLayerInfo{.name = "V1",
                    .flags = TechLayerInfoFlag::kLef58Backside,
                    .lef58_type = TechLef58LayerType::kSpecialCut},
      TechCutLayer{.flags = TechCutLayerFlag::kHasWidth, .width = 50});

  TechCutLef58EnclosureRule enclosure;
  enclosure.flags = TechCutLef58EnclosureRuleFlag::kHasOverhang1 | TechCutLef58EnclosureRuleFlag::kHasOverhang2
                    | TechCutLef58EnclosureRuleFlag::kHasMinWidth | TechCutLef58EnclosureRuleFlag::kIncludeAbutted
                    | TechCutLef58EnclosureRuleFlag::kExceptExtraCut | TechCutLef58EnclosureRuleFlag::kHasCutWithin
                    | TechCutLef58EnclosureRuleFlag::kNoSharedEdge;
  enclosure.cutclass_name = "C1";
  enclosure.side = CutLayerSide::kAbove;
  enclosure.overhang1 = 110;
  enclosure.overhang2 = 120;
  enclosure.min_width = 130;
  enclosure.cut_within = 140;
  static_cast<void>(source.cutLayerStorage().addLef58EnclosureRule(cut, std::move(enclosure)));

  TechCutOrthogonalSpacingTableRule orthogonal{.flags = TechCutOrthogonalSpacingTableRuleFlag::kLef58Property};
  orthogonal.items = {{.within = 210, .spacing = 220}, {.within = 230, .spacing = 240}};
  static_cast<void>(source.cutLayerStorage().addOrthogonalSpacingTableRule(cut, std::move(orthogonal)));

  TechCutLef58SpacingTableRule table;
  table.flags = TechCutLef58SpacingTableRuleFlag::kHasDefault | TechCutLef58SpacingTableRuleFlag::kSameMask
                | TechCutLef58SpacingTableRuleFlag::kSameNet | TechCutLef58SpacingTableRuleFlag::kHasSecondLayer
                | TechCutLef58SpacingTableRuleFlag::kNoStack | TechCutLef58SpacingTableRuleFlag::kPrlForAlignedCut
                | TechCutLef58SpacingTableRuleFlag::kHasPrl | TechCutLef58SpacingTableRuleFlag::kMaxXY;
  table.default_spacing = 120;
  table.second_layer_name = "V1";
  table.prl = 140;
  table.cutclass1_names = {"C1", "C1", "C2"};
  table.cutclass1_edges = {CutClassEdge::kSide, CutClassEdge::kEnd, CutClassEdge::kUnspecified};
  table.cutclass2_names = {"C3", "C4"};
  table.cutclass2_edges = {CutClassEdge::kUnspecified, CutClassEdge::kUnspecified};
  table.prl_for_aligned_cut = {{.from = "C1", .to = "C2"}, {.from = "C3", .to = "C4"}};
  table.prl_entries = {{.from = "C1", .to = "C2", .prl = 300}};
  table.cells = {{.cut_spacing1 = 100, .cut_spacing2 = 200, .has_cut_spacing1 = true, .has_cut_spacing2 = true},
                 {.cut_spacing1 = 300, .has_cut_spacing1 = true},
                 {.cut_spacing1 = 400, .cut_spacing2 = 500, .has_cut_spacing1 = true, .has_cut_spacing2 = true},
                 {.cut_spacing1 = 600, .has_cut_spacing1 = true},
                 {.cut_spacing1 = 700, .has_cut_spacing1 = true},
                 {.cut_spacing1 = 800, .cut_spacing2 = 900, .has_cut_spacing1 = true, .has_cut_spacing2 = true}};
  static_cast<void>(source.cutLayerStorage().addLef58SpacingTableRule(cut, std::move(table)));

  std::ostringstream text;
  ASSERT_NO_THROW(LefTechExporter::write(text, source));
  EXPECT_NE(text.str().find("PROPERTY LEF58_TYPE \"TYPE SPECIALCUT ;\""), std::string::npos);
  EXPECT_NE(text.str().find("PROPERTY LEF58_BACKSIDE \"BACKSIDE ;\""), std::string::npos);
  EXPECT_NE(text.str().find("INCLUDEABUTTED EXCEPTEXTRACUT 0.14 NOSHAREDEDGE"), std::string::npos);
  EXPECT_NE(text.str().find("SPACINGTABLE ORTHOGONAL WITHIN 0.21 SPACING 0.22"), std::string::npos);
  EXPECT_NE(text.str().find("SPACINGTABLE DEFAULT 0.12 SAMEMASK SAMENET LAYER V1 NOSTACK PRLFORALIGNEDCUT"), std::string::npos);

  const auto exported_path = newPath("synthesized-properties");
  ASSERT_NO_THROW(LefTechExporter::write(exported_path, source));
  TechStore roundtripped;
  LefTechImporter importer(roundtripped);
  ASSERT_NO_THROW(importer.import(exported_path));
  const auto restored = TechCutLayerId{roundtripped.findLayer("V1").entity()};
  ASSERT_EQ(roundtripped.cutLayerStorage().lef58EnclosureRules(restored).size(), 1u);
  EXPECT_NE(roundtripped.cutLayerStorage()
                .lef58EnclosureRule(roundtripped.cutLayerStorage().lef58EnclosureRules(restored).front())
                .flags
                & TechCutLef58EnclosureRuleFlag::kIncludeAbutted,
            0u);
  ASSERT_EQ(roundtripped.cutLayerStorage().orthogonalSpacingTableRules(restored).size(), 1u);
  const auto restored_tables = roundtripped.cutLayerStorage().lef58SpacingTableRules(restored);
  ASSERT_EQ(restored_tables.size(), 1u);
  const auto& restored_table = roundtripped.cutLayerStorage().lef58SpacingTableRule(restored_tables.front());
  EXPECT_EQ(restored_table.flags, TechCutLef58SpacingTableRuleFlag::kHasDefault | TechCutLef58SpacingTableRuleFlag::kSameMask
                                      | TechCutLef58SpacingTableRuleFlag::kSameNet
                                      | TechCutLef58SpacingTableRuleFlag::kHasSecondLayer
                                      | TechCutLef58SpacingTableRuleFlag::kNoStack
                                      | TechCutLef58SpacingTableRuleFlag::kPrlForAlignedCut
                                      | TechCutLef58SpacingTableRuleFlag::kHasPrl
                                      | TechCutLef58SpacingTableRuleFlag::kMaxXY);
  EXPECT_EQ(restored_table.cutclass1_edges,
            (std::vector<CutClassEdge>{CutClassEdge::kSide, CutClassEdge::kEnd, CutClassEdge::kUnspecified}));
  EXPECT_EQ(restored_table.cells.size(), 6u);
}

TEST_F(LefTechExporterTest, RoundTripsNativeSpacingTablesAndCurrentDensityTables)
{
  const auto source_path = writeFile("tables", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  SPACINGTABLE
    PARALLELRUNLENGTH 0.00 0.50
      WIDTH 0.00 0.10 0.20
      WIDTH 0.30 0.20 0.30 ;
  SPACINGTABLE
    INFLUENCE
      WIDTH 0.10 WITHIN 0.20 SPACING 0.30
      WIDTH 0.40 WITHIN 0.50 SPACING 0.60 ;
  ACCURRENTDENSITY PEAK
    FREQUENCY 1 2 ;
    TABLEENTRIES 10 20 ;
  ACCURRENTDENSITY RMS
    FREQUENCY 1 2 ;
    WIDTH 0.10 0.20 ;
    TABLEENTRIES 1 2 3 4 ;
  DCCURRENTDENSITY AVERAGE 5.5 ;
END M1
LAYER V1
  TYPE CUT ;
  WIDTH 0.10 ;
  ACCURRENTDENSITY PEAK
    FREQUENCY 1 2 ;
    TABLEENTRIES 10 20 ;
  ACCURRENTDENSITY RMS
    FREQUENCY 1 2 ;
    CUTAREA 0.01 0.02 ;
    TABLEENTRIES 1 2 3 4 ;
  DCCURRENTDENSITY AVERAGE 0.40 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.30 ;
  WIDTH 0.12 ;
  SPACINGTABLE
    TWOWIDTHS
      WIDTH 0.00 0.10 0.20
      WIDTH 0.20 PRL 0.30 0.20 0.30 ;
END M2
END LIBRARY
)LEF");

  TechStore source;
  LefTechImporter source_importer(source);
  ASSERT_NO_THROW(source_importer.import(source_path));

  const auto exported_path = newPath("tables-roundtrip");
  ASSERT_NO_THROW(LefTechExporter::write(exported_path, source));

  TechStore roundtripped;
  LefTechImporter roundtrip_importer(roundtripped);
  ASSERT_NO_THROW(roundtrip_importer.import(exported_path));

  const auto m1 = TechRoutingLayerId{roundtripped.findLayer("M1").entity()};
  const auto& routing = roundtripped.routingLayerStorage();
  const auto prl = routing.prlSpacingTableRules(m1);
  ASSERT_EQ(prl.size(), 1u);
  EXPECT_EQ(routing.prlSpacingTableCell(prl.front(), 1, 1), 300);
  const auto influence = routing.influenceSpacingTableRules(m1);
  ASSERT_EQ(influence.size(), 1u);
  EXPECT_EQ(routing.influenceSpacingTableEntries(influence.front())[1].spacing, 600);
  const auto routing_density = routing.currentDensityRules(m1);
  ASSERT_EQ(routing_density.size(), 3u);
  EXPECT_DOUBLE_EQ(routing.currentDensityTableEntry(routing_density[0], 1, 0), 20.0);
  EXPECT_DOUBLE_EQ(routing.currentDensityTableEntry(routing_density[1], 1, 1), 4.0);

  const auto m2 = TechRoutingLayerId{roundtripped.findLayer("M2").entity()};
  const auto two_widths = routing.twoWidthsSpacingTableRules(m2);
  ASSERT_EQ(two_widths.size(), 1u);
  EXPECT_EQ(routing.twoWidthsSpacingTableCell(two_widths.front(), 1, 1), 300);

  const auto v1 = TechCutLayerId{roundtripped.findLayer("V1").entity()};
  const auto& cut = roundtripped.cutLayerStorage();
  const auto cut_density = cut.currentDensityRules(v1);
  ASSERT_EQ(cut_density.size(), 3u);
  EXPECT_DOUBLE_EQ(cut.currentDensityTableEntry(cut_density[0], 1, 0), 20.0);
  EXPECT_DOUBLE_EQ(cut.currentDensityTableEntry(cut_density[1], 1, 1), 4.0);
}

TEST_F(LefTechExporterTest, RoundTripsViasViaRulesAndNonDefaultRules)
{
  const auto source_path = writeFile("tech-objects", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
PROPERTYDEFINITIONS
  VIA USER STRING ;
  VIARULE USER STRING ;
  NONDEFAULTRULE USER STRING ;
END PROPERTYDEFINITIONS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
LAYER V1 TYPE CUT ; WIDTH 0.10 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.30 ; WIDTH 0.12 ; END M2
VIA VFIX DEFAULT
  RESISTANCE 2.5 ;
  LAYER V1 ; RECT -0.05 -0.05 0.05 0.05 ; POLYGON ( -0.05 -0.05 ) ( 0.05 -0.05 ) ( 0.05 0.05 ) ( -0.05 0.05 ) ;
  LAYER M2 ; RECT -0.08 -0.07 0.08 0.07 ;
  LAYER M1 ; RECT -0.07 -0.08 0.07 0.08 ;
  PROPERTY USER "fixed" ;
END VFIX
VIARULE VGEN_RULE GENERATE DEFAULT
  LAYER M1 ; ENCLOSURE 0.05 0.06 ; WIDTH 0.10 TO 1.00 ;
  LAYER M2 ; ENCLOSURE 0.07 0.08 ;
  LAYER V1 ; RECT -0.05 -0.05 0.05 0.05 ; SPACING 0.05 BY 0.05 ; RESISTANCE 3.0 ;
  PROPERTY USER "generate" ;
END VGEN_RULE
VIA VGENERATED DEFAULT
  VIARULE VGEN_RULE ;
  CUTSIZE 0.10 0.10 ;
  LAYERS M1 V1 M2 ;
  CUTSPACING 0.05 0.05 ;
  ENCLOSURE 0.05 0.06 0.07 0.08 ;
  ROWCOL 2 3 ;
  ORIGIN 0.01 -0.02 ;
  OFFSET 0.005 0.006 -0.007 -0.008 ;
  PATTERN staggered ;
END VGENERATED
VIARULE ORDINARY
  LAYER M2 ; DIRECTION VERTICAL ; WIDTH 0.12 TO 0.50 ;
  LAYER M1 ; DIRECTION HORIZONTAL ;
  VIA VFIX ;
  PROPERTY USER "ordinary" ;
END ORDINARY
NONDEFAULTRULE WIDE
  HARDSPACING ;
  LAYER M1
    WIDTH 0.20 ;
    DIAGWIDTH 0.22 ;
    SPACING 0.25 ;
    WIREEXTENSION 0.03 ;
  END M1
  VIA NDR_LOCAL
    LAYER M1 ; RECT -0.06 -0.07 0.06 0.07 ;
    LAYER V1 ; RECT -0.04 -0.04 0.04 0.04 ; POLYGON ( -0.03 -0.03 ) ( 0.03 -0.03 ) ( 0.03 0.03 ) ( -0.03 0.03 ) ;
    LAYER M2 ; RECT -0.07 -0.06 0.07 0.06 ;
  END NDR_LOCAL
  VIA NDR_GENERATED
    VIARULE VGEN_RULE ;
    CUTSIZE 0.10 0.10 ;
    LAYERS M1 V1 M2 ;
    CUTSPACING 0.05 0.05 ;
    ENCLOSURE 0.05 0.06 0.07 0.08 ;
    ROWCOL 1 2 ;
  END NDR_GENERATED
  USEVIA VFIX ;
  USEVIARULE VGEN_RULE ;
  MINCUTS V1 2 ;
  PROPERTY USER "ndr" ;
END WIDE
END LIBRARY
)LEF");

  TechStore source;
  ASSERT_NO_THROW(LefTechImporter(source).import(source_path));

  std::ostringstream text;
  ASSERT_NO_THROW(LefTechExporter::write(text, source));
  EXPECT_NE(text.str().find("VIARULE VGEN_RULE GENERATE DEFAULT"), std::string::npos);
  EXPECT_NE(text.str().find("VIA VFIX DEFAULT"), std::string::npos);
  EXPECT_NE(text.str().find("ROWCOL 2 3"), std::string::npos);
  EXPECT_NE(text.str().find("VIARULE ORDINARY"), std::string::npos);
  EXPECT_NE(text.str().find("NONDEFAULTRULE WIDE"), std::string::npos);

  const auto exported_path = newPath("tech-objects-roundtrip");
  ASSERT_NO_THROW(LefTechExporter::write(exported_path, source));

  TechStore roundtripped;
  ASSERT_NO_THROW(LefTechImporter(roundtripped).import(exported_path));

  const auto fixed = roundtripped.viaMasterStorage().findViaMaster("VFIX");
  ASSERT_TRUE(fixed);
  EXPECT_TRUE(roundtripped.viaMasterStorage().hasFixedViaMaster(fixed));
  EXPECT_EQ(roundtripped.viaMasterStorage().cutPolygonCount(fixed), 1u);

  const auto generate_rule = roundtripped.viaRuleGenerateStorage().findViaRuleGenerate("VGEN_RULE");
  ASSERT_TRUE(generate_rule);
  EXPECT_TRUE(roundtripped.viaRuleGenerateStorage().viaRuleGenerate(generate_rule).isDefault());

  const auto generated = roundtripped.viaMasterStorage().findViaMaster("VGENERATED");
  ASSERT_TRUE(generated);
  ASSERT_TRUE(roundtripped.viaMasterStorage().hasGeneratedViaMaster(generated));
  const auto& formula = roundtripped.viaMasterStorage().generatedViaMaster(generated);
  EXPECT_EQ(formula.via_rule_generate, generate_rule);
  EXPECT_EQ(formula.row_count, 2u);
  EXPECT_EQ(formula.column_count, 3u);
  EXPECT_EQ(formula.origin_x, 10);
  EXPECT_EQ(formula.origin_y, -20);
  EXPECT_EQ(formula.bottom_offset_x, 5);
  EXPECT_EQ(formula.top_offset_y, -8);
  EXPECT_EQ(formula.pattern, "staggered");

  const auto ordinary = roundtripped.viaRuleStorage().findViaRule("ORDINARY");
  ASSERT_TRUE(ordinary);
  EXPECT_EQ(roundtripped.viaRuleStorage().candidates(ordinary).size(), 1u);
  EXPECT_EQ(roundtripped.viaRuleStorage().candidates(ordinary).front(), fixed);

  const auto ndr = roundtripped.nonDefaultRuleStorage().findNonDefaultRule("WIDE");
  ASSERT_TRUE(ndr);
  EXPECT_TRUE(roundtripped.nonDefaultRuleStorage().nonDefaultRule(ndr).isHardSpacing());
  ASSERT_EQ(roundtripped.nonDefaultRuleStorage().routingRules(ndr).size(), 1u);
  const auto& ndr_routing = roundtripped.nonDefaultRuleStorage().routingRules(ndr).front();
  EXPECT_EQ(ndr_routing.width, 200);
  EXPECT_NE(ndr_routing.flags & TechNdrRoutingRuleFlag::kHasDiagWidth, 0u);
  EXPECT_NE(ndr_routing.flags & TechNdrRoutingRuleFlag::kHasSpacing, 0u);
  EXPECT_NE(ndr_routing.flags & TechNdrRoutingRuleFlag::kHasWireExtension, 0u);
  EXPECT_EQ(roundtripped.nonDefaultRuleStorage().useVias(ndr).size(), 1u);
  EXPECT_EQ(roundtripped.nonDefaultRuleStorage().useViaRules(ndr).size(), 1u);
  EXPECT_EQ(roundtripped.nonDefaultRuleStorage().minCutsRules(ndr).size(), 1u);
  const auto local_vias = roundtripped.nonDefaultRuleStorage().viaDefinitions(ndr);
  ASSERT_EQ(local_vias.size(), 2u);
  EXPECT_EQ(roundtripped.nonDefaultRuleStorage().viaDefinition(local_vias[0]).name, "NDR_LOCAL");
  EXPECT_EQ(
      roundtripped.geometryPool().polygonCount(roundtripped.nonDefaultRuleStorage().viaDefinitionGeometry(local_vias[0]).cut_geometry), 1u);
  EXPECT_EQ(roundtripped.nonDefaultRuleStorage().viaDefinition(local_vias[1]).name, "NDR_GENERATED");
  const auto* local_generated = roundtripped.nonDefaultRuleStorage().generatedViaDefinition(local_vias[1]);
  ASSERT_NE(local_generated, nullptr);
  EXPECT_EQ(local_generated->via_rule_generate, generate_rule);
  EXPECT_EQ(local_generated->row_count, 1u);
  EXPECT_EQ(local_generated->column_count, 2u);
}

TEST_F(LefTechExporterTest, RejectsLegacyNonDefaultRuleSameNetSpacing)
{
  const auto source_path = writeFile("legacy-ndr-spacing", R"LEF(
VERSION 5.5 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
LAYER V1 TYPE CUT ; WIDTH 0.10 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.30 ; WIDTH 0.12 ; END M2
NONDEFAULTRULE LEGACY
  LAYER M1
    WIDTH 0.20 ;
    SPACING 0.25 ;
  END M1
  VIA NDR_LOCAL
    LAYER M1 ; RECT -0.06 -0.07 0.06 0.07 ;
    LAYER V1 ; RECT -0.04 -0.04 0.04 0.04 ;
    LAYER M2 ; RECT -0.07 -0.06 0.07 0.06 ;
  END NDR_LOCAL
  SPACING
    SAMENET M1 M2 0.30 STACK ;
  END SPACING
END LEGACY
END LIBRARY
)LEF");

  TechStore database;
  ASSERT_NO_THROW(LefTechImporter(database).import(source_path));
  std::ostringstream output;
  EXPECT_THROW(LefTechExporter::write(output, database), std::logic_error);
}

TEST_F(LefTechExporterTest, RoundTripsSky130TechnologyLef)
{
  const auto source_path = std::filesystem::path{ECC_TOOLS_SOURCE_DIR} / "scripts/foundry/sky130/lef/sky130_fd_sc_hd.tlef";
  ASSERT_TRUE(std::filesystem::exists(source_path));

  TechStore source;
  ASSERT_NO_THROW(LefTechImporter(source).import(source_path));

  const auto exported_path = newPath("sky130-tech-roundtrip");
  ASSERT_NO_THROW(LefTechExporter::write(exported_path, source));

  TechStore roundtripped;
  ASSERT_NO_THROW(LefTechImporter(roundtripped).import(exported_path));

  ASSERT_TRUE(roundtripped.globalStorage().hasUnits());
  EXPECT_EQ(roundtripped.globalStorage().getUnits().database_units_per_micron, source.globalStorage().getUnits().database_units_per_micron);
  ASSERT_TRUE(roundtripped.globalStorage().hasManufacturingGrid());
  EXPECT_EQ(roundtripped.globalStorage().getManufacturingGrid().value, source.globalStorage().getManufacturingGrid().value);

  ASSERT_EQ(roundtripped.layerSequence().size(), source.layerSequence().size());
  const auto& source_registry = source.techRegistry().registry();
  const auto& roundtrip_registry = roundtripped.techRegistry().registry();
  for (size_t index = 0; index < source.layerSequence().size(); ++index) {
    const auto source_id = source.layerSequence()[index];
    const auto roundtrip_id = roundtripped.layerSequence()[index];
    EXPECT_EQ(roundtripped.layerInfo(roundtrip_id).name, source.layerInfo(source_id).name) << index;
    EXPECT_EQ(roundtripped.layerInfo(roundtrip_id).mask_count, source.layerInfo(source_id).mask_count) << index;
    EXPECT_EQ(roundtrip_registry.all_of<TechRoutingLayer>(roundtrip_id.entity()),
              source_registry.all_of<TechRoutingLayer>(source_id.entity()))
        << source.layerInfo(source_id).name;
    EXPECT_EQ(roundtrip_registry.all_of<TechCutLayer>(roundtrip_id.entity()), source_registry.all_of<TechCutLayer>(source_id.entity()))
        << source.layerInfo(source_id).name;
    EXPECT_EQ(roundtrip_registry.all_of<TechMastersliceLayer>(roundtrip_id.entity()),
              source_registry.all_of<TechMastersliceLayer>(source_id.entity()))
        << source.layerInfo(source_id).name;
  }

  EXPECT_EQ(roundtripped.viaMasterStorage().viaMasters().size(), source.viaMasterStorage().viaMasters().size());
  EXPECT_EQ(roundtripped.viaRuleGenerateStorage().viaRuleGenerates().size(), source.viaRuleGenerateStorage().viaRuleGenerates().size());
  EXPECT_EQ(roundtripped.viaRuleStorage().viaRules().size(), source.viaRuleStorage().viaRules().size());
  EXPECT_EQ(roundtripped.nonDefaultRuleStorage().nonDefaultRules().size(), source.nonDefaultRuleStorage().nonDefaultRules().size());

  const auto source_met1 = TechRoutingLayerId{source.findLayer("met1").entity()};
  const auto roundtrip_met1 = TechRoutingLayerId{roundtripped.findLayer("met1").entity()};
  const auto& source_met1_rules = source.routingLayerStorage();
  const auto& roundtrip_met1_rules = roundtripped.routingLayerStorage();
  EXPECT_EQ(roundtrip_met1_rules.prlSpacingTableRules(roundtrip_met1).size(), source_met1_rules.prlSpacingTableRules(source_met1).size());
  EXPECT_EQ(roundtrip_met1_rules.currentDensityRules(roundtrip_met1).size(), source_met1_rules.currentDensityRules(source_met1).size());

  const auto source_via = source.viaMasterStorage().findViaMaster("M1M2_PR");
  const auto roundtrip_via = roundtripped.viaMasterStorage().findViaMaster("M1M2_PR");
  ASSERT_TRUE(source_via);
  ASSERT_TRUE(roundtrip_via);
  EXPECT_EQ(roundtripped.viaMasterStorage().shapeCount(roundtrip_via), source.viaMasterStorage().shapeCount(source_via));

  const auto source_rule = source.viaRuleGenerateStorage().findViaRuleGenerate("M1M2_PR");
  const auto roundtrip_rule = roundtripped.viaRuleGenerateStorage().findViaRuleGenerate("M1M2_PR");
  ASSERT_TRUE(source_rule);
  ASSERT_TRUE(roundtrip_rule);
  EXPECT_EQ(roundtripped.viaRuleGenerateStorage().bottomLayer(roundtrip_rule).enclosure_overhang1,
            source.viaRuleGenerateStorage().bottomLayer(source_rule).enclosure_overhang1);
  EXPECT_EQ(roundtripped.viaRuleGenerateStorage().topLayer(roundtrip_rule).enclosure_overhang2,
            source.viaRuleGenerateStorage().topLayer(source_rule).enclosure_overhang2);
}

TEST_F(LefTechExporterTest, RoundTripsIhp130TechnologyLef)
{
  const auto source_path
      = std::filesystem::path{ECC_TOOLS_SOURCE_DIR} / "scripts/foundry/ihp130/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef";
  ASSERT_TRUE(std::filesystem::exists(source_path));

  TechStore source;
  ASSERT_NO_THROW(LefTechImporter(source).import(source_path));

  const auto exported_path = newPath("ihp130-tech-roundtrip");
  ASSERT_NO_THROW(LefTechExporter::write(exported_path, source));

  TechStore roundtripped;
  ASSERT_NO_THROW(LefTechImporter(roundtripped).import(exported_path));

  EXPECT_EQ(roundtripped.layerSequence().size(), source.layerSequence().size());
  EXPECT_EQ(roundtripped.viaMasterStorage().viaMasters().size(), source.viaMasterStorage().viaMasters().size());
  EXPECT_EQ(roundtripped.viaRuleGenerateStorage().viaRuleGenerates().size(), source.viaRuleGenerateStorage().viaRuleGenerates().size());

  const auto source_metal1 = TechRoutingLayerId{source.findLayer("Metal1").entity()};
  const auto roundtrip_metal1 = TechRoutingLayerId{roundtripped.findLayer("Metal1").entity()};
  const auto& source_routing = source.routingLayerStorage();
  const auto& roundtrip_routing = roundtripped.routingLayerStorage();
  EXPECT_EQ(roundtrip_routing.prlSpacingTableRules(roundtrip_metal1).size(), source_routing.prlSpacingTableRules(source_metal1).size());
  EXPECT_EQ(roundtrip_routing.minimumCutRules(roundtrip_metal1).size(), source_routing.minimumCutRules(source_metal1).size());
  EXPECT_EQ(roundtrip_routing.currentDensityRules(roundtrip_metal1).size(), source_routing.currentDensityRules(source_metal1).size());

  const auto source_via1 = TechCutLayerId{source.findLayer("Via1").entity()};
  const auto roundtrip_via1 = TechCutLayerId{roundtripped.findLayer("Via1").entity()};
  const auto& source_cut = source.cutLayerStorage();
  const auto& roundtrip_cut = roundtripped.cutLayerStorage();
  EXPECT_EQ(roundtrip_cut.spacingRules(roundtrip_via1).size(), source_cut.spacingRules(source_via1).size());
  EXPECT_EQ(roundtrip_cut.enclosureRules(roundtrip_via1).size(), source_cut.enclosureRules(source_via1).size());

  const auto source_fixed = source.viaMasterStorage().findViaMaster("Via1_XX_so");
  const auto roundtrip_fixed = roundtripped.viaMasterStorage().findViaMaster("Via1_XX_so");
  ASSERT_TRUE(source_fixed);
  ASSERT_TRUE(roundtrip_fixed);
  EXPECT_EQ(roundtripped.viaMasterStorage().shapeCount(roundtrip_fixed), source.viaMasterStorage().shapeCount(source_fixed));
}

TEST(LefTechExporter, RejectsDatabaseWithoutUnits)
{
  TechStore database;
  std::ostringstream output;
  EXPECT_THROW(LefTechExporter::write(output, database), std::logic_error);
}

}  // namespace
}  // namespace eccdb
