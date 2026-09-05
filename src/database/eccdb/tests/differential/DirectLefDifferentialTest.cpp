// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "idb/IdbLibraryImporter.h"
#include "idb/IdbTechImporter.h"
#include "idb/LegacyLefReader.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"

namespace eccdb {
namespace {

enum class LayerKind
{
  kRouting,
  kCut,
  kImplant,
  kMasterslice,
  kOverlap
};

LayerKind layerKind(const TechStore& database, TechLayerId layer)
{
  const auto& registry = database.techRegistry().registry();
  if (registry.all_of<TechRoutingLayer>(layer.entity())) {
    return LayerKind::kRouting;
  }
  if (registry.all_of<TechCutLayer>(layer.entity())) {
    return LayerKind::kCut;
  }
  if (registry.all_of<TechImplantLayer>(layer.entity())) {
    return LayerKind::kImplant;
  }
  if (registry.all_of<TechMastersliceLayer>(layer.entity())) {
    return LayerKind::kMasterslice;
  }
  if (registry.all_of<TechOverlapLayer>(layer.entity())) {
    return LayerKind::kOverlap;
  }
  throw std::runtime_error("unknown refactor technology layer kind");
}

size_t layerCount(const TechStore& database)
{
  size_t count = 0;
  for ([[maybe_unused]] const auto entity : database.techRegistry().registry().view<const TechLayerInfo>()) {
    ++count;
  }
  return count;
}

class TemporaryLef
{
 public:
  explicit TemporaryLef(std::string_view text)
      : path(std::filesystem::temp_directory_path() / ("idb-refactor-cut-differential-" + std::to_string(getpid()) + ".lef"))
  {
    std::ofstream output(path);
    if (!output || !(output << text)) {
      throw std::runtime_error("failed to create temporary LEF differential fixture");
    }
  }

  ~TemporaryLef()
  {
    std::error_code error;
    std::filesystem::remove(path, error);
  }

  std::filesystem::path path;
};

void compareSyntheticCutQualifiers()
{
  const TemporaryLef lef(R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
PROPERTYDEFINITIONS
  LAYER LEF58_TYPE STRING ;
  LAYER LEF58_BACKSIDE STRING ;
  LAYER LEF58_AREA STRING ;
  LAYER LEF58_CORNERFILLSPACING STRING ;
  LAYER LEF58_CORNERSPACING STRING ;
  LAYER LEF58_MINIMUMCUT STRING ;
  LAYER LEF58_MINSTEP STRING ;
  LAYER LEF58_WIDTHTABLE STRING ;
  LAYER LEF58_SPACING STRING ;
  LAYER LEF58_ENCLOSURE STRING ;
  LAYER LEF58_CUTCLASS STRING ;
  LAYER LEF58_ENCLOSUREEDGE STRING ;
  LAYER LEF58_EOLENCLOSURE STRING ;
  LAYER LEF58_EOLSPACING STRING ;
  LAYER LEF58_SPACINGTABLE STRING ;
END PROPERTYDEFINITIONS
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
  PROPERTY LEF58_AREA "AREA 0.10 MASK 1 EXCEPTMINWIDTH 0.02 EXCEPTMINSIZE 0.03 0.04 ;" ;
  PROPERTY LEF58_CORNERFILLSPACING "CORNERFILLSPACING 0.05 EDGELENGTH 0.06 0.07 ADJACENTEOL 0.08 ;" ;
  PROPERTY LEF58_CORNERSPACING "CORNERSPACING CONVEXCORNER CORNERTOCORNER EXCEPTEOL 0.09 WIDTH 0.10 SPACING 0.11 ;" ;
  PROPERTY LEF58_MINIMUMCUT "MINIMUMCUT 2 WIDTH 0.09 WITHIN 0.10 FROMABOVE ;" ;
  PROPERTY LEF58_MINSTEP "MINSTEP 0.11 INSIDECORNER LENGTHSUM 0.12 MAXEDGES 3 ;" ;
  PROPERTY LEF58_WIDTHTABLE "WIDTHTABLE 0.10 0.20 WRONGDIRECTION ;" ;
  PROPERTY LEF58_SPACING "SPACING 0.13 NOTCHLENGTH 0.14 CONCAVEENDS 0.15 ;" ;
  PROPERTY LEF58_SPACING "SPACING 0.16 ENDOFLINE 0.17 WITHIN 0.18 ;" ;
END M1
LAYER V0 TYPE CUT ; WIDTH 0.04 ; END V0
LAYER V1
  TYPE CUT ;
  WIDTH 0.05 ;
  SPACING 0.21 CENTERTOCENTER SAMENET AREA 0.02 ;
  SPACING 0.22 SAMENET PGONLY ;
  SPACING 0.23 SAMENET LAYER V0 STACK ;
  SPACING 0.24 ADJACENTCUTS 2 WITHIN 0.25 EXCEPTSAMEPGNET ;
  SPACING 0.26 PARALLELOVERLAP ;
  ENCLOSURE 0.01 0.02 LENGTH 0.03 ;
  ENCLOSURE BELOW 0.04 0.05 WIDTH 0.06 EXCEPTEXTRACUT 0.07 ;
  ARRAYSPACING WIDTH 0.08 CUTSPACING 0.09 ARRAYCUTS 2 SPACING 0.10 ;
  SPACINGTABLE ORTHOGONAL WITHIN 0.30 SPACING 0.20 WITHIN 0.10 SPACING 0.15 ;
  PROPERTY LEF58_TYPE "TYPE SPECIALCUT ;" ;
  PROPERTY LEF58_BACKSIDE "BACKSIDE ;" ;
  PROPERTY LEF58_ENCLOSURE
    "ENCLOSURE CUTCLASS C1 ABOVE 0.11 0.12 WIDTH 0.13 INCLUDEABUTTED EXCEPTEXTRACUT 0.14 NOSHAREDEDGE ;" ;
  PROPERTY LEF58_CUTCLASS "CUTCLASS C1 WIDTH 0.05 LENGTH 0.06 CUTS 2 ORIENT HORIZONTAL ;" ;
  PROPERTY LEF58_ENCLOSUREEDGE "ENCLOSUREEDGE CUTCLASS C1 BELOW 0.03 WIDTH 0.04 PARALLEL 0.05 WITHIN 0.06 ;" ;
  PROPERTY LEF58_EOLENCLOSURE "EOLENCLOSURE 0.07 MINEOLWIDTH 0.08 HORIZONTAL CUTCLASS C1 ABOVE 0.09 ;" ;
  PROPERTY LEF58_EOLSPACING
    "EOLSPACING 0.10 0.11 CUTCLASS C1 TO C1 0.12 0.13 ENDWIDTH 0.14 PRL 0.15
     ENCLOSURE 0.16 0.17 EXTENSION 0.18 0.19 SPANLENGTH 0.20 ;" ;
  PROPERTY LEF58_SPACINGTABLE
    "SPACINGTABLE ORTHOGONAL WITHIN 0.21 SPACING 0.22 WITHIN 0.23 SPACING 0.24 ;
     SPACINGTABLE DEFAULT 0.12 SAMEMASK SAMENET
       LAYER V0 NOSTACK PRLFORALIGNEDCUT C1 TO C2 C3 TO C4
       PRL 0.14 MAXXY C1 TO C2 0.30
       CUTCLASS C1 SIDE C1 END C2
       C3 0.10 0.20 0.30 - 0.40 0.50
       C4 0.60 - 0.70 - 0.80 0.90 ;" ;
END V1
END LIBRARY
)LEF");

  TechStore direct;
  LefTechImporter direct_importer(direct);
  ASSERT_NO_THROW(direct_importer.import(lef.path));

  LegacyLefReader builder;
  std::vector<std::string> files{lef.path.string()};
  auto* service = builder.buildLef(files, true);
  ASSERT_NE(service, nullptr);
  ASSERT_NE(service->get_layout(), nullptr);
  TechStore adapted;
  IdbTechImporter adapted_importer(adapted);
  ASSERT_NO_THROW(adapted_importer.import(*service->get_layout()));

  const auto direct_routing_layer = TechRoutingLayerId{direct.findLayer("M1").entity()};
  const auto adapted_routing_layer = TechRoutingLayerId{adapted.findLayer("M1").entity()};
  const auto& direct_routing = direct.routingLayerStorage();
  const auto& adapted_routing = adapted.routingLayerStorage();

  const auto direct_areas = direct_routing.lef58AreaRules(direct_routing_layer);
  const auto adapted_areas = adapted_routing.lef58AreaRules(adapted_routing_layer);
  ASSERT_EQ(direct_areas.size(), 1u);
  ASSERT_EQ(adapted_areas.size(), 1u);
  const auto& direct_area = direct_routing.rule(direct_areas.front());
  const auto& adapted_area = adapted_routing.rule(adapted_areas.front());
  EXPECT_EQ(std::tie(direct_area.flags, direct_area.min_area, direct_area.mask, direct_area.except_min_width),
            std::tie(adapted_area.flags, adapted_area.min_area, adapted_area.mask, adapted_area.except_min_width));
  ASSERT_EQ(direct_area.except_min_sizes.size(), 1u);
  ASSERT_EQ(adapted_area.except_min_sizes.size(), 1u);
  EXPECT_EQ(std::tie(direct_area.except_min_sizes.front().min_width, direct_area.except_min_sizes.front().min_length),
            std::tie(adapted_area.except_min_sizes.front().min_width, adapted_area.except_min_sizes.front().min_length));

  const auto direct_fill = direct_routing.lef58CornerFillSpacingRules(direct_routing_layer);
  const auto adapted_fill = adapted_routing.lef58CornerFillSpacingRules(adapted_routing_layer);
  ASSERT_EQ(direct_fill.size(), 1u);
  ASSERT_EQ(adapted_fill.size(), 1u);
  const auto& direct_fill_rule = direct_routing.rule(direct_fill.front());
  const auto& adapted_fill_rule = adapted_routing.rule(adapted_fill.front());
  EXPECT_EQ(std::tie(direct_fill_rule.spacing, direct_fill_rule.edge_length1, direct_fill_rule.edge_length2,
                     direct_fill_rule.eol_width),
            std::tie(adapted_fill_rule.spacing, adapted_fill_rule.edge_length1, adapted_fill_rule.edge_length2,
                     adapted_fill_rule.eol_width));

  const auto direct_minimum_cuts = direct_routing.lef58MinimumCutRules(direct_routing_layer);
  const auto adapted_minimum_cuts = adapted_routing.lef58MinimumCutRules(adapted_routing_layer);
  ASSERT_EQ(direct_minimum_cuts.size(), 1u);
  ASSERT_EQ(adapted_minimum_cuts.size(), 1u);
  const auto& direct_minimum_cut = direct_routing.rule(direct_minimum_cuts.front());
  const auto& adapted_minimum_cut = adapted_routing.rule(adapted_minimum_cuts.front());
  EXPECT_EQ(std::tie(direct_minimum_cut.flags, direct_minimum_cut.num_cuts, direct_minimum_cut.width,
                     direct_minimum_cut.within_cut_distance, direct_minimum_cut.orient),
            std::tie(adapted_minimum_cut.flags, adapted_minimum_cut.num_cuts, adapted_minimum_cut.width,
                     adapted_minimum_cut.within_cut_distance, adapted_minimum_cut.orient));

  const auto direct_steps = direct_routing.lef58MinStepRules(direct_routing_layer);
  const auto adapted_steps = adapted_routing.lef58MinStepRules(adapted_routing_layer);
  ASSERT_EQ(direct_steps.size(), 1u);
  ASSERT_EQ(adapted_steps.size(), 1u);
  const auto& direct_step = direct_routing.rule(direct_steps.front());
  const auto& adapted_step = adapted_routing.rule(adapted_steps.front());
  EXPECT_EQ(std::tie(direct_step.flags, direct_step.type, direct_step.min_step_length, direct_step.max_length_sum,
                     direct_step.max_edges),
            std::tie(adapted_step.flags, adapted_step.type, adapted_step.min_step_length, adapted_step.max_length_sum,
                     adapted_step.max_edges));

  const auto direct_widths = direct_routing.lef58WidthTableRules(direct_routing_layer);
  const auto adapted_widths = adapted_routing.lef58WidthTableRules(adapted_routing_layer);
  ASSERT_EQ(direct_widths.size(), 1u);
  ASSERT_EQ(adapted_widths.size(), 1u);
  EXPECT_EQ(direct_routing.rule(direct_widths.front()).flags, adapted_routing.rule(adapted_widths.front()).flags);
  EXPECT_EQ(direct_routing.rule(direct_widths.front()).widths, adapted_routing.rule(adapted_widths.front()).widths);

  const auto direct_notches = direct_routing.lef58SpacingNotchLengthRules(direct_routing_layer);
  const auto adapted_notches = adapted_routing.lef58SpacingNotchLengthRules(adapted_routing_layer);
  ASSERT_EQ(direct_notches.size(), 1u);
  ASSERT_EQ(adapted_notches.size(), 1u);
  const auto& direct_notch = direct_routing.rule(direct_notches.front());
  const auto& adapted_notch = adapted_routing.rule(adapted_notches.front());
  EXPECT_EQ(std::tie(direct_notch.flags, direct_notch.min_spacing, direct_notch.min_notch_length,
                     direct_notch.concave_ends_side_of_notch_width),
            std::tie(adapted_notch.flags, adapted_notch.min_spacing, adapted_notch.min_notch_length,
                     adapted_notch.concave_ends_side_of_notch_width));

  const auto direct_lef58_eol = direct_routing.lef58SpacingEolRules(direct_routing_layer);
  const auto adapted_lef58_eol = adapted_routing.lef58SpacingEolRules(adapted_routing_layer);
  ASSERT_EQ(direct_lef58_eol.size(), 1u);
  ASSERT_EQ(adapted_lef58_eol.size(), 1u);
  const auto& direct_eol = direct_routing.rule(direct_lef58_eol.front());
  const auto& adapted_eol = adapted_routing.rule(adapted_lef58_eol.front());
  EXPECT_EQ(std::tie(direct_eol.flags, direct_eol.eol_space, direct_eol.eol_width, direct_eol.eol_within),
            std::tie(adapted_eol.flags, adapted_eol.eol_space, adapted_eol.eol_width, adapted_eol.eol_within));

  const auto direct_layer = TechCutLayerId{direct.findLayer("V1").entity()};
  const auto adapted_layer = TechCutLayerId{adapted.findLayer("V1").entity()};
  EXPECT_EQ(direct.layerInfo(TechLayerId{direct_layer.entity()}).flags, adapted.layerInfo(TechLayerId{adapted_layer.entity()}).flags);
  EXPECT_EQ(direct.layerInfo(TechLayerId{direct_layer.entity()}).lef58_type,
            adapted.layerInfo(TechLayerId{adapted_layer.entity()}).lef58_type);

  const auto& direct_storage = direct.cutLayerStorage();
  const auto& adapted_storage = adapted.cutLayerStorage();
  const auto direct_spacings = direct_storage.spacingRules(direct_layer);
  const auto adapted_spacings = adapted_storage.spacingRules(adapted_layer);
  ASSERT_EQ(direct_spacings.size(), adapted_spacings.size());
  for (std::size_t index = 0; index < direct_spacings.size(); ++index) {
    const auto& lhs = direct_storage.spacingRule(direct_spacings[index]);
    const auto& rhs = adapted_storage.spacingRule(adapted_spacings[index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_EQ(lhs.second_layer_name, rhs.second_layer_name) << index;
    EXPECT_EQ(lhs.cut_area, rhs.cut_area) << index;
  }
  const auto direct_enclosures = direct_storage.enclosureRules(direct_layer);
  const auto adapted_enclosures = adapted_storage.enclosureRules(adapted_layer);
  ASSERT_EQ(direct_enclosures.size(), adapted_enclosures.size());
  for (std::size_t index = 0; index < direct_enclosures.size(); ++index) {
    const auto& lhs = direct_storage.enclosureRule(direct_enclosures[index]);
    const auto& rhs = adapted_storage.enclosureRule(adapted_enclosures[index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_EQ(lhs.side, rhs.side) << index;
    EXPECT_EQ(lhs.min_width, rhs.min_width) << index;
    EXPECT_EQ(lhs.cut_within, rhs.cut_within) << index;
    EXPECT_EQ(lhs.min_length, rhs.min_length) << index;
  }
  const auto direct_array = direct_storage.arraySpacingRule(direct_layer);
  const auto adapted_array = adapted_storage.arraySpacingRule(adapted_layer);
  ASSERT_TRUE(direct_array);
  ASSERT_TRUE(adapted_array);
  EXPECT_EQ(direct_storage.arraySpacingRule(direct_array).flags, adapted_storage.arraySpacingRule(adapted_array).flags);
  EXPECT_EQ(direct_storage.arraySpacingRule(direct_array).via_width, adapted_storage.arraySpacingRule(adapted_array).via_width);

  const auto direct_orthogonal = direct_storage.orthogonalSpacingTableRules(direct_layer);
  const auto adapted_orthogonal = adapted_storage.orthogonalSpacingTableRules(adapted_layer);
  ASSERT_EQ(direct_orthogonal.size(), adapted_orthogonal.size());
  for (std::size_t index = 0; index < direct_orthogonal.size(); ++index) {
    EXPECT_EQ(direct_storage.orthogonalSpacingTableRule(direct_orthogonal[index]).flags,
              adapted_storage.orthogonalSpacingTableRule(adapted_orthogonal[index]).flags);
    EXPECT_EQ(direct_storage.orthogonalSpacingTableRule(direct_orthogonal[index]).items.size(),
              adapted_storage.orthogonalSpacingTableRule(adapted_orthogonal[index]).items.size());
  }

  const auto direct_cutclasses = direct_storage.lef58CutClassRules(direct_layer);
  const auto adapted_cutclasses = adapted_storage.lef58CutClassRules(adapted_layer);
  ASSERT_EQ(direct_cutclasses.size(), 1u);
  ASSERT_EQ(adapted_cutclasses.size(), 1u);
  const auto& direct_cutclass = direct_storage.lef58CutClassRule(direct_cutclasses.front());
  const auto& adapted_cutclass = adapted_storage.lef58CutClassRule(adapted_cutclasses.front());
  EXPECT_EQ(std::tie(direct_cutclass.flags, direct_cutclass.name, direct_cutclass.via_width, direct_cutclass.via_length,
                     direct_cutclass.num_cut, direct_cutclass.orient),
            std::tie(adapted_cutclass.flags, adapted_cutclass.name, adapted_cutclass.via_width, adapted_cutclass.via_length,
                     adapted_cutclass.num_cut, adapted_cutclass.orient));

  const auto direct_edges = direct_storage.lef58EnclosureEdgeRules(direct_layer);
  const auto adapted_edges = adapted_storage.lef58EnclosureEdgeRules(adapted_layer);
  ASSERT_EQ(direct_edges.size(), 1u);
  ASSERT_EQ(adapted_edges.size(), 1u);
  const auto& direct_edge = direct_storage.lef58EnclosureEdgeRule(direct_edges.front());
  const auto& adapted_edge = adapted_storage.lef58EnclosureEdgeRule(adapted_edges.front());
  EXPECT_EQ(std::tie(direct_edge.flags, direct_edge.cutclass_name, direct_edge.side, direct_edge.overhang,
                     direct_edge.min_width, direct_edge.par_length, direct_edge.par_within),
            std::tie(adapted_edge.flags, adapted_edge.cutclass_name, adapted_edge.side, adapted_edge.overhang,
                     adapted_edge.min_width, adapted_edge.par_length, adapted_edge.par_within));

  const auto direct_eol_enclosure_id = direct_storage.lef58EolEnclosureRule(direct_layer);
  const auto adapted_eol_enclosure_id = adapted_storage.lef58EolEnclosureRule(adapted_layer);
  ASSERT_TRUE(direct_eol_enclosure_id);
  ASSERT_TRUE(adapted_eol_enclosure_id);
  const auto& direct_eol_enclosure = direct_storage.lef58EolEnclosureRule(direct_eol_enclosure_id);
  const auto& adapted_eol_enclosure = adapted_storage.lef58EolEnclosureRule(adapted_eol_enclosure_id);
  EXPECT_EQ(std::tie(direct_eol_enclosure.flags, direct_eol_enclosure.eol_width, direct_eol_enclosure.min_eol_width,
                     direct_eol_enclosure.edge_direction, direct_eol_enclosure.cutclass_name, direct_eol_enclosure.side,
                     direct_eol_enclosure.overhang),
            std::tie(adapted_eol_enclosure.flags, adapted_eol_enclosure.eol_width, adapted_eol_enclosure.min_eol_width,
                     adapted_eol_enclosure.edge_direction, adapted_eol_enclosure.cutclass_name, adapted_eol_enclosure.side,
                     adapted_eol_enclosure.overhang));

  const auto direct_eol_spacing_id = direct_storage.lef58EolSpacingRule(direct_layer);
  const auto adapted_eol_spacing_id = adapted_storage.lef58EolSpacingRule(adapted_layer);
  ASSERT_TRUE(direct_eol_spacing_id);
  ASSERT_TRUE(adapted_eol_spacing_id);
  const auto& direct_eol_spacing = direct_storage.lef58EolSpacingRule(direct_eol_spacing_id);
  const auto& adapted_eol_spacing = adapted_storage.lef58EolSpacingRule(adapted_eol_spacing_id);
  EXPECT_EQ(std::tie(direct_eol_spacing.cutclass_name, direct_eol_spacing.cut_spacing1, direct_eol_spacing.cut_spacing2,
                     direct_eol_spacing.eol_width, direct_eol_spacing.prl, direct_eol_spacing.smaller_overhang,
                     direct_eol_spacing.equal_overhang, direct_eol_spacing.side_ext, direct_eol_spacing.backward_ext,
                     direct_eol_spacing.span_length),
            std::tie(adapted_eol_spacing.cutclass_name, adapted_eol_spacing.cut_spacing1, adapted_eol_spacing.cut_spacing2,
                     adapted_eol_spacing.eol_width, adapted_eol_spacing.prl, adapted_eol_spacing.smaller_overhang,
                     adapted_eol_spacing.equal_overhang, adapted_eol_spacing.side_ext, adapted_eol_spacing.backward_ext,
                     adapted_eol_spacing.span_length));
  ASSERT_EQ(direct_eol_spacing.to_classes.size(), 1u);
  ASSERT_EQ(adapted_eol_spacing.to_classes.size(), 1u);
  EXPECT_EQ(std::tie(direct_eol_spacing.to_classes.front().cutclass_name,
                     direct_eol_spacing.to_classes.front().cut_spacing1, direct_eol_spacing.to_classes.front().cut_spacing2),
            std::tie(adapted_eol_spacing.to_classes.front().cutclass_name,
                     adapted_eol_spacing.to_classes.front().cut_spacing1, adapted_eol_spacing.to_classes.front().cut_spacing2));

  const auto direct_tables = direct_storage.lef58SpacingTableRules(direct_layer);
  const auto adapted_tables = adapted_storage.lef58SpacingTableRules(adapted_layer);
  ASSERT_EQ(direct_tables.size(), 1u);
  ASSERT_EQ(adapted_tables.size(), 1u);
  const auto& direct_table = direct_storage.lef58SpacingTableRule(direct_tables.front());
  const auto& adapted_table = adapted_storage.lef58SpacingTableRule(adapted_tables.front());
  EXPECT_EQ(direct_table.flags, adapted_table.flags);
  EXPECT_EQ(direct_table.flags,
            TechCutLef58SpacingTableRuleFlag::kHasDefault | TechCutLef58SpacingTableRuleFlag::kSameMask
                | TechCutLef58SpacingTableRuleFlag::kSameNet | TechCutLef58SpacingTableRuleFlag::kHasSecondLayer
                | TechCutLef58SpacingTableRuleFlag::kNoStack | TechCutLef58SpacingTableRuleFlag::kPrlForAlignedCut
                | TechCutLef58SpacingTableRuleFlag::kHasPrl | TechCutLef58SpacingTableRuleFlag::kMaxXY);
  EXPECT_EQ(direct_table.default_spacing, 120);
  EXPECT_EQ(direct_table.second_layer_name, "V0");
  EXPECT_EQ(direct_table.prl, 140);
  EXPECT_EQ(direct_table.prl_entries.size(), 1u);
  EXPECT_EQ(direct_table.prl_for_aligned_cut.size(), 2u);
  EXPECT_EQ(direct_table.cutclass1_edges,
            (std::vector<CutClassEdge>{CutClassEdge::kSide, CutClassEdge::kEnd, CutClassEdge::kUnspecified}));
  EXPECT_EQ(direct_table.cutclass2_edges,
            (std::vector<CutClassEdge>{CutClassEdge::kUnspecified, CutClassEdge::kUnspecified}));
  EXPECT_EQ(direct_table.default_spacing, adapted_table.default_spacing);
  EXPECT_EQ(direct_table.prl_direction, adapted_table.prl_direction);
  EXPECT_EQ(direct_table.cutclass1_names, adapted_table.cutclass1_names);
  EXPECT_EQ(direct_table.cutclass2_names, adapted_table.cutclass2_names);
  EXPECT_EQ(direct_table.cutclass1_edges, adapted_table.cutclass1_edges);
  EXPECT_EQ(direct_table.cutclass2_edges, adapted_table.cutclass2_edges);
  ASSERT_EQ(direct_table.prl_for_aligned_cut.size(), adapted_table.prl_for_aligned_cut.size());
  ASSERT_EQ(direct_table.prl_entries.size(), adapted_table.prl_entries.size());
  EXPECT_EQ(direct_table.cells.size(), adapted_table.cells.size());

  const auto direct_lef58 = direct_storage.lef58EnclosureRules(direct_layer);
  const auto adapted_lef58 = adapted_storage.lef58EnclosureRules(adapted_layer);
  ASSERT_EQ(direct_lef58.size(), 1u);
  ASSERT_EQ(adapted_lef58.size(), 1u);
  EXPECT_EQ(direct_storage.lef58EnclosureRule(direct_lef58.front()).flags,
            adapted_storage.lef58EnclosureRule(adapted_lef58.front()).flags);
}

void compareSky130BasicTechnologyFields()
{
  const auto lef = std::filesystem::path(ECC_TOOLS_SOURCE_DIR) / "scripts/foundry/sky130/lef/sky130_fd_sc_hd.tlef";
  ASSERT_TRUE(std::filesystem::exists(lef));

  TechStore direct;
  LefTechImporter direct_importer(direct);
  ASSERT_NO_THROW(direct_importer.import(lef));

  LegacyLefReader builder;
  std::vector<std::string> files{lef.string()};
  auto* service = builder.buildLef(files, true);
  ASSERT_NE(service, nullptr);
  ASSERT_NE(service->get_layout(), nullptr);
  TechStore adapted;
  IdbTechImporter idb_importer(adapted);
  ASSERT_NO_THROW(idb_importer.import(*service->get_layout()));

  ASSERT_TRUE(direct.globalStorage().hasUnits());
  ASSERT_TRUE(adapted.globalStorage().hasUnits());
  const auto& direct_units = direct.globalStorage().getUnits();
  const auto& adapted_units = adapted.globalStorage().getUnits();
  EXPECT_EQ(direct_units.flags, adapted_units.flags);
  EXPECT_EQ(direct_units.nanoseconds, adapted_units.nanoseconds);
  EXPECT_EQ(direct_units.picofarads, adapted_units.picofarads);
  EXPECT_EQ(direct_units.ohms, adapted_units.ohms);
  EXPECT_EQ(direct_units.database_units_per_micron, adapted_units.database_units_per_micron);
  EXPECT_EQ(direct.globalStorage().getManufacturingGrid().value, adapted.globalStorage().getManufacturingGrid().value);

  ASSERT_EQ(layerCount(direct), layerCount(adapted));
  ASSERT_EQ(direct.layerSequence().size(), adapted.layerSequence().size());
  for (size_t index = 0; index < direct.layerSequence().size(); ++index) {
    const auto direct_layer = direct.layerSequence()[index];
    const auto adapted_layer = adapted.layerSequence()[index];
    EXPECT_EQ(direct.layerInfo(direct_layer).name, adapted.layerInfo(adapted_layer).name) << index;
    EXPECT_EQ(layerKind(direct, direct_layer), layerKind(adapted, adapted_layer)) << direct.layerInfo(direct_layer).name;
  }

  const auto direct_met1 = TechRoutingLayerId{direct.findLayer("met1").entity()};
  const auto adapted_met1 = TechRoutingLayerId{adapted.findLayer("met1").entity()};
  const auto& direct_routing = direct.routingLayerStorage().routingLayer(direct_met1);
  const auto& adapted_routing = adapted.routingLayerStorage().routingLayer(adapted_met1);
  EXPECT_EQ(direct_routing.direction, adapted_routing.direction);
  EXPECT_EQ(direct_routing.pitch_form, adapted_routing.pitch_form);
  EXPECT_EQ(direct_routing.offset_form, adapted_routing.offset_form);
  EXPECT_EQ(direct_routing.pitch_x, adapted_routing.pitch_x);
  EXPECT_EQ(direct_routing.pitch_y, adapted_routing.pitch_y);
  EXPECT_EQ(direct_routing.offset_x, adapted_routing.offset_x);
  EXPECT_EQ(direct_routing.offset_y, adapted_routing.offset_y);
  EXPECT_EQ(direct_routing.width, adapted_routing.width);
  EXPECT_EQ(direct_routing.thickness, adapted_routing.thickness);
  EXPECT_EQ(direct_routing.area, adapted_routing.area);
  EXPECT_DOUBLE_EQ(direct_routing.capacitance, adapted_routing.capacitance);
  EXPECT_DOUBLE_EQ(direct_routing.edge_capacitance, adapted_routing.edge_capacitance);
  EXPECT_DOUBLE_EQ(direct_routing.min_density, adapted_routing.min_density);
  EXPECT_DOUBLE_EQ(direct_routing.max_density, adapted_routing.max_density);
  EXPECT_EQ(direct_routing.density_check_length, adapted_routing.density_check_length);
  EXPECT_EQ(direct_routing.density_check_width, adapted_routing.density_check_width);
  EXPECT_EQ(direct_routing.density_check_step, adapted_routing.density_check_step);

  const auto direct_mcon = TechCutLayerId{direct.findLayer("mcon").entity()};
  const auto adapted_mcon = TechCutLayerId{adapted.findLayer("mcon").entity()};
  EXPECT_EQ(direct.cutLayerStorage().cutLayer(direct_mcon).width, adapted.cutLayerStorage().cutLayer(adapted_mcon).width);

  const auto direct_nwell = TechMastersliceLayerId{direct.findLayer("nwell").entity()};
  const auto adapted_nwell = TechMastersliceLayerId{adapted.findLayer("nwell").entity()};
  EXPECT_EQ(direct.mastersliceLayerStorage().mastersliceLayer(direct_nwell).subtype,
            adapted.mastersliceLayerStorage().mastersliceLayer(adapted_nwell).subtype);
}

void compareSky130BasicLibraryFields()
{
  const auto source_root = std::filesystem::path(ECC_TOOLS_SOURCE_DIR);
  const auto tech_lef = source_root / "scripts/foundry/sky130/lef/sky130_fd_sc_hd.tlef";
  const auto cells_lef = source_root / "scripts/foundry/sky130/lef/sky130_fd_sc_hd_merged.lef";
  ASSERT_TRUE(std::filesystem::exists(tech_lef));
  ASSERT_TRUE(std::filesystem::exists(cells_lef));

  TechStore direct_tech;
  LefTechImporter direct_tech_importer(direct_tech);
  ASSERT_NO_THROW(direct_tech_importer.import(tech_lef));
  LibraryStore direct_library(direct_tech.techRegistry());
  LefLibraryImporter direct_library_importer(direct_tech, direct_library);
  const std::array direct_files{tech_lef, cells_lef};
  ASSERT_NO_THROW(direct_library_importer.import(std::span<const std::filesystem::path>(direct_files)));

  LegacyLefReader builder;
  std::vector<std::string> tech_files{tech_lef.string()};
  ASSERT_NE(builder.buildLef(tech_files, true), nullptr);
  std::vector<std::string> cell_files{cells_lef.string()};
  auto* service = builder.buildLef(cell_files, false);
  ASSERT_NE(service, nullptr);
  ASSERT_NE(service->get_layout(), nullptr);
  TechStore adapted_tech;
  IdbTechImporter adapted_tech_importer(adapted_tech);
  ASSERT_NO_THROW(adapted_tech_importer.import(*service->get_layout()));
  LibraryStore adapted_library(adapted_tech.techRegistry());
  IdbLibraryImporter adapted_library_importer(adapted_library, adapted_tech_importer);
  ASSERT_NO_THROW(adapted_library_importer.import(*service->get_layout()));

  EXPECT_EQ(direct_library.siteStorage().siteCount(), adapted_library.siteStorage().siteCount());
  EXPECT_EQ(direct_library.cellMasterStorage().cellMasterCount(), adapted_library.cellMasterStorage().cellMasterCount());
  EXPECT_EQ(direct_library.masterTermStorage().masterTermCount(), adapted_library.masterTermStorage().masterTermCount());
  EXPECT_EQ(direct_library.masterPortStorage().masterPortCount(), adapted_library.masterPortStorage().masterPortCount());

  const auto direct_site_id = direct_library.siteStorage().findSite("unithd");
  const auto adapted_site_id = adapted_library.siteStorage().findSite("unithd");
  ASSERT_TRUE(direct_site_id);
  ASSERT_TRUE(adapted_site_id);
  const auto& direct_site = direct_library.siteStorage().site(direct_site_id);
  const auto& adapted_site = adapted_library.siteStorage().site(adapted_site_id);
  EXPECT_EQ(direct_site.site_class, adapted_site.site_class);
  EXPECT_EQ(direct_site.width, adapted_site.width);
  EXPECT_EQ(direct_site.height, adapted_site.height);
  EXPECT_EQ(direct_site.symmetry_x, adapted_site.symmetry_x);
  EXPECT_EQ(direct_site.symmetry_y, adapted_site.symmetry_y);
  EXPECT_EQ(direct_site.symmetry_r90, adapted_site.symmetry_r90);

  constexpr auto master_name = "sky130_fd_sc_hd__a2111o_1";
  const auto direct_master_id = direct_library.cellMasterStorage().findCellMaster(master_name);
  const auto adapted_master_id = adapted_library.cellMasterStorage().findCellMaster(master_name);
  ASSERT_TRUE(direct_master_id);
  ASSERT_TRUE(adapted_master_id);
  const auto& direct_master = direct_library.cellMasterStorage().cellMaster(direct_master_id);
  const auto& adapted_master = adapted_library.cellMasterStorage().cellMaster(adapted_master_id);
  EXPECT_EQ(direct_master.type, adapted_master.type);
  EXPECT_EQ(direct_master.origin_x, adapted_master.origin_x);
  EXPECT_EQ(direct_master.origin_y, adapted_master.origin_y);
  EXPECT_EQ(direct_master.width, adapted_master.width);
  EXPECT_EQ(direct_master.height, adapted_master.height);
  EXPECT_EQ(direct_master.terms.size(), adapted_master.terms.size());

  const auto direct_term_id = direct_library.masterTermStorage().findMasterTerm(direct_master_id, "A1");
  const auto adapted_term_id = adapted_library.masterTermStorage().findMasterTerm(adapted_master_id, "A1");
  ASSERT_TRUE(direct_term_id);
  ASSERT_TRUE(adapted_term_id);
  const auto& direct_term = direct_library.masterTermStorage().masterTerm(direct_term_id);
  const auto& adapted_term = adapted_library.masterTermStorage().masterTerm(adapted_term_id);
  EXPECT_EQ(direct_term.direction, adapted_term.direction);
  EXPECT_EQ(direct_term.use, adapted_term.use);
  ASSERT_EQ(direct_term.ports.size(), adapted_term.ports.size());
  const auto& direct_port = direct_library.masterPortStorage().masterPort(direct_term.ports.front());
  const auto& adapted_port = adapted_library.masterPortStorage().masterPort(adapted_term.ports.front());
  ASSERT_EQ(direct_port.layer_clauses.size(), adapted_port.layer_clauses.size());
  for (size_t index = 0; index < direct_port.layer_clauses.size(); ++index) {
    const auto& direct_clause = direct_port.layer_clauses[index];
    const auto& adapted_clause = adapted_port.layer_clauses[index];
    EXPECT_EQ(direct_tech.layerInfo(direct_clause.layer).name, adapted_tech.layerInfo(adapted_clause.layer).name);
    const auto direct_rects = direct_library.geometryPool().rectangles(direct_clause.geometry);
    const auto adapted_rects = adapted_library.geometryPool().rectangles(adapted_clause.geometry);
    ASSERT_EQ(direct_rects.size(), adapted_rects.size());
    for (size_t rect_index = 0; rect_index < direct_rects.size(); ++rect_index) {
      EXPECT_EQ(direct_rects[rect_index], adapted_rects[rect_index]);
    }
  }

  ASSERT_TRUE(direct_library.cellMasterStorage().hasObs(direct_master_id));
  ASSERT_TRUE(adapted_library.cellMasterStorage().hasObs(adapted_master_id));
  const auto& direct_obs = direct_library.cellMasterStorage().obs(direct_master_id);
  const auto& adapted_obs = adapted_library.cellMasterStorage().obs(adapted_master_id);
  EXPECT_EQ(direct_obs.layer_clauses.size(), adapted_obs.layer_clauses.size());
  EXPECT_EQ(direct_obs.vias.size(), adapted_obs.vias.size());
}

void expectRectsEqual(std::span<const Rect> direct, std::span<const Rect> adapted, std::string_view role)
{
  ASSERT_EQ(direct.size(), adapted.size()) << role;
  for (size_t index = 0; index < direct.size(); ++index) {
    EXPECT_EQ(direct[index], adapted[index]) << role << " rectangle " << index;
  }
}

void compareSyntheticViaObjects()
{
  const TemporaryLef lef(R"LEF(
VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
END M1
LAYER V1
  TYPE CUT ;
  WIDTH 0.05 ;
  SPACING 0.05 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 0.20 ;
  WIDTH 0.10 ;
END M2
VIARULE VR GENERATE
  LAYER M1 ; ENCLOSURE 0.01 0.02 ;
  LAYER V1 ; RECT -0.025 -0.030 0.025 0.030 ; SPACING 0.07 BY 0.09 ; RESISTANCE 3.5 ;
  LAYER M2 ; ENCLOSURE 0.03 0.04 ;
END VR
VIA VFIX DEFAULT
  RESISTANCE 1.25 ;
  LAYER M1 ; RECT -0.06 -0.07 0.06 0.07 ;
  LAYER V1 ; RECT -0.025 -0.03 0.025 0.03 ;
  LAYER M2 ; RECT -0.08 -0.09 0.08 0.09 ;
END VFIX
VIA VGEN
  VIARULE VR ;
  CUTSIZE 0.05 0.06 ;
  LAYERS M1 V1 M2 ;
  CUTSPACING 0.02 0.03 ;
  ENCLOSURE 0.01 0.02 0.03 0.04 ;
  ROWCOL 2 3 ;
  ORIGIN 0.01 -0.01 ;
  OFFSET 0.01 0.02 -0.01 -0.02 ;
  PATTERN 2_7 ;
END VGEN
MACRO VIA_CELL
  CLASS CORE ;
  ORIGIN 0.10 0.20 ;
  SIZE 1.0 BY 1.0 ;
  PIN A
    DIRECTION INPUT ;
    PORT
      LAYER M1 ;
        RECT 0.10 0.10 0.20 0.20 ;
      VIA 0.30 0.40 VFIX ;
    END
  END A
END VIA_CELL
END LIBRARY
)LEF");

  TechStore direct_tech;
  LefTechImporter(direct_tech).import(lef.path);
  LibraryStore direct_library(direct_tech.techRegistry());
  const std::array direct_files{lef.path};
  LefLibraryImporter(direct_tech, direct_library).import(std::span<const std::filesystem::path>(direct_files));

  LegacyLefReader builder;
  std::vector<std::string> files{lef.path.string()};
  auto* service = builder.buildLef(files, true);
  ASSERT_NE(service, nullptr);
  ASSERT_NE(service->get_layout(), nullptr);
  builder.updateLefData();
  TechStore adapted_tech;
  IdbTechImporter adapted_tech_importer(adapted_tech);
  ASSERT_NO_THROW(adapted_tech_importer.import(*service->get_layout()));
  LibraryStore adapted_library(adapted_tech.techRegistry());
  ASSERT_NO_THROW(IdbLibraryImporter(adapted_library, adapted_tech_importer).import(*service->get_layout()));

  const auto direct_rule_id = direct_tech.viaRuleGenerateStorage().findViaRuleGenerate("VR");
  const auto adapted_rule_id = adapted_tech.viaRuleGenerateStorage().findViaRuleGenerate("VR");
  ASSERT_TRUE(direct_rule_id);
  ASSERT_TRUE(adapted_rule_id);
  const auto& direct_rule_storage = direct_tech.viaRuleGenerateStorage();
  const auto& adapted_rule_storage = adapted_tech.viaRuleGenerateStorage();
  const auto& direct_bottom = direct_rule_storage.bottomLayer(direct_rule_id);
  const auto& adapted_bottom = adapted_rule_storage.bottomLayer(adapted_rule_id);
  EXPECT_EQ(direct_tech.layerInfo(direct_bottom.layer.layer()).name, adapted_tech.layerInfo(adapted_bottom.layer.layer()).name);
  EXPECT_EQ(direct_bottom.flags, adapted_bottom.flags);
  EXPECT_EQ(direct_bottom.enclosure_overhang1, adapted_bottom.enclosure_overhang1);
  EXPECT_EQ(direct_bottom.enclosure_overhang2, adapted_bottom.enclosure_overhang2);
  const auto& direct_cut = direct_rule_storage.cutLayer(direct_rule_id);
  const auto& adapted_cut = adapted_rule_storage.cutLayer(adapted_rule_id);
  EXPECT_EQ(direct_tech.layerInfo(TechLayerId{direct_cut.layer.entity()}).name,
            adapted_tech.layerInfo(TechLayerId{adapted_cut.layer.entity()}).name);
  EXPECT_EQ(direct_cut.flags, adapted_cut.flags);
  EXPECT_EQ(direct_cut.cut_rect, adapted_cut.cut_rect);
  EXPECT_EQ(direct_cut.spacing_x, adapted_cut.spacing_x);
  EXPECT_EQ(direct_cut.spacing_y, adapted_cut.spacing_y);
  EXPECT_DOUBLE_EQ(direct_cut.resistance_per_cut, adapted_cut.resistance_per_cut);
  const auto& direct_top = direct_rule_storage.topLayer(direct_rule_id);
  const auto& adapted_top = adapted_rule_storage.topLayer(adapted_rule_id);
  EXPECT_EQ(direct_tech.layerInfo(direct_top.layer.layer()).name, adapted_tech.layerInfo(adapted_top.layer.layer()).name);
  EXPECT_EQ(direct_top.flags, adapted_top.flags);
  EXPECT_EQ(direct_top.enclosure_overhang1, adapted_top.enclosure_overhang1);
  EXPECT_EQ(direct_top.enclosure_overhang2, adapted_top.enclosure_overhang2);

  for (const auto name : {std::string_view{"VFIX"}, std::string_view{"VGEN"}}) {
    SCOPED_TRACE(name);
    const auto direct_id = direct_tech.viaMasterStorage().findViaMaster(name);
    const auto adapted_id = adapted_tech.viaMasterStorage().findViaMaster(name);
    ASSERT_TRUE(direct_id);
    ASSERT_TRUE(adapted_id);
    const auto& direct_master = direct_tech.viaMasterStorage().viaMaster(direct_id);
    const auto& adapted_master = adapted_tech.viaMasterStorage().viaMaster(adapted_id);
    EXPECT_EQ(direct_master.flags, adapted_master.flags);
    EXPECT_DOUBLE_EQ(direct_master.resistance, adapted_master.resistance);
    const auto& direct_geometry = direct_tech.viaMasterStorage().geometry(direct_id);
    const auto& adapted_geometry = adapted_tech.viaMasterStorage().geometry(adapted_id);
    EXPECT_EQ(direct_tech.layerInfo(direct_geometry.bottom_layer.layer()).name,
              adapted_tech.layerInfo(adapted_geometry.bottom_layer.layer()).name);
    EXPECT_EQ(direct_tech.layerInfo(TechLayerId{direct_geometry.cut_layer.entity()}).name,
              adapted_tech.layerInfo(TechLayerId{adapted_geometry.cut_layer.entity()}).name);
    EXPECT_EQ(direct_tech.layerInfo(direct_geometry.top_layer.layer()).name,
              adapted_tech.layerInfo(adapted_geometry.top_layer.layer()).name);
    expectRectsEqual(direct_tech.viaMasterStorage().bottomRects(direct_id), adapted_tech.viaMasterStorage().bottomRects(adapted_id),
                     "bottom");
    expectRectsEqual(direct_tech.viaMasterStorage().cutRects(direct_id), adapted_tech.viaMasterStorage().cutRects(adapted_id), "cut");
    expectRectsEqual(direct_tech.viaMasterStorage().topRects(direct_id), adapted_tech.viaMasterStorage().topRects(adapted_id), "top");
  }

  const auto direct_generated_id = direct_tech.viaMasterStorage().findViaMaster("VGEN");
  const auto adapted_generated_id = adapted_tech.viaMasterStorage().findViaMaster("VGEN");
  EXPECT_EQ(direct_tech.viaMasterStorage().cutRects(direct_generated_id).size(), 4u);
  EXPECT_EQ(adapted_tech.viaMasterStorage().cutRects(adapted_generated_id).size(), 4u);
  const auto& direct_generated = direct_tech.viaMasterStorage().generatedViaMaster(direct_generated_id);
  const auto& adapted_generated = adapted_tech.viaMasterStorage().generatedViaMaster(adapted_generated_id);
  EXPECT_EQ(direct_generated.flags, adapted_generated.flags);
  EXPECT_EQ(direct_generated.cut_size_x, adapted_generated.cut_size_x);
  EXPECT_EQ(direct_generated.cut_size_y, adapted_generated.cut_size_y);
  EXPECT_EQ(direct_generated.cut_spacing_x, adapted_generated.cut_spacing_x);
  EXPECT_EQ(direct_generated.cut_spacing_y, adapted_generated.cut_spacing_y);
  EXPECT_EQ(direct_generated.bottom_enclosure_x, adapted_generated.bottom_enclosure_x);
  EXPECT_EQ(direct_generated.bottom_enclosure_y, adapted_generated.bottom_enclosure_y);
  EXPECT_EQ(direct_generated.top_enclosure_x, adapted_generated.top_enclosure_x);
  EXPECT_EQ(direct_generated.top_enclosure_y, adapted_generated.top_enclosure_y);
  EXPECT_EQ(direct_generated.row_count, adapted_generated.row_count);
  EXPECT_EQ(direct_generated.column_count, adapted_generated.column_count);
  EXPECT_EQ(direct_generated.origin_x, adapted_generated.origin_x);
  EXPECT_EQ(direct_generated.origin_y, adapted_generated.origin_y);
  EXPECT_EQ(direct_generated.bottom_offset_x, adapted_generated.bottom_offset_x);
  EXPECT_EQ(direct_generated.bottom_offset_y, adapted_generated.bottom_offset_y);
  EXPECT_EQ(direct_generated.top_offset_x, adapted_generated.top_offset_x);
  EXPECT_EQ(direct_generated.top_offset_y, adapted_generated.top_offset_y);
  EXPECT_EQ(direct_generated.pattern, adapted_generated.pattern);

  const auto direct_cell = direct_library.cellMasterStorage().findCellMaster("VIA_CELL");
  const auto adapted_cell = adapted_library.cellMasterStorage().findCellMaster("VIA_CELL");
  ASSERT_TRUE(direct_cell);
  ASSERT_TRUE(adapted_cell);
  const auto direct_term = direct_library.masterTermStorage().findMasterTerm(direct_cell, "A");
  const auto adapted_term = adapted_library.masterTermStorage().findMasterTerm(adapted_cell, "A");
  ASSERT_TRUE(direct_term);
  ASSERT_TRUE(adapted_term);
  const auto& direct_ports = direct_library.masterTermStorage().masterTerm(direct_term).ports;
  const auto& adapted_ports = adapted_library.masterTermStorage().masterTerm(adapted_term).ports;
  ASSERT_EQ(direct_ports.size(), 1u);
  ASSERT_EQ(adapted_ports.size(), 1u);
  const auto& direct_port = direct_library.masterPortStorage().masterPort(direct_ports.front());
  const auto& adapted_port = adapted_library.masterPortStorage().masterPort(adapted_ports.front());
  ASSERT_EQ(direct_port.layer_clauses.size(), 1u);
  ASSERT_EQ(adapted_port.layer_clauses.size(), 1u);
  EXPECT_EQ(direct_tech.layerInfo(direct_port.layer_clauses.front().layer).name,
            adapted_tech.layerInfo(adapted_port.layer_clauses.front().layer).name);
  expectRectsEqual(direct_library.geometryPool().rectangles(direct_port.layer_clauses.front().geometry),
                   adapted_library.geometryPool().rectangles(adapted_port.layer_clauses.front().geometry), "PORT layer");
  ASSERT_EQ(direct_port.vias.size(), 1u);
  ASSERT_EQ(adapted_port.vias.size(), 1u);
  EXPECT_EQ(direct_tech.viaMasterStorage().viaMaster(direct_port.vias.front().via).name,
            adapted_tech.viaMasterStorage().viaMaster(adapted_port.vias.front().via).name);
  EXPECT_EQ(direct_port.vias.front().origin, adapted_port.vias.front().origin);
}

void expectInIsolatedProcess(void (*comparison)())
{
  std::fflush(nullptr);
  const pid_t child = fork();
  ASSERT_GE(child, 0) << "fork failed";
  if (child == 0) {
    comparison();
    std::fflush(nullptr);
    _exit(testing::Test::HasFailure() ? 1 : 0);
  }

  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status)) << "differential-test child terminated abnormally";
  EXPECT_EQ(WEXITSTATUS(status), 0) << "differential-test child failed";
}

TEST(DirectLefDifferentialTest, MatchesLegacyIdbAdapterForSky130BasicTechnologyFields)
{
  expectInIsolatedProcess(compareSky130BasicTechnologyFields);
}

TEST(DirectLefDifferentialTest, MatchesLegacyIdbAdapterForCutQualifiers)
{
  expectInIsolatedProcess(compareSyntheticCutQualifiers);
}

TEST(DirectLefDifferentialTest, MatchesLegacyIdbAdapterForSky130BasicLibraryFields)
{
  expectInIsolatedProcess(compareSky130BasicLibraryFields);
}

TEST(DirectLefDifferentialTest, MatchesLegacyIdbAdapterForViaObjectsAndPortPlacements)
{
  expectInIsolatedProcess(compareSyntheticViaObjects);
}

}  // namespace
}  // namespace eccdb
