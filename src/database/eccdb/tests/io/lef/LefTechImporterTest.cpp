// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "lef/LefTechImporter.h"

namespace eccdb {
namespace {

class LefTechImporterTest : public testing::Test
{
 protected:
  std::filesystem::path writeLef(std::string_view stem, std::string_view contents)
  {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
                      / ("idb-refactor-" + std::string(stem) + "-" + std::to_string(nonce) + "-" + std::to_string(_paths.size()) + ".lef");
    std::ofstream output(path);
    if (!output) {
      throw std::runtime_error("failed to create temporary LEF test input");
    }
    output << contents;
    output.close();
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

  static size_t layerCount(const TechStore& database)
  {
    size_t count = 0;
    for ([[maybe_unused]] const auto entity : database.techRegistry().registry().view<const TechLayerInfo>()) {
      ++count;
    }
    return count;
  }

  std::vector<std::filesystem::path> _paths;
};

TEST_F(LefTechImporterTest, ImportsGlobalsFiveLayerKindsPropertiesAndImplantSpacing)
{
  const auto lef = writeLef("complete", R"LEF(
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
  PROPERTY USER "routing" ;
END M1
LAYER V1
  TYPE CUT ;
  WIDTH 0.05 ;
  RESISTANCE 4.5 ;
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

  TechStore database;
  LefTechImporter importer(database);
  ASSERT_NO_THROW(importer.import(lef));

  EXPECT_EQ(layerCount(database), 7u);
  EXPECT_EQ(database.layerSequence().size(), 6u);
  const auto& globals = database.globalStorage();
  ASSERT_TRUE(globals.hasUnits());
  EXPECT_EQ(globals.getUnits().database_units_per_micron, 1000);
  EXPECT_EQ(globals.getUnits().megahertz, 7);
  EXPECT_EQ(globals.getManufacturingGrid().value, 5);

  const auto nw = database.findLayer("NW");
  ASSERT_TRUE(nw);
  EXPECT_EQ(database.mastersliceLayerStorage().mastersliceLayer(TechMastersliceLayerId{nw.entity()}).subtype, TechMastersliceType::kNWell);

  const auto m1 = database.findLayer("M1");
  const auto m2 = database.findLayer("M2");
  ASSERT_TRUE(m1);
  ASSERT_TRUE(m2);
  const auto& routing = database.routingLayerStorage().routingLayer(TechRoutingLayerId{m1.entity()});
  EXPECT_EQ(database.layerInfo(m1).mask_count, 2u);
  EXPECT_EQ(routing.direction, TechRoutingDirection::kHorizontal);
  EXPECT_EQ(routing.pitch_form, TechRoutingAxisValueForm::kSeparateXY);
  EXPECT_EQ(routing.pitch_x, 200);
  EXPECT_EQ(routing.pitch_y, 300);
  EXPECT_EQ(routing.offset_x, 100);
  EXPECT_EQ(routing.offset_y, 150);
  EXPECT_EQ(routing.width, 100);
  EXPECT_EQ(routing.min_width, 80);
  EXPECT_EQ(routing.max_width, 1500);
  EXPECT_EQ(routing.thickness, 70);
  EXPECT_EQ(routing.area, 50000);
  EXPECT_DOUBLE_EQ(routing.resistance, 2.5);
  EXPECT_DOUBLE_EQ(routing.capacitance, 0.001);
  EXPECT_DOUBLE_EQ(routing.edge_capacitance, 0.002);
  EXPECT_DOUBLE_EQ(routing.min_density, 20.0);
  EXPECT_DOUBLE_EQ(routing.max_density, 80.0);
  EXPECT_EQ(routing.density_check_length, 1000);
  EXPECT_EQ(routing.density_check_width, 2000);
  EXPECT_EQ(routing.density_check_step, 500);
  ASSERT_EQ(database.layerProperties(m1).size(), 1u);
  EXPECT_EQ(database.layerProperties(m1).front().value, "routing");

  const auto v1 = database.findLayer("V1");
  const auto& cut = database.cutLayerStorage().cutLayer(TechCutLayerId{v1.entity()});
  EXPECT_EQ(cut.width, 50);
  EXPECT_DOUBLE_EQ(cut.resistance_per_cut, 4.5);

  const auto nimp = TechImplantLayerId{database.findLayer("NIMP").entity()};
  const auto pimp = TechImplantLayerId{database.findLayer("PIMP").entity()};
  EXPECT_EQ(database.implantLayerStorage().implantLayer(nimp).min_width, 100);
  const auto spacing = database.implantLayerStorage().spacingRules(nimp);
  ASSERT_EQ(spacing.size(), 1u);
  EXPECT_EQ(spacing.front().min_spacing, 200);
  EXPECT_EQ(spacing.front().other_layer, pimp);

  ASSERT_TRUE(globals.hasMaxViaStack());
  EXPECT_EQ(globals.getMaxViaStack().max_stack_count, 2u);
  EXPECT_EQ(globals.getMaxViaStack().bottom_layer, TechRoutingLayerId{m1.entity()});
  EXPECT_EQ(globals.getMaxViaStack().top_layer, TechRoutingLayerId{m2.entity()});
  EXPECT_TRUE(database.techRegistry().registry().all_of<TechOverlapLayer>(database.findLayer("OVERLAP").entity()));
}

TEST_F(LefTechImporterTest, PreservesAreasBeyondInt32DbuSquared)
{
  const auto lef = writeLef("int64-areas", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 20000 ; END UNITS
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  AREA 10.0 ;
  MINENCLOSEDAREA 11.0 WIDTH 0.25 ;
END M1
LAYER V1
  TYPE CUT ;
  WIDTH 0.05 ;
  DCCURRENTDENSITY AVERAGE
    CUTAREA 12.0 13.0 ;
    TABLEENTRIES 1.0 2.0 ;
END V1
END LIBRARY
)LEF");

  TechStore database;
  ASSERT_NO_THROW(LefTechImporter(database).import(lef));

  const auto routing_id = TechRoutingLayerId{database.findLayer("M1").entity()};
  EXPECT_EQ(database.routingLayerStorage().routingLayer(routing_id).area, 4'000'000'000LL);
  const auto enclosed_areas = database.routingLayerStorage().minEncloseAreaRules(routing_id);
  ASSERT_EQ(enclosed_areas.size(), 1u);
  EXPECT_EQ(database.routingLayerStorage().rule(enclosed_areas.front()).area, 4'400'000'000LL);

  const auto cut_id = TechCutLayerId{database.findLayer("V1").entity()};
  const auto density_rules = database.cutLayerStorage().currentDensityRules(cut_id);
  ASSERT_EQ(density_rules.size(), 1u);
  EXPECT_EQ(database.cutLayerStorage().currentDensityRule(density_rules.front()).cut_areas,
            (std::vector<int64_t>{4'800'000'000LL, 5'200'000'000LL}));
  EXPECT_DOUBLE_EQ(database.cutLayerStorage().currentDensityAt(density_rules.front(), 0.0, 5'000'000'000LL), 1.5);
}

TEST_F(LefTechImporterTest, MergesMultipleFilesAndResolvesCrossFileReferences)
{
  const auto first = writeLef("multi-a", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER NIMP TYPE IMPLANT ; WIDTH 0.10 ; SPACING 0.20 LAYER PIMP ; END NIMP
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
END LIBRARY
)LEF");
  const auto second = writeLef("multi-b", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER PIMP TYPE IMPLANT ; WIDTH 0.10 ; END PIMP
LAYER V1 TYPE CUT ; WIDTH 0.05 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.30 ; WIDTH 0.12 ; END M2
MAXVIASTACK 2 RANGE M1 M2 ;
END LIBRARY
)LEF");

  TechStore database;
  LefTechImporter importer(database);
  const std::array files{first, second};
  ASSERT_NO_THROW(importer.import(std::span<const std::filesystem::path>(files)));

  ASSERT_EQ(database.layerSequence().size(), 5u);
  EXPECT_EQ(database.layerInfo(database.layerSequence()[0]).name, "NIMP");
  EXPECT_EQ(database.layerInfo(database.layerSequence()[1]).name, "M1");
  EXPECT_EQ(database.layerInfo(database.layerSequence()[2]).name, "PIMP");
  const auto nimp = TechImplantLayerId{database.findLayer("NIMP").entity()};
  const auto spacing = database.implantLayerStorage().spacingRules(nimp);
  ASSERT_EQ(spacing.size(), 1u);
  EXPECT_EQ(spacing.front().other_layer, TechImplantLayerId{database.findLayer("PIMP").entity()});
}

TEST_F(LefTechImporterTest, ImportsNativeRoutingRulesTablesAndCurrentDensity)
{
  const auto lef = writeLef("routing-rules", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  SPACING 0.10 ;
  SPACING 0.20 RANGE 0.30 0.40 ;
  SPACING 0.12 NOTCHLENGTH 0.15 ;
  SPACING 0.13 ENDOFLINE 0.14 WITHIN 0.15 PARALLELEDGE 0.16 WITHIN 0.17 TWOEDGES ;
  MINENCLOSEDAREA 0.04 ;
  MINENCLOSEDAREA 0.05 WIDTH 0.25 ;
  MINSTEP 0.05 INSIDECORNER LENGTHSUM 0.20 ;
  MINSTEP 0.06 MAXEDGES 3 ;
  MINIMUMCUT 2 WIDTH 0.50 WITHIN 0.10 FROMABOVE LENGTH 0.60 WITHIN 0.20 ;
  MINIMUMCUT 3 WIDTH 0.70 FROMBELOW ;
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

  TechStore database;
  LefTechImporter importer(database);
  ASSERT_NO_THROW(importer.import(lef));

  const auto layer = TechRoutingLayerId{database.findLayer("M1").entity()};
  const auto& storage = database.routingLayerStorage();

  const auto spacing_ids = storage.spacingRules(layer);
  ASSERT_EQ(spacing_ids.size(), 2u);
  EXPECT_EQ(storage.rule(spacing_ids[0]).min_spacing, 100);
  EXPECT_EQ(storage.rule(spacing_ids[0]).min_width, -1);
  EXPECT_EQ(storage.rule(spacing_ids[0]).max_width, -1);
  EXPECT_EQ(storage.rule(spacing_ids[1]).type, TechRoutingSpacingType::kRange);
  EXPECT_EQ(storage.rule(spacing_ids[1]).min_width, 300);
  EXPECT_EQ(storage.rule(spacing_ids[1]).max_width, 400);
  const auto notch_ids = storage.spacingNotchLengthRules(layer);
  ASSERT_EQ(notch_ids.size(), 1u);
  EXPECT_EQ(storage.rule(notch_ids.front()).min_spacing, 120);
  EXPECT_EQ(storage.rule(notch_ids.front()).notch_length, 150);
  const auto eol_ids = storage.endOfLineSpacingRules(layer);
  ASSERT_EQ(eol_ids.size(), 1u);
  const auto& eol = storage.rule(eol_ids.front());
  EXPECT_EQ(eol.min_spacing, 130);
  EXPECT_EQ(eol.eol_width, 140);
  EXPECT_EQ(eol.eol_within, 150);
  EXPECT_EQ(eol.parallel_space, 160);
  EXPECT_EQ(eol.parallel_within, 170);
  EXPECT_NE(eol.flags & TechRoutingEndOfLineSpacingRuleFlag::kHasParallelEdge, 0u);
  EXPECT_NE(eol.flags & TechRoutingEndOfLineSpacingRuleFlag::kTwoEdges, 0u);

  const auto area_ids = storage.minEncloseAreaRules(layer);
  ASSERT_EQ(area_ids.size(), 2u);
  EXPECT_EQ(storage.rule(area_ids[0]).area, 40000);
  EXPECT_EQ(storage.rule(area_ids[0]).width, -1);
  EXPECT_EQ(storage.rule(area_ids[1]).area, 50000);
  EXPECT_EQ(storage.rule(area_ids[1]).width, 250);

  const auto min_step_ids = storage.minStepRules(layer);
  ASSERT_EQ(min_step_ids.size(), 2u);
  EXPECT_EQ(storage.rule(min_step_ids[0]).type, TechRoutingMinStepType::kInsideCorner);
  EXPECT_EQ(storage.rule(min_step_ids[0]).max_length_sum, 200);
  EXPECT_NE(storage.rule(min_step_ids[0]).flags & TechRoutingMinStepRuleFlag::kHasMaxLengthSum, 0u);
  EXPECT_EQ(storage.rule(min_step_ids[1]).max_edges, 3);

  const auto minimum_cut_ids = storage.minimumCutRules(layer);
  ASSERT_EQ(minimum_cut_ids.size(), 2u);
  const auto& first_minimum_cut = storage.rule(minimum_cut_ids[0]);
  EXPECT_EQ(first_minimum_cut.num_cuts, 2);
  EXPECT_EQ(first_minimum_cut.width, 500);
  EXPECT_EQ(first_minimum_cut.within_cut_distance, 100);
  EXPECT_EQ(first_minimum_cut.orient, TechRoutingMinimumCutOrient::kFromAbove);
  EXPECT_EQ(first_minimum_cut.length, 600);
  EXPECT_EQ(first_minimum_cut.length_distance, 200);

  const auto prl_ids = storage.prlSpacingTableRules(layer);
  ASSERT_EQ(prl_ids.size(), 1u);
  EXPECT_EQ(storage.prlSpacingTableCell(prl_ids.front(), 0, 0), 100);
  EXPECT_EQ(storage.prlSpacingTableCell(prl_ids.front(), 0, 1), 200);
  EXPECT_EQ(storage.prlSpacingTableCell(prl_ids.front(), 1, 0), 200);
  EXPECT_EQ(storage.prlSpacingTableCell(prl_ids.front(), 1, 1), 300);

  const auto influence_ids = storage.influenceSpacingTableRules(layer);
  ASSERT_EQ(influence_ids.size(), 1u);
  const auto influence_entries = storage.influenceSpacingTableEntries(influence_ids.front());
  ASSERT_EQ(influence_entries.size(), 2u);
  EXPECT_EQ(influence_entries[1].width, 400);
  EXPECT_EQ(influence_entries[1].within, 500);
  EXPECT_EQ(influence_entries[1].spacing, 600);

  const auto second_layer = TechRoutingLayerId{database.findLayer("M2").entity()};
  const auto two_width_ids = storage.twoWidthsSpacingTableRules(second_layer);
  ASSERT_EQ(two_width_ids.size(), 1u);
  EXPECT_EQ(storage.twoWidthsSpacingTableCell(two_width_ids.front(), 0, 0), 100);
  EXPECT_EQ(storage.twoWidthsSpacingTableCell(two_width_ids.front(), 0, 1), 200);
  EXPECT_EQ(storage.twoWidthsSpacingTableCell(two_width_ids.front(), 1, 0), 200);
  EXPECT_EQ(storage.twoWidthsSpacingTableCell(two_width_ids.front(), 1, 1), 300);

  const auto density_ids = storage.currentDensityRules(layer);
  ASSERT_EQ(density_ids.size(), 3u);
  const auto& implicit_width = storage.rule(density_ids[0]);
  EXPECT_EQ(implicit_width.type, TechRoutingCurrentDensityType::kPeak);
  EXPECT_TRUE(implicit_width.widths.empty());
  EXPECT_DOUBLE_EQ(storage.currentDensityTableEntry(density_ids[0], 1, 0), 20.0);
  EXPECT_DOUBLE_EQ(storage.currentDensityAt(density_ids[0], 1.5, 999), 15.0);
  EXPECT_DOUBLE_EQ(storage.currentDensityTableEntry(density_ids[1], 1, 1), 4.0);
  EXPECT_DOUBLE_EQ(storage.currentDensityAt(density_ids[2], 100.0, 100), 5.5);
}

TEST_F(LefTechImporterTest, ImportsNativeCutRulesArraySpacingAndCurrentDensity)
{
  const auto lef = writeLef("cut-rules", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER V0 TYPE CUT ; WIDTH 0.04 ; END V0
LAYER V1
  TYPE CUT ;
  WIDTH 0.05 ;
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
  ACCURRENTDENSITY PEAK
    FREQUENCY 1 2 ;
    TABLEENTRIES 10 20 ;
  ACCURRENTDENSITY RMS
    FREQUENCY 1 2 ;
    CUTAREA 0.01 0.02 ;
    TABLEENTRIES 1 2 3 4 ;
  DCCURRENTDENSITY AVERAGE 0.40 ;
END V1
END LIBRARY
)LEF");

  TechStore database;
  LefTechImporter importer(database);
  ASSERT_NO_THROW(importer.import(lef));

  const auto layer = TechCutLayerId{database.findLayer("V1").entity()};
  const auto& storage = database.cutLayerStorage();
  const auto spacing_ids = storage.spacingRules(layer);
  ASSERT_EQ(spacing_ids.size(), 8u);
  EXPECT_EQ(storage.spacingRule(spacing_ids[0]).spacing, 100);
  EXPECT_NE(storage.spacingRule(spacing_ids[1]).flags & TechCutSpacingRuleFlag::kSameNet, 0u);
  const auto& adjacent = storage.spacingRule(spacing_ids[2]);
  EXPECT_NE(adjacent.flags & TechCutSpacingRuleFlag::kHasAdjacentCuts, 0u);
  EXPECT_EQ(adjacent.adjacent_cut_count, 3u);
  EXPECT_EQ(adjacent.adjacent_cut_within, 300);
  const auto& center_area = storage.spacingRule(spacing_ids[3]);
  EXPECT_NE(center_area.flags & TechCutSpacingRuleFlag::kCenterToCenter, 0u);
  EXPECT_NE(center_area.flags & TechCutSpacingRuleFlag::kSameNet, 0u);
  EXPECT_NE(center_area.flags & TechCutSpacingRuleFlag::kHasCutArea, 0u);
  EXPECT_EQ(center_area.cut_area, 20000);
  const auto& pg_only = storage.spacingRule(spacing_ids[4]);
  EXPECT_NE(pg_only.flags & TechCutSpacingRuleFlag::kSameNetPgOnly, 0u);
  const auto& second_layer = storage.spacingRule(spacing_ids[5]);
  EXPECT_NE(second_layer.flags & TechCutSpacingRuleFlag::kHasSecondLayer, 0u);
  EXPECT_NE(second_layer.flags & TechCutSpacingRuleFlag::kStack, 0u);
  EXPECT_EQ(second_layer.second_layer_name, "V0");
  EXPECT_NE(storage.spacingRule(spacing_ids[6]).flags & TechCutSpacingRuleFlag::kExceptSamePgNet, 0u);
  EXPECT_NE(storage.spacingRule(spacing_ids[7]).flags & TechCutSpacingRuleFlag::kParallelOverlap, 0u);

  const auto enclosures = storage.enclosureRules(layer);
  ASSERT_EQ(enclosures.size(), 4u);
  const auto& unqualified = storage.enclosureRule(enclosures[0]);
  EXPECT_EQ(unqualified.side, CutLayerSide::kUnknown);
  EXPECT_NE(unqualified.flags & TechCutEnclosureRuleFlag::kHasMinLength, 0u);
  EXPECT_EQ(unqualified.min_length, 7);
  const auto& below = storage.enclosureRule(enclosures[1]);
  EXPECT_EQ(below.side, CutLayerSide::kBelow);
  EXPECT_EQ(below.overhang1, 10);
  EXPECT_EQ(below.overhang2, 20);
  const auto& qualified_below = storage.enclosureRule(enclosures[2]);
  EXPECT_EQ(qualified_below.side, CutLayerSide::kBelow);
  EXPECT_NE(qualified_below.flags & TechCutEnclosureRuleFlag::kHasMinWidth, 0u);
  EXPECT_NE(qualified_below.flags & TechCutEnclosureRuleFlag::kExceptExtraCut, 0u);
  EXPECT_NE(qualified_below.flags & TechCutEnclosureRuleFlag::kHasCutWithin, 0u);
  EXPECT_EQ(qualified_below.min_width, 80);
  EXPECT_EQ(qualified_below.cut_within, 90);
  EXPECT_EQ(storage.enclosureRule(enclosures[3]).side, CutLayerSide::kAbove);

  const auto array_id = storage.arraySpacingRule(layer);
  ASSERT_TRUE(array_id);
  const auto& array = storage.arraySpacingRule(array_id);
  EXPECT_NE(array.flags & TechCutArraySpacingRuleFlag::kLongArray, 0u);
  EXPECT_NE(array.flags & TechCutArraySpacingRuleFlag::kHasViaWidth, 0u);
  EXPECT_EQ(array.via_width, 80);
  EXPECT_EQ(array.cut_spacing, 50);
  ASSERT_EQ(array.items.size(), 2u);
  EXPECT_EQ(array.items[0].array_cut_count, 2u);
  EXPECT_EQ(array.items[0].spacing, 60);
  EXPECT_EQ(array.items[1].array_cut_count, 3u);
  EXPECT_EQ(array.items[1].spacing, 70);

  const auto orthogonal_tables = storage.orthogonalSpacingTableRules(layer);
  ASSERT_EQ(orthogonal_tables.size(), 1u);
  const auto& orthogonal = storage.orthogonalSpacingTableRule(orthogonal_tables.front());
  EXPECT_EQ(orthogonal.flags, 0u);
  ASSERT_EQ(orthogonal.items.size(), 2u);
  EXPECT_EQ(orthogonal.items[0].within, 300);
  EXPECT_EQ(orthogonal.items[0].spacing, 200);
  EXPECT_EQ(orthogonal.items[1].within, 100);
  EXPECT_EQ(orthogonal.items[1].spacing, 150);

  const auto density_ids = storage.currentDensityRules(layer);
  ASSERT_EQ(density_ids.size(), 3u);
  EXPECT_TRUE(storage.currentDensityRule(density_ids[0]).cut_areas.empty());
  EXPECT_DOUBLE_EQ(storage.currentDensityTableEntry(density_ids[0], 1, 0), 20.0);
  EXPECT_DOUBLE_EQ(storage.currentDensityAt(density_ids[0], 1.5, 12345), 15.0);
  EXPECT_EQ(storage.currentDensityRule(density_ids[1]).cut_areas, (std::vector<int64_t>{10000, 20000}));
  EXPECT_DOUBLE_EQ(storage.currentDensityTableEntry(density_ids[1], 1, 1), 4.0);
  EXPECT_DOUBLE_EQ(storage.currentDensityAt(density_ids[2], 0.0, 0), 0.40);
}

TEST_F(LefTechImporterTest, MaterializesLef58LayerMetadataAndCutQualifiers)
{
  const auto lef = writeLef("lef58-layer-and-cut-qualifiers", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
PROPERTYDEFINITIONS
  LAYER LEF58_TYPE STRING ;
  LAYER LEF58_BACKSIDE STRING ;
  LAYER LEF58_ENCLOSURE STRING ;
  LAYER LEF58_SPACINGTABLE STRING ;
END PROPERTYDEFINITIONS
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  PROPERTY LEF58_TYPE "TYPE POLYROUTING ;" ;
  PROPERTY LEF58_BACKSIDE "BACKSIDE ;" ;
END M1
LAYER V1
  TYPE CUT ;
  WIDTH 0.05 ;
  PROPERTY LEF58_TYPE "TYPE SPECIALCUT ;" ;
  PROPERTY LEF58_BACKSIDE "BACKSIDE ;" ;
  PROPERTY LEF58_ENCLOSURE
    "ENCLOSURE CUTCLASS C1 ABOVE 0.11 0.12 WIDTH 0.13 INCLUDEABUTTED EXCEPTEXTRACUT 0.14 NOSHAREDEDGE ;" ;
  PROPERTY LEF58_SPACINGTABLE
    "SPACINGTABLE ORTHOGONAL WITHIN 0.21 SPACING 0.22 WITHIN 0.23 SPACING 0.24 ;" ;
END V1
END LIBRARY
)LEF");

  TechStore database;
  LefTechImporter importer(database);
  ASSERT_NO_THROW(importer.import(lef));

  const auto m1 = database.findLayer("M1");
  ASSERT_TRUE(m1);
  const auto& m1_info = database.layerInfo(m1);
  EXPECT_EQ(m1_info.lef58_type, TechLef58LayerType::kPolyRouting);
  EXPECT_NE(m1_info.flags & TechLayerInfoFlag::kLef58Backside, 0u);

  const auto v1 = TechCutLayerId{database.findLayer("V1").entity()};
  const auto& v1_info = database.layerInfo(TechLayerId{v1.entity()});
  EXPECT_EQ(v1_info.lef58_type, TechLef58LayerType::kSpecialCut);
  EXPECT_NE(v1_info.flags & TechLayerInfoFlag::kLef58Backside, 0u);

  const auto& storage = database.cutLayerStorage();
  const auto enclosures = storage.lef58EnclosureRules(v1);
  ASSERT_EQ(enclosures.size(), 1u);
  const auto& enclosure = storage.lef58EnclosureRule(enclosures.front());
  EXPECT_EQ(enclosure.cutclass_name, "C1");
  EXPECT_EQ(enclosure.side, CutLayerSide::kAbove);
  EXPECT_EQ(enclosure.overhang1, 110);
  EXPECT_EQ(enclosure.overhang2, 120);
  EXPECT_EQ(enclosure.min_width, 130);
  EXPECT_EQ(enclosure.cut_within, 140);
  EXPECT_NE(enclosure.flags & TechCutLef58EnclosureRuleFlag::kHasMinWidth, 0u);
  EXPECT_NE(enclosure.flags & TechCutLef58EnclosureRuleFlag::kIncludeAbutted, 0u);
  EXPECT_NE(enclosure.flags & TechCutLef58EnclosureRuleFlag::kExceptExtraCut, 0u);
  EXPECT_NE(enclosure.flags & TechCutLef58EnclosureRuleFlag::kHasCutWithin, 0u);
  EXPECT_NE(enclosure.flags & TechCutLef58EnclosureRuleFlag::kNoSharedEdge, 0u);

  const auto orthogonal_tables = storage.orthogonalSpacingTableRules(v1);
  ASSERT_EQ(orthogonal_tables.size(), 1u);
  const auto& orthogonal = storage.orthogonalSpacingTableRule(orthogonal_tables.front());
  EXPECT_NE(orthogonal.flags & TechCutOrthogonalSpacingTableRuleFlag::kLef58Property, 0u);
  ASSERT_EQ(orthogonal.items.size(), 2u);
  EXPECT_EQ(orthogonal.items[0].within, 210);
  EXPECT_EQ(orthogonal.items[0].spacing, 220);
  EXPECT_EQ(orthogonal.items[1].within, 230);
  EXPECT_EQ(orthogonal.items[1].spacing, 240);
}

TEST_F(LefTechImporterTest, RollsBackEveryRuleEntityWhenCommitValidationFails)
{
  const auto lef = writeLef("invalid-rule-table", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  SPACING 0.10 ;
  SPACINGTABLE
    TWOWIDTHS
      WIDTH 0.00 0.10
      WIDTH 0.20 0.20 0.30 ;
END M1
END LIBRARY
)LEF");

  TechStore database;
  LefTechImporter importer(database);
  EXPECT_THROW(importer.import(lef), std::invalid_argument);
  EXPECT_EQ(layerCount(database), 0u);
  EXPECT_TRUE(database.layerSequence().empty());
  EXPECT_FALSE(database.globalStorage().hasUnits());
  EXPECT_EQ(database.techRegistry().registry().storage<TechEntity>().free_list(), 1u);
}

TEST_F(LefTechImporterTest, RollsBackGeometryWhenViaCommitFails)
{
  const auto lef = writeLef("duplicate-via", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
LAYER V1 TYPE CUT ; WIDTH 0.10 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.30 ; WIDTH 0.12 ; END M2
VIA DUPLICATE
  LAYER M1 ; RECT -0.10 -0.10 0.10 0.10 ;
  LAYER V1 ; POLYGON ( -0.05 -0.05 ) ( 0.05 -0.05 ) ( 0.05 0.05 ) ( -0.05 0.05 ) ;
  LAYER M2 ; RECT -0.10 -0.10 0.10 0.10 ;
END DUPLICATE
VIA DUPLICATE
  LAYER M1 ; RECT -0.10 -0.10 0.10 0.10 ;
  LAYER V1 ; RECT -0.05 -0.05 0.05 0.05 ;
  LAYER M2 ; RECT -0.10 -0.10 0.10 0.10 ;
END DUPLICATE
END LIBRARY
)LEF");

  TechStore database;
  LefTechImporter importer(database);
  EXPECT_THROW(importer.import(lef), std::invalid_argument);
  EXPECT_EQ(layerCount(database), 0u);
  EXPECT_TRUE(database.layerSequence().empty());
  EXPECT_EQ(database.geometryPool().rectangleCount(), 0u);
  EXPECT_EQ(database.geometryPool().polygonCount(), 0u);
  EXPECT_EQ(database.geometryPool().pointCount(), 0u);
}

TEST_F(LefTechImporterTest, LeavesTargetEmptyWhenSemanticValidationFails)
{
  const auto lef = writeLef("invalid-reference", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER NIMP TYPE IMPLANT ; WIDTH 0.10 ; SPACING 0.20 LAYER MISSING ; END NIMP
END LIBRARY
)LEF");

  TechStore database;
  LefTechImporter importer(database);
  EXPECT_THROW(importer.import(lef), std::runtime_error);
  EXPECT_EQ(layerCount(database), 0u);
  EXPECT_TRUE(database.layerSequence().empty());
  EXPECT_FALSE(database.globalStorage().hasUnits());
  EXPECT_EQ(database.techRegistry().registry().storage<TechEntity>().free_list(), 1u);
}

TEST_F(LefTechImporterTest, LeavesTargetEmptyWhenParserRejectsInput)
{
  const auto lef = writeLef("syntax-error", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; WIDTH 0.10 ;
)LEF");

  TechStore database;
  LefTechImporter importer(database);
  EXPECT_THROW(importer.import(lef), std::runtime_error);
  EXPECT_EQ(layerCount(database), 0u);
  EXPECT_FALSE(database.globalStorage().hasUnits());
  EXPECT_EQ(database.techRegistry().registry().storage<TechEntity>().free_list(), 1u);
}

TEST_F(LefTechImporterTest, ImportsViaRulesFixedAndGeneratedViasAndNonDefaultRules)
{
  const auto lef = writeLef("tech-objects", R"LEF(
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
  USEVIA VFIX ;
  USEVIARULE VGEN_RULE ;
  MINCUTS V1 2 ;
  PROPERTY USER "ndr" ;
END WIDE
END LIBRARY
)LEF");

  TechStore database;
  LefTechImporter importer(database);
  ASSERT_NO_THROW(importer.import(lef));

  const auto fixed = database.viaMasterStorage().findViaMaster("VFIX");
  ASSERT_TRUE(fixed);
  EXPECT_TRUE(database.viaMasterStorage().hasFixedViaMaster(fixed));
  const auto& fixed_master = database.viaMasterStorage().viaMaster(fixed);
  EXPECT_NE(fixed_master.flags & TechViaMasterFlag::kDefault, 0u);
  EXPECT_NE(fixed_master.flags & TechViaMasterFlag::kHasResistance, 0u);
  EXPECT_DOUBLE_EQ(fixed_master.resistance, 2.5);
  ASSERT_EQ(fixed_master.properties.size(), 1u);
  EXPECT_EQ(fixed_master.properties.front().value, "fixed");
  EXPECT_EQ(database.viaMasterStorage().bottomRects(fixed).front(), (Rect{-70, -80, 70, 80}));
  EXPECT_EQ(database.viaMasterStorage().cutRects(fixed).front(), (Rect{-50, -50, 50, 50}));
  EXPECT_EQ(database.viaMasterStorage().topRects(fixed).front(), (Rect{-80, -70, 80, 70}));
  ASSERT_EQ(database.viaMasterStorage().cutPolygonCount(fixed), 1u);
  const auto fixed_cut_points = database.viaMasterStorage().cutPolygonPoints(fixed, 0);
  ASSERT_EQ(fixed_cut_points.size(), 4u);
  EXPECT_EQ(fixed_cut_points.front(), (Point{-50, -50}));

  const auto generate_rule = database.viaRuleGenerateStorage().findViaRuleGenerate("VGEN_RULE");
  ASSERT_TRUE(generate_rule);
  EXPECT_TRUE(database.viaRuleGenerateStorage().viaRuleGenerate(generate_rule).isDefault());
  EXPECT_EQ(database.viaRuleGenerateStorage().viaRuleGenerate(generate_rule).properties.front().value, "generate");
  EXPECT_EQ(database.viaRuleGenerateStorage().bottomLayer(generate_rule).layer, TechRoutingLayerId{database.findLayer("M1").entity()});
  EXPECT_EQ(database.viaRuleGenerateStorage().cutLayer(generate_rule).layer, TechCutLayerId{database.findLayer("V1").entity()});
  EXPECT_EQ(database.viaRuleGenerateStorage().topLayer(generate_rule).layer, TechRoutingLayerId{database.findLayer("M2").entity()});

  const auto generated = database.viaMasterStorage().findViaMaster("VGENERATED");
  ASSERT_TRUE(generated);
  ASSERT_TRUE(database.viaMasterStorage().hasGeneratedViaMaster(generated));
  const auto& formula = database.viaMasterStorage().generatedViaMaster(generated);
  EXPECT_EQ(formula.via_rule_generate, generate_rule);
  EXPECT_EQ(formula.row_count, 2u);
  EXPECT_EQ(formula.column_count, 3u);
  EXPECT_EQ(formula.pattern, "staggered");
  const auto cuts = database.viaMasterStorage().cutRects(generated);
  ASSERT_EQ(cuts.size(), 6u);
  EXPECT_EQ(cuts.front(), (Rect{-190, -145, -90, -45}));
  EXPECT_EQ(cuts.back(), (Rect{110, 5, 210, 105}));
  EXPECT_EQ(database.viaMasterStorage().bottomRects(generated).front(), (Rect{-235, -199, 265, 171}));
  EXPECT_EQ(database.viaMasterStorage().topRects(generated).front(), (Rect{-267, -233, 273, 177}));

  const auto ordinary = database.viaRuleStorage().findViaRule("ORDINARY");
  ASSERT_TRUE(ordinary);
  EXPECT_EQ(database.viaRuleStorage().lowerLayer(ordinary).layer, TechRoutingLayerId{database.findLayer("M1").entity()});
  EXPECT_EQ(database.viaRuleStorage().upperLayer(ordinary).layer, TechRoutingLayerId{database.findLayer("M2").entity()});
  ASSERT_EQ(database.viaRuleStorage().candidates(ordinary).size(), 1u);
  EXPECT_EQ(database.viaRuleStorage().candidates(ordinary).front(), fixed);
  EXPECT_EQ(database.viaRuleStorage().properties(ordinary).front().value, "ordinary");

  const auto ndr = database.nonDefaultRuleStorage().findNonDefaultRule("WIDE");
  ASSERT_TRUE(ndr);
  EXPECT_TRUE(database.nonDefaultRuleStorage().nonDefaultRule(ndr).isHardSpacing());
  const auto routing_rules = database.nonDefaultRuleStorage().routingRules(ndr);
  ASSERT_EQ(routing_rules.size(), 1u);
  const auto& routing_rule = routing_rules.front();
  EXPECT_EQ(routing_rule.width, 200);
  EXPECT_EQ(routing_rule.diag_width, 220);
  EXPECT_EQ(routing_rule.spacing, 250);
  EXPECT_EQ(routing_rule.wire_extension, 30);
  EXPECT_EQ(database.nonDefaultRuleStorage().useVias(ndr).size(), 1u);
  EXPECT_EQ(database.nonDefaultRuleStorage().useVias(ndr).front(), fixed);
  EXPECT_EQ(database.nonDefaultRuleStorage().useViaRules(ndr).front(), generate_rule);
  EXPECT_EQ(database.nonDefaultRuleStorage().minCutsRules(ndr).front().cut_count, 2u);
  EXPECT_EQ(database.nonDefaultRuleStorage().properties(ndr).front().value, "ndr");
  const auto local_vias = database.nonDefaultRuleStorage().viaDefinitions(ndr);
  ASSERT_EQ(local_vias.size(), 1u);
  EXPECT_EQ(database.nonDefaultRuleStorage().viaDefinition(local_vias.front()).name, "NDR_LOCAL");
  const auto& local_geometry = database.nonDefaultRuleStorage().viaDefinitionGeometry(local_vias.front());
  EXPECT_EQ(database.geometryPool().rectangles(local_geometry.cut_geometry).front(), (Rect{-40, -40, 40, 40}));
  ASSERT_EQ(database.geometryPool().polygonCount(local_geometry.cut_geometry), 1u);
  EXPECT_EQ(database.geometryPool().polygonPoints(local_geometry.cut_geometry, 0).front(), (Point{-30, -30}));
  EXPECT_FALSE(database.viaMasterStorage().findViaMaster("NDR_LOCAL"));
}

TEST_F(LefTechImporterTest, ImportsLegacyNdrSameNetSpacing)
{
  const auto lef = writeLef("legacy-ndr-spacing", R"LEF(
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
  LefTechImporter importer(database);
  ASSERT_NO_THROW(importer.import(lef));

  const auto ndr = database.nonDefaultRuleStorage().findNonDefaultRule("LEGACY");
  ASSERT_TRUE(ndr);
  const auto same_net_spacing = database.nonDefaultRuleStorage().sameNetSpacingRules(ndr);
  ASSERT_EQ(same_net_spacing.size(), 1u);
  const auto& spacing_rule = same_net_spacing.front();
  EXPECT_EQ(spacing_rule.first_layer, database.findLayer("M1"));
  EXPECT_EQ(spacing_rule.second_layer, database.findLayer("M2"));
  EXPECT_EQ(spacing_rule.spacing, 300);
  EXPECT_NE(spacing_rule.flags & TechNdrSameNetSpacingRuleFlag::kStack, 0u);
}

TEST_F(LefTechImporterTest, ResolvesUseViaToAnEarlierNdrViaDefinition)
{
  const auto lef = writeLef("ndr-via-reference", R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.20 ; WIDTH 0.10 ; END M1
LAYER V1 TYPE CUT ; WIDTH 0.10 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.30 ; WIDTH 0.12 ; END M2
NONDEFAULTRULE FIRST
  LAYER M1 WIDTH 0.20 ; END M1
  VIA NDR_VIA
    LAYER M1 ; RECT -0.06 -0.07 0.06 0.07 ;
    LAYER V1 ; RECT -0.04 -0.04 0.04 0.04 ;
    LAYER M2 ; RECT -0.07 -0.06 0.07 0.06 ;
  END NDR_VIA
END FIRST
NONDEFAULTRULE SECOND
  LAYER M1 WIDTH 0.24 ; END M1
  USEVIA NDR_VIA ;
END SECOND
END LIBRARY
)LEF");

  TechStore database;
  ASSERT_NO_THROW(LefTechImporter(database).import(lef));

  auto& storage = database.nonDefaultRuleStorage();
  const auto first = storage.findNonDefaultRule("FIRST");
  const auto second = storage.findNonDefaultRule("SECOND");
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_EQ(storage.viaDefinitions(first).size(), 1u);
  ASSERT_EQ(storage.useVias(second).size(), 1u);
  const auto via = storage.viaDefinitions(first).front();
  EXPECT_EQ(storage.useVias(second).front(), via);
  EXPECT_EQ(storage.viaDefinitionOwner(via), first);
  EXPECT_THROW(static_cast<void>(storage.destroyNonDefaultRule(first)), std::logic_error);
  EXPECT_TRUE(storage.contains(first));
  EXPECT_TRUE(storage.containsViaDefinition(via));
}

TEST_F(LefTechImporterTest, MaterializesLef58LayerPropertiesIntoRuleComponents)
{
  const auto lef = writeLef("lef58-properties", R"LEF(
VERSION 5.8 ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
PROPERTYDEFINITIONS
  LAYER LEF58_TYPE STRING ;
  LAYER LEF58_TRIMMEDMETAL STRING ;
  LAYER LEF58_RECTONLY STRING ;
  LAYER LEF58_RIGHTWAYONGRIDONLY STRING ;
  LAYER LEF58_AREA STRING ;
  LAYER LEF58_CORNERFILLSPACING STRING ;
  LAYER LEF58_CORNERSPACING STRING ;
  LAYER LEF58_MINIMUMCUT STRING ;
  LAYER LEF58_MINSTEP STRING ;
  LAYER LEF58_WIDTHTABLE STRING ;
  LAYER LEF58_SPACING STRING ;
  LAYER LEF58_SPACINGTABLE STRING ;
  LAYER LEF58_CUTCLASS STRING ;
  LAYER LEF58_ENCLOSURE STRING ;
  LAYER LEF58_ENCLOSUREEDGE STRING ;
  LAYER LEF58_EOLENCLOSURE STRING ;
  LAYER LEF58_EOLSPACING STRING ;
END PROPERTYDEFINITIONS
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  PROPERTY LEF58_RECTONLY "RECTONLY EXCEPTNONCOREPINS ;" ;
  PROPERTY LEF58_RIGHTWAYONGRIDONLY "RIGHTWAYONGRIDONLY CHECKMASK ;" ;
  PROPERTY LEF58_AREA "AREA 0.10 MASK 1 EXCEPTMINWIDTH 0.02 EXCEPTMINSIZE 0.03 0.04 ;" ;
  PROPERTY LEF58_CORNERFILLSPACING "CORNERFILLSPACING 0.05 EDGELENGTH 0.06 0.07 ADJACENTEOL 0.08 ;" ;
  PROPERTY LEF58_CORNERSPACING "CORNERSPACING CONVEXCORNER CORNERTOCORNER EXCEPTEOL 0.09 WIDTH 0.10 SPACING 0.11 WIDTH 0.20 SPACING 0.21 ;" ;
  PROPERTY LEF58_MINIMUMCUT "MINIMUMCUT 2 WIDTH 0.09 WITHIN 0.10 FROMABOVE ;" ;
  PROPERTY LEF58_MINSTEP "MINSTEP 0.11 INSIDECORNER LENGTHSUM 0.12 MAXEDGES 3 ;" ;
  PROPERTY LEF58_WIDTHTABLE "WIDTHTABLE 0.10 0.20 WRONGDIRECTION ;" ;
  PROPERTY LEF58_SPACING "SPACING 0.13 NOTCHLENGTH 0.14 CONCAVEENDS 0.15 ;" ;
  PROPERTY LEF58_SPACINGTABLE "SPACINGTABLE PARALLELRUNLENGTH 0.00 0.10 WIDTH 0.00 0.16 0.17 WIDTH 0.20 0.18 0.19 ;" ;
END M1
LAYER V1
  TYPE CUT ;
  PROPERTY LEF58_CUTCLASS "CUTCLASS C1 WIDTH 0.05 LENGTH 0.06 CUTS 2 ORIENT HORIZONTAL ;" ;
  PROPERTY LEF58_ENCLOSURE "ENCLOSURE CUTCLASS C1 ABOVE 0.01 0.02 ;" ;
  PROPERTY LEF58_ENCLOSUREEDGE "ENCLOSUREEDGE CUTCLASS C1 BELOW 0.03 WIDTH 0.04 PARALLEL 0.05 WITHIN 0.06 ;" ;
  PROPERTY LEF58_EOLENCLOSURE "EOLENCLOSURE 0.07 MINEOLWIDTH 0.08 HORIZONTAL CUTCLASS C1 ABOVE 0.09 ;" ;
  PROPERTY LEF58_EOLSPACING "EOLSPACING 0.10 0.11 CUTCLASS C1 TO C1 0.12 0.13 ENDWIDTH 0.14 PRL 0.15 ENCLOSURE 0.16 0.17 EXTENSION 0.18 0.19 SPANLENGTH 0.20 ;" ;
END V1
LAYER TM1
  TYPE MASTERSLICE ;
  PROPERTY LEF58_TYPE "TYPE TRIMMETAL ;" ;
  PROPERTY LEF58_TRIMMEDMETAL "TRIMMEDMETAL M1 MASK 1 ;" ;
END TM1
END LIBRARY
)LEF");

  TechStore database;
  LefTechImporter importer(database);
  ASSERT_NO_THROW(importer.import(lef));

  const auto m1 = TechRoutingLayerId{database.findLayer("M1").entity()};
  const auto& routing = database.routingLayerStorage().routingLayer(m1);
  EXPECT_NE(routing.flags & TechRoutingLayerFlag::kLef58RectOnly, 0u);
  EXPECT_NE(routing.flags & TechRoutingLayerFlag::kLef58RectOnlyExceptNonCorePins, 0u);
  EXPECT_NE(routing.flags & TechRoutingLayerFlag::kLef58RightWayOnGridOnlyCheckMask, 0u);

  const auto areas = database.routingLayerStorage().lef58AreaRules(m1);
  ASSERT_EQ(areas.size(), 1u);
  const auto& area = database.routingLayerStorage().rule(areas.front());
  EXPECT_EQ(area.min_area, 100000);
  ASSERT_EQ(area.except_min_sizes.size(), 1u);
  EXPECT_EQ(area.except_min_sizes.front().min_length, 40);
  EXPECT_EQ(database.routingLayerStorage().lef58CornerFillSpacingRules(m1).size(), 1u);
  const auto corners = database.routingLayerStorage().lef58CornerSpacingRules(m1);
  ASSERT_EQ(corners.size(), 1u);
  const auto& corner = database.routingLayerStorage().rule(corners.front());
  EXPECT_EQ(corner.type, TechRoutingLef58CornerType::kConvex);
  EXPECT_NE(corner.flags & TechRoutingLef58CornerSpacingRuleFlag::kHasExceptEol, 0u);
  EXPECT_NE(corner.flags & TechRoutingLef58CornerSpacingRuleFlag::kCornerToCorner, 0u);
  EXPECT_EQ(corner.except_eol, 90);
  ASSERT_EQ(corner.width_spacings.size(), 2u);
  EXPECT_EQ(corner.width_spacings[1].width, 200);
  EXPECT_EQ(corner.width_spacings[1].spacing, 210);
  EXPECT_EQ(database.routingLayerStorage().lef58MinimumCutRules(m1).size(), 1u);
  EXPECT_EQ(database.routingLayerStorage().lef58MinStepRules(m1).size(), 1u);
  EXPECT_EQ(database.routingLayerStorage().lef58WidthTableRules(m1).size(), 1u);
  EXPECT_EQ(database.routingLayerStorage().lef58SpacingNotchLengthRules(m1).size(), 1u);

  const auto prl = database.routingLayerStorage().prlSpacingTableRules(m1);
  ASSERT_EQ(prl.size(), 1u);
  EXPECT_EQ(database.routingLayerStorage().prlSpacingTableCell(prl.front(), 1, 1), 190);

  const auto v1 = TechCutLayerId{database.findLayer("V1").entity()};
  EXPECT_EQ(database.cutLayerStorage().lef58CutClassRules(v1).size(), 1u);
  EXPECT_EQ(database.cutLayerStorage().lef58EnclosureRules(v1).size(), 1u);
  EXPECT_EQ(database.cutLayerStorage().lef58EnclosureEdgeRules(v1).size(), 1u);
  EXPECT_TRUE(database.cutLayerStorage().lef58EolEnclosureRule(v1));
  EXPECT_TRUE(database.cutLayerStorage().lef58EolSpacingRule(v1));

  const auto tm1 = TechMastersliceLayerId{database.findLayer("TM1").entity()};
  ASSERT_TRUE(database.mastersliceLayerStorage().hasTrimmedMetalRule(tm1));
  const auto& trimmed = database.mastersliceLayerStorage().trimmedMetalRule(tm1);
  EXPECT_EQ(trimmed.metal_layer, m1);
  EXPECT_EQ(trimmed.mask, 1u);
}

TEST_F(LefTechImporterTest, DirectlyImportsSky130TechnologyLef)
{
  const auto lef = std::filesystem::path(ECC_TOOLS_SOURCE_DIR) / "scripts/foundry/sky130/lef/sky130_fd_sc_hd.tlef";
  ASSERT_TRUE(std::filesystem::exists(lef));

  TechStore database;
  LefTechImporter importer(database);
  ASSERT_NO_THROW(importer.import(lef));

  EXPECT_EQ(layerCount(database), 13u);
  EXPECT_EQ(database.layerSequence().size(), 13u);
  EXPECT_EQ(database.globalStorage().getUnits().database_units_per_micron, 1000);
  EXPECT_EQ(database.globalStorage().getManufacturingGrid().value, 5);

  const auto nwell = database.findLayer("nwell");
  ASSERT_TRUE(nwell);
  EXPECT_EQ(database.mastersliceLayerStorage().mastersliceLayer(TechMastersliceLayerId{nwell.entity()}).subtype,
            TechMastersliceType::kNWell);
  const auto met1 = database.findLayer("met1");
  ASSERT_TRUE(met1);
  const auto& routing = database.routingLayerStorage().routingLayer(TechRoutingLayerId{met1.entity()});
  EXPECT_EQ(routing.direction, TechRoutingDirection::kHorizontal);
  EXPECT_EQ(routing.width, 140);
  EXPECT_EQ(routing.pitch_x, 340);
  EXPECT_EQ(routing.area, 83000);
  const auto met1_id = TechRoutingLayerId{met1.entity()};
  EXPECT_EQ(database.routingLayerStorage().prlSpacingTableRules(met1_id).size(), 1u);
  EXPECT_EQ(database.routingLayerStorage().currentDensityRules(met1_id).size(), 2u);

  const auto mcon = TechCutLayerId{database.findLayer("mcon").entity()};
  EXPECT_EQ(database.cutLayerStorage().currentDensityRules(mcon).size(), 1u);
}

TEST_F(LefTechImporterTest, DirectlyImportsIhpTechnologyNativeRules)
{
  const auto lef
      = std::filesystem::path(ECC_TOOLS_SOURCE_DIR) / "scripts/foundry/ihp130/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef";
  ASSERT_TRUE(std::filesystem::exists(lef));

  TechStore database;
  LefTechImporter importer(database);
  ASSERT_NO_THROW(importer.import(lef));

  const auto metal1 = TechRoutingLayerId{database.findLayer("Metal1").entity()};
  ASSERT_TRUE(metal1);
  EXPECT_EQ(database.routingLayerStorage().prlSpacingTableRules(metal1).size(), 1u);
  EXPECT_EQ(database.routingLayerStorage().minimumCutRules(metal1).size(), 1u);
  EXPECT_EQ(database.routingLayerStorage().currentDensityRules(metal1).size(), 1u);

  const auto via1 = TechCutLayerId{database.findLayer("Via1").entity()};
  ASSERT_TRUE(via1);
  EXPECT_EQ(database.cutLayerStorage().spacingRules(via1).size(), 2u);
  EXPECT_EQ(database.cutLayerStorage().enclosureRules(via1).size(), 2u);
  EXPECT_EQ(database.cutLayerStorage().currentDensityRules(via1).size(), 1u);
}

}  // namespace
}  // namespace eccdb
