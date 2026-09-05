// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IdbObs.h"
#include "lef/LefTechExporter.h"
#include "idb/IdbLibraryImporter.h"
#include "idb/IdbTechImporter.h"
#include "idb/LegacyLefReader.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "LefPdkCorpus.h"

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
  throw std::runtime_error("unknown technology layer kind");
}

std::size_t layerCount(const TechStore& database)
{
  std::size_t result = 0;
  for ([[maybe_unused]] const auto entity : database.techRegistry().registry().view<const TechLayerInfo>()) {
    ++result;
  }
  return result;
}

void expectRoutingLayer(const TechRoutingLayer& direct, const TechRoutingLayer& adapted)
{
  constexpr uint64_t kCommonFlags
      = TechRoutingLayerFlag::kHasMaxWidth | TechRoutingLayerFlag::kHasPitchY | TechRoutingLayerFlag::kHasOffsetY
        | TechRoutingLayerFlag::kHasWireExtension | TechRoutingLayerFlag::kHasThickness | TechRoutingLayerFlag::kHasHeight
        | TechRoutingLayerFlag::kHasArea | TechRoutingLayerFlag::kHasResistance | TechRoutingLayerFlag::kHasCapacitance
        | TechRoutingLayerFlag::kHasEdgeCapacitance | TechRoutingLayerFlag::kLef58RectOnly
        | TechRoutingLayerFlag::kLef58RectOnlyExceptNonCorePins | TechRoutingLayerFlag::kLef58RightWayOnGridOnly
        | TechRoutingLayerFlag::kLef58RightWayOnGridOnlyCheckMask | TechRoutingLayerFlag::kHasWidth | TechRoutingLayerFlag::kHasMinDensity
        | TechRoutingLayerFlag::kHasMaxDensity | TechRoutingLayerFlag::kHasDensityCheckWindow | TechRoutingLayerFlag::kHasDensityCheckStep
        | TechRoutingLayerFlag::kHasDiagWidth | TechRoutingLayerFlag::kHasDiagSpacing | TechRoutingLayerFlag::kHasProtrusion
        | TechRoutingLayerFlag::kHasShrinkage | TechRoutingLayerFlag::kHasCapMultiplier | TechRoutingLayerFlag::kHasFillActiveSpacing
        | TechRoutingLayerFlag::kPolyRouting;
  EXPECT_EQ(direct.flags & kCommonFlags, adapted.flags & kCommonFlags);
  EXPECT_EQ(direct.direction, adapted.direction);
  EXPECT_EQ(direct.pitch_form, adapted.pitch_form);
  if (direct.offset_form == TechRoutingAxisValueForm::kNone) {
    // iDB materializes an absent OFFSET as half the already-quantized PITCH;
    // the direct model preserves that the LEF statement was absent.
    EXPECT_EQ(adapted.offset_form, TechRoutingAxisValueForm::kBothXY);
    EXPECT_EQ(adapted.offset_x, direct.pitch_x / 2);
    EXPECT_EQ(adapted.offset_y, direct.pitch_y / 2);
  } else {
    EXPECT_EQ(direct.offset_form, adapted.offset_form);
    EXPECT_EQ(direct.offset_x, adapted.offset_x);
    EXPECT_EQ(direct.offset_y, adapted.offset_y);
  }
  EXPECT_EQ(direct.width, adapted.width);
  if ((direct.flags & TechRoutingLayerFlag::kHasMinWidth) != 0u) {
    EXPECT_NE(adapted.flags & TechRoutingLayerFlag::kHasMinWidth, 0u);
    EXPECT_EQ(direct.min_width, adapted.min_width);
  } else if ((direct.flags & TechRoutingLayerFlag::kHasWidth) != 0u) {
    // iDB materializes the LEF default MINWIDTH=WIDTH; the direct model keeps
    // statement presence and lets consumers apply the default.
    EXPECT_NE(adapted.flags & TechRoutingLayerFlag::kHasMinWidth, 0u);
    EXPECT_EQ(adapted.min_width, direct.width);
  }
  EXPECT_EQ(direct.max_width, adapted.max_width);
  EXPECT_EQ(direct.diag_width, adapted.diag_width);
  EXPECT_EQ(direct.diag_spacing, adapted.diag_spacing);
  EXPECT_EQ(direct.pitch_x, adapted.pitch_x);
  EXPECT_EQ(direct.pitch_y, adapted.pitch_y);
  EXPECT_EQ(direct.wire_extension, adapted.wire_extension);
  EXPECT_EQ(direct.thickness, adapted.thickness);
  EXPECT_EQ(direct.height, adapted.height);
  EXPECT_EQ(direct.shrinkage, adapted.shrinkage);
  EXPECT_DOUBLE_EQ(direct.cap_multiplier, adapted.cap_multiplier);
  EXPECT_EQ(direct.fill_active_spacing, adapted.fill_active_spacing);
  EXPECT_EQ(direct.area, adapted.area);
  EXPECT_DOUBLE_EQ(direct.resistance, adapted.resistance);
  EXPECT_DOUBLE_EQ(direct.capacitance, adapted.capacitance);
  EXPECT_DOUBLE_EQ(direct.edge_capacitance, adapted.edge_capacitance);
  EXPECT_DOUBLE_EQ(direct.min_density, adapted.min_density);
  EXPECT_DOUBLE_EQ(direct.max_density, adapted.max_density);
  EXPECT_EQ(direct.density_check_length, adapted.density_check_length);
  EXPECT_EQ(direct.density_check_width, adapted.density_check_width);
  EXPECT_EQ(direct.density_check_step, adapted.density_check_step);
  EXPECT_EQ(direct.protrusion_width1, adapted.protrusion_width1);
  EXPECT_EQ(direct.protrusion_length, adapted.protrusion_length);
  EXPECT_EQ(direct.protrusion_width2, adapted.protrusion_width2);
}

void expectRoutingRules(const TechStore& direct, TechRoutingLayerId direct_layer, const TechStore& adapted,
                        TechRoutingLayerId adapted_layer)
{
  const auto& direct_storage = direct.routingLayerStorage();
  const auto& adapted_storage = adapted.routingLayerStorage();

  const auto direct_spacing = direct_storage.spacingRules(direct_layer);
  const auto adapted_spacing = adapted_storage.spacingRules(adapted_layer);
  ASSERT_EQ(direct_spacing.size(), adapted_spacing.size());
  for (std::size_t index = 0; index < direct_spacing.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_spacing[index]);
    const auto& rhs = adapted_storage.rule(adapted_spacing[index]);
    EXPECT_EQ(lhs.type, rhs.type) << index;
    EXPECT_EQ(lhs.min_spacing, rhs.min_spacing) << index;
    EXPECT_EQ(lhs.min_width, rhs.min_width) << index;
    EXPECT_EQ(lhs.max_width, rhs.max_width) << index;
  }

  const auto direct_notch = direct_storage.spacingNotchLengthRules(direct_layer);
  const auto adapted_notch = adapted_storage.spacingNotchLengthRules(adapted_layer);
  ASSERT_EQ(direct_notch.size(), adapted_notch.size());
  for (std::size_t index = 0; index < direct_notch.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_notch[index]);
    const auto& rhs = adapted_storage.rule(adapted_notch[index]);
    EXPECT_EQ(lhs.min_spacing, rhs.min_spacing) << index;
    EXPECT_EQ(lhs.notch_length, rhs.notch_length) << index;
  }

  const auto direct_eol = direct_storage.endOfLineSpacingRules(direct_layer);
  const auto adapted_eol = adapted_storage.endOfLineSpacingRules(adapted_layer);
  ASSERT_EQ(direct_eol.size(), adapted_eol.size());
  for (std::size_t index = 0; index < direct_eol.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_eol[index]);
    const auto& rhs = adapted_storage.rule(adapted_eol[index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_EQ(lhs.min_spacing, rhs.min_spacing) << index;
    EXPECT_EQ(lhs.eol_width, rhs.eol_width) << index;
    EXPECT_EQ(lhs.eol_within, rhs.eol_within) << index;
    EXPECT_EQ(lhs.parallel_space, rhs.parallel_space) << index;
    EXPECT_EQ(lhs.parallel_within, rhs.parallel_within) << index;
  }

  const auto direct_lef58_areas = direct_storage.lef58AreaRules(direct_layer);
  const auto adapted_lef58_areas = adapted_storage.lef58AreaRules(adapted_layer);
  ASSERT_GE(direct_lef58_areas.size(), adapted_lef58_areas.size());
  for (std::size_t index = 0; index < adapted_lef58_areas.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_lef58_areas[index]);
    const auto& rhs = adapted_storage.rule(adapted_lef58_areas[index]);
    EXPECT_EQ(std::tie(lhs.flags, lhs.min_area, lhs.mask, lhs.except_min_width, lhs.except_min_edge_length,
                       lhs.except_max_edge_length, lhs.except_step_x, lhs.except_step_y, lhs.rect_width, lhs.trim_layer_name, lhs.overlap),
              std::tie(rhs.flags, rhs.min_area, rhs.mask, rhs.except_min_width, rhs.except_min_edge_length,
                       rhs.except_max_edge_length, rhs.except_step_x, rhs.except_step_y, rhs.rect_width, rhs.trim_layer_name, rhs.overlap))
        << index;
    ASSERT_EQ(lhs.except_min_sizes.size(), rhs.except_min_sizes.size()) << index;
    for (std::size_t item = 0; item < lhs.except_min_sizes.size(); ++item) {
      EXPECT_EQ(std::tie(lhs.except_min_sizes[item].min_width, lhs.except_min_sizes[item].min_length),
                std::tie(rhs.except_min_sizes[item].min_width, rhs.except_min_sizes[item].min_length))
          << index << ':' << item;
    }
  }

  const auto direct_corner_fill = direct_storage.lef58CornerFillSpacingRules(direct_layer);
  const auto adapted_corner_fill = adapted_storage.lef58CornerFillSpacingRules(adapted_layer);
  ASSERT_GE(direct_corner_fill.size(), adapted_corner_fill.size());
  for (std::size_t index = 0; index < adapted_corner_fill.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_corner_fill[index]);
    const auto& rhs = adapted_storage.rule(adapted_corner_fill[index]);
    EXPECT_EQ(std::tie(lhs.spacing, lhs.edge_length1, lhs.edge_length2, lhs.eol_width),
              std::tie(rhs.spacing, rhs.edge_length1, rhs.edge_length2, rhs.eol_width))
        << index;
  }

  const auto direct_corners = direct_storage.lef58CornerSpacingRules(direct_layer);
  const auto adapted_corners = adapted_storage.lef58CornerSpacingRules(adapted_layer);
  ASSERT_EQ(direct_corners.size(), adapted_corners.size());
  for (std::size_t rule_index = 0; rule_index < direct_corners.size(); ++rule_index) {
    const auto& lhs = direct_storage.rule(direct_corners[rule_index]);
    const auto& rhs = adapted_storage.rule(adapted_corners[rule_index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << rule_index;
    EXPECT_EQ(lhs.type, rhs.type) << rule_index;
    EXPECT_EQ(lhs.except_eol, rhs.except_eol) << rule_index;
    ASSERT_EQ(lhs.width_spacings.size(), rhs.width_spacings.size()) << rule_index;
    for (std::size_t index = 0; index < lhs.width_spacings.size(); ++index) {
      EXPECT_EQ(lhs.width_spacings[index].width, rhs.width_spacings[index].width) << rule_index << ':' << index;
      EXPECT_EQ(lhs.width_spacings[index].spacing, rhs.width_spacings[index].spacing) << rule_index << ':' << index;
    }
  }

  const auto direct_lef58_cuts = direct_storage.lef58MinimumCutRules(direct_layer);
  const auto adapted_lef58_cuts = adapted_storage.lef58MinimumCutRules(adapted_layer);
  ASSERT_GE(direct_lef58_cuts.size(), adapted_lef58_cuts.size());
  for (std::size_t index = 0; index < adapted_lef58_cuts.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_lef58_cuts[index]);
    const auto& rhs = adapted_storage.rule(adapted_lef58_cuts[index]);
    EXPECT_EQ(std::tie(lhs.flags, lhs.num_cuts, lhs.width, lhs.within_cut_distance, lhs.orient, lhs.length, lhs.length_distance,
                       lhs.area, lhs.area_within_distance),
              std::tie(rhs.flags, rhs.num_cuts, rhs.width, rhs.within_cut_distance, rhs.orient, rhs.length, rhs.length_distance,
                       rhs.area, rhs.area_within_distance))
        << index;
    ASSERT_EQ(lhs.cutclasses.size(), rhs.cutclasses.size()) << index;
    for (std::size_t item = 0; item < lhs.cutclasses.size(); ++item) {
      EXPECT_EQ(std::tie(lhs.cutclasses[item].cutclass_name, lhs.cutclasses[item].num_cuts),
                std::tie(rhs.cutclasses[item].cutclass_name, rhs.cutclasses[item].num_cuts))
          << index << ':' << item;
    }
  }

  const auto direct_lef58_steps = direct_storage.lef58MinStepRules(direct_layer);
  const auto adapted_lef58_steps = adapted_storage.lef58MinStepRules(adapted_layer);
  ASSERT_GE(direct_lef58_steps.size(), adapted_lef58_steps.size());
  for (std::size_t index = 0; index < adapted_lef58_steps.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_lef58_steps[index]);
    const auto& rhs = adapted_storage.rule(adapted_lef58_steps[index]);
    EXPECT_EQ(std::tie(lhs.flags, lhs.type, lhs.min_step_length, lhs.max_length_sum, lhs.max_edges, lhs.min_adjacent_length,
                       lhs.min_adjacent_length2, lhs.except_within, lhs.center_width, lhs.min_between_length,
                       lhs.no_adjacent_eol_width, lhs.except_adjacent_length, lhs.followup_min_adjacent_length,
                       lhs.no_between_eol_width),
              std::tie(rhs.flags, rhs.type, rhs.min_step_length, rhs.max_length_sum, rhs.max_edges, rhs.min_adjacent_length,
                       rhs.min_adjacent_length2, rhs.except_within, rhs.center_width, rhs.min_between_length,
                       rhs.no_adjacent_eol_width, rhs.except_adjacent_length, rhs.followup_min_adjacent_length,
                       rhs.no_between_eol_width))
        << index;
  }

  const auto direct_width_tables = direct_storage.lef58WidthTableRules(direct_layer);
  const auto adapted_width_tables = adapted_storage.lef58WidthTableRules(adapted_layer);
  ASSERT_GE(direct_width_tables.size(), adapted_width_tables.size());
  for (std::size_t index = 0; index < adapted_width_tables.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_width_tables[index]);
    const auto& rhs = adapted_storage.rule(adapted_width_tables[index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_EQ(lhs.widths, rhs.widths) << index;
  }

  const auto direct_lef58_notches = direct_storage.lef58SpacingNotchLengthRules(direct_layer);
  const auto adapted_lef58_notches = adapted_storage.lef58SpacingNotchLengthRules(adapted_layer);
  ASSERT_GE(direct_lef58_notches.size(), adapted_lef58_notches.size());
  for (std::size_t index = 0; index < adapted_lef58_notches.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_lef58_notches[index]);
    const auto& rhs = adapted_storage.rule(adapted_lef58_notches[index]);
    EXPECT_EQ(std::tie(lhs.flags, lhs.min_spacing, lhs.min_notch_length, lhs.concave_ends_side_of_notch_width),
              std::tie(rhs.flags, rhs.min_spacing, rhs.min_notch_length, rhs.concave_ends_side_of_notch_width))
        << index;
  }

  const auto direct_lef58_eol = direct_storage.lef58SpacingEolRules(direct_layer);
  const auto adapted_lef58_eol = adapted_storage.lef58SpacingEolRules(adapted_layer);
  ASSERT_GE(direct_lef58_eol.size(), adapted_lef58_eol.size());
  for (std::size_t index = 0; index < adapted_lef58_eol.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_lef58_eol[index]);
    const auto& rhs = adapted_storage.rule(adapted_lef58_eol[index]);
    EXPECT_EQ(std::tie(lhs.flags, lhs.eol_space, lhs.eol_width, lhs.wrong_dir_space, lhs.opposite_width, lhs.eol_within,
                       lhs.wrong_dir_within, lhs.except_exact_width1, lhs.except_exact_width2, lhs.fill_concave_corner_width,
                       lhs.with_cut_class_name, lhs.with_cut_space, lhs.enclosure_end_width, lhs.enclosure_end_within,
                       lhs.end_prl_space, lhs.end_prl, lhs.end_to_end_space, lhs.one_cut_space, lhs.two_cut_space, lhs.extension,
                       lhs.wrong_dir_extension, lhs.other_end_width, lhs.adjacent_max_length, lhs.adjacent_min_length,
                       lhs.parallel_space, lhs.parallel_within, lhs.parallel_run_length, lhs.parallel_min_length,
                       lhs.enclose_cut_side, lhs.enclose_distance, lhs.cut_to_metal_spacing, lhs.to_concave_corner_min_length,
                       lhs.to_concave_corner_min_adjacent_length1, lhs.to_concave_corner_min_adjacent_length2, lhs.notch_length),
              std::tie(rhs.flags, rhs.eol_space, rhs.eol_width, rhs.wrong_dir_space, rhs.opposite_width, rhs.eol_within,
                       rhs.wrong_dir_within, rhs.except_exact_width1, rhs.except_exact_width2, rhs.fill_concave_corner_width,
                       rhs.with_cut_class_name, rhs.with_cut_space, rhs.enclosure_end_width, rhs.enclosure_end_within,
                       rhs.end_prl_space, rhs.end_prl, rhs.end_to_end_space, rhs.one_cut_space, rhs.two_cut_space, rhs.extension,
                       rhs.wrong_dir_extension, rhs.other_end_width, rhs.adjacent_max_length, rhs.adjacent_min_length,
                       rhs.parallel_space, rhs.parallel_within, rhs.parallel_run_length, rhs.parallel_min_length,
                       rhs.enclose_cut_side, rhs.enclose_distance, rhs.cut_to_metal_spacing, rhs.to_concave_corner_min_length,
                       rhs.to_concave_corner_min_adjacent_length1, rhs.to_concave_corner_min_adjacent_length2, rhs.notch_length))
        << index;
  }

  const auto direct_areas = direct_storage.minEncloseAreaRules(direct_layer);
  const auto adapted_areas = adapted_storage.minEncloseAreaRules(adapted_layer);
  ASSERT_EQ(direct_areas.size(), adapted_areas.size());
  for (std::size_t index = 0; index < direct_areas.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_areas[index]);
    const auto& rhs = adapted_storage.rule(adapted_areas[index]);
    EXPECT_EQ(lhs.area, rhs.area) << index;
    EXPECT_EQ(lhs.width, rhs.width) << index;
  }

  const auto direct_steps = direct_storage.minStepRules(direct_layer);
  const auto adapted_steps = adapted_storage.minStepRules(adapted_layer);
  ASSERT_EQ(direct_steps.size(), adapted_steps.size());
  for (std::size_t index = 0; index < direct_steps.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_steps[index]);
    const auto& rhs = adapted_storage.rule(adapted_steps[index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_EQ(lhs.min_step_length, rhs.min_step_length) << index;
    EXPECT_EQ(lhs.max_length_sum, rhs.max_length_sum) << index;
    EXPECT_EQ(lhs.max_edges, rhs.max_edges) << index;
    EXPECT_EQ(lhs.type, rhs.type) << index;
  }

  const auto direct_cuts = direct_storage.minimumCutRules(direct_layer);
  const auto adapted_cuts = adapted_storage.minimumCutRules(adapted_layer);
  ASSERT_EQ(direct_cuts.size(), adapted_cuts.size());
  for (std::size_t index = 0; index < direct_cuts.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_cuts[index]);
    const auto& rhs = adapted_storage.rule(adapted_cuts[index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_EQ(lhs.num_cuts, rhs.num_cuts) << index;
    EXPECT_EQ(lhs.width, rhs.width) << index;
    EXPECT_EQ(lhs.within_cut_distance, rhs.within_cut_distance) << index;
    EXPECT_EQ(lhs.orient, rhs.orient) << index;
    EXPECT_EQ(lhs.length, rhs.length) << index;
    EXPECT_EQ(lhs.length_distance, rhs.length_distance) << index;
  }
  const auto& adapted_component = adapted_storage.routingLayer(adapted_layer);
  if ((adapted_component.flags & TechRoutingLayerFlag::kHasMinCut) != 0u) {
    ASSERT_FALSE(adapted_cuts.empty());
    const auto& first = adapted_storage.rule(adapted_cuts.front());
    EXPECT_EQ(adapted_component.min_cut_num, first.num_cuts);
    EXPECT_EQ(adapted_component.min_cut_width, first.width);
  }

  const auto direct_prl = direct_storage.prlSpacingTableRules(direct_layer);
  const auto adapted_prl = adapted_storage.prlSpacingTableRules(adapted_layer);
  ASSERT_EQ(direct_prl.size(), adapted_prl.size());
  for (std::size_t index = 0; index < direct_prl.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_prl[index]);
    const auto& rhs = adapted_storage.rule(adapted_prl[index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_EQ(lhs.eol_width, rhs.eol_width) << index;
    EXPECT_EQ(lhs.widths, rhs.widths) << index;
    EXPECT_EQ(lhs.parallel_run_lengths, rhs.parallel_run_lengths) << index;
    EXPECT_EQ(lhs.cells, rhs.cells) << index;
    ASSERT_EQ(lhs.except_withins.size(), rhs.except_withins.size()) << index;
    for (std::size_t item = 0; item < lhs.except_withins.size(); ++item) {
      EXPECT_EQ(lhs.except_withins[item].width_index, rhs.except_withins[item].width_index) << index << ':' << item;
      EXPECT_EQ(lhs.except_withins[item].low, rhs.except_withins[item].low) << index << ':' << item;
      EXPECT_EQ(lhs.except_withins[item].high, rhs.except_withins[item].high) << index << ':' << item;
    }
    ASSERT_EQ(lhs.influences.size(), rhs.influences.size()) << index;
    for (std::size_t item = 0; item < lhs.influences.size(); ++item) {
      EXPECT_EQ(lhs.influences[item].width, rhs.influences[item].width) << index << ':' << item;
      EXPECT_EQ(lhs.influences[item].within, rhs.influences[item].within) << index << ':' << item;
      EXPECT_EQ(lhs.influences[item].spacing, rhs.influences[item].spacing) << index << ':' << item;
    }
  }

  const auto direct_jog = direct_storage.lef58SpacingTableJogToJogRules(direct_layer);
  const auto adapted_jog = adapted_storage.lef58SpacingTableJogToJogRules(adapted_layer);
  ASSERT_EQ(direct_jog.size(), adapted_jog.size());
  for (std::size_t index = 0; index < direct_jog.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_jog[index]);
    const auto& rhs = adapted_storage.rule(adapted_jog[index]);
    EXPECT_EQ(lhs.jog_to_jog_spacing, rhs.jog_to_jog_spacing) << index;
    EXPECT_EQ(lhs.jog_width, rhs.jog_width) << index;
    EXPECT_EQ(lhs.short_jog_spacing, rhs.short_jog_spacing) << index;
    ASSERT_EQ(lhs.widths.size(), rhs.widths.size()) << index;
    for (std::size_t item = 0; item < lhs.widths.size(); ++item) {
      EXPECT_EQ(lhs.widths[item].width, rhs.widths[item].width) << index << ':' << item;
      EXPECT_EQ(lhs.widths[item].parallel_length, rhs.widths[item].parallel_length) << index << ':' << item;
      EXPECT_EQ(lhs.widths[item].parallel_within, rhs.widths[item].parallel_within) << index << ':' << item;
      EXPECT_EQ(lhs.widths[item].long_jog_spacing, rhs.widths[item].long_jog_spacing) << index << ':' << item;
    }
  }

  const auto direct_influence = direct_storage.influenceSpacingTableRules(direct_layer);
  const auto adapted_influence = adapted_storage.influenceSpacingTableRules(adapted_layer);
  ASSERT_EQ(direct_influence.size(), adapted_influence.size());
  for (std::size_t rule_index = 0; rule_index < direct_influence.size(); ++rule_index) {
    const auto& lhs = direct_storage.rule(direct_influence[rule_index]).entries;
    const auto& rhs = adapted_storage.rule(adapted_influence[rule_index]).entries;
    ASSERT_EQ(lhs.size(), rhs.size()) << rule_index;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      EXPECT_EQ(lhs[index].width, rhs[index].width) << rule_index << ':' << index;
      EXPECT_EQ(lhs[index].within, rhs[index].within) << rule_index << ':' << index;
      EXPECT_EQ(lhs[index].spacing, rhs[index].spacing) << rule_index << ':' << index;
    }
  }

  const auto direct_two_widths = direct_storage.twoWidthsSpacingTableRules(direct_layer);
  const auto adapted_two_widths = adapted_storage.twoWidthsSpacingTableRules(adapted_layer);
  ASSERT_EQ(direct_two_widths.size(), adapted_two_widths.size());
  for (std::size_t rule_index = 0; rule_index < direct_two_widths.size(); ++rule_index) {
    const auto& lhs = direct_storage.rule(direct_two_widths[rule_index]);
    const auto& rhs = adapted_storage.rule(adapted_two_widths[rule_index]);
    ASSERT_EQ(lhs.widths.size(), rhs.widths.size()) << rule_index;
    for (std::size_t index = 0; index < lhs.widths.size(); ++index) {
      EXPECT_EQ(lhs.widths[index].width, rhs.widths[index].width) << rule_index << ':' << index;
      EXPECT_EQ(lhs.widths[index].has_prl, rhs.widths[index].has_prl) << rule_index << ':' << index;
      EXPECT_EQ(lhs.widths[index].prl, rhs.widths[index].prl) << rule_index << ':' << index;
    }
    EXPECT_EQ(lhs.cells, rhs.cells) << rule_index;
  }

  const auto direct_density = direct_storage.currentDensityRules(direct_layer);
  const auto adapted_density = adapted_storage.currentDensityRules(adapted_layer);
  ASSERT_EQ(direct_density.size(), adapted_density.size());
  for (std::size_t index = 0; index < direct_density.size(); ++index) {
    const auto& lhs = direct_storage.rule(direct_density[index]);
    const auto& rhs = adapted_storage.rule(adapted_density[index]);
    EXPECT_EQ(lhs.signal, rhs.signal) << index;
    EXPECT_EQ(lhs.type, rhs.type) << index;
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_DOUBLE_EQ(lhs.scalar, rhs.scalar) << index;
    EXPECT_EQ(lhs.frequencies, rhs.frequencies) << index;
    EXPECT_EQ(lhs.widths, rhs.widths) << index;
    EXPECT_EQ(lhs.table_entries, rhs.table_entries) << index;
  }
}

void expectCutRules(const TechStore& direct, TechCutLayerId direct_layer, const TechStore& adapted, TechCutLayerId adapted_layer)
{
  const auto& direct_storage = direct.cutLayerStorage();
  const auto& adapted_storage = adapted.cutLayerStorage();

  const auto direct_spacing = direct_storage.spacingRules(direct_layer);
  const auto adapted_spacing = adapted_storage.spacingRules(adapted_layer);
  ASSERT_EQ(direct_spacing.size(), adapted_spacing.size());
  for (std::size_t index = 0; index < direct_spacing.size(); ++index) {
    const auto& lhs = direct_storage.spacingRule(direct_spacing[index]);
    const auto& rhs = adapted_storage.spacingRule(adapted_spacing[index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_EQ(lhs.spacing, rhs.spacing) << index;
    EXPECT_EQ(lhs.adjacent_cut_count, rhs.adjacent_cut_count) << index;
    EXPECT_EQ(lhs.adjacent_cut_within, rhs.adjacent_cut_within) << index;
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
    EXPECT_EQ(lhs.overhang1, rhs.overhang1) << index;
    EXPECT_EQ(lhs.overhang2, rhs.overhang2) << index;
    EXPECT_EQ(lhs.min_width, rhs.min_width) << index;
    EXPECT_EQ(lhs.cut_within, rhs.cut_within) << index;
    EXPECT_EQ(lhs.min_length, rhs.min_length) << index;
  }

  const auto direct_array_id = direct_storage.arraySpacingRule(direct_layer);
  const auto adapted_array_id = adapted_storage.arraySpacingRule(adapted_layer);
  ASSERT_EQ(static_cast<bool>(direct_array_id), static_cast<bool>(adapted_array_id));
  if (direct_array_id) {
    const auto& lhs = direct_storage.arraySpacingRule(direct_array_id);
    const auto& rhs = adapted_storage.arraySpacingRule(adapted_array_id);
    EXPECT_EQ(lhs.flags, rhs.flags);
    EXPECT_EQ(lhs.via_width, rhs.via_width);
    EXPECT_EQ(lhs.cut_spacing, rhs.cut_spacing);
    ASSERT_EQ(lhs.items.size(), rhs.items.size());
    for (std::size_t index = 0; index < lhs.items.size(); ++index) {
      EXPECT_EQ(lhs.items[index].array_cut_count, rhs.items[index].array_cut_count) << index;
      EXPECT_EQ(lhs.items[index].spacing, rhs.items[index].spacing) << index;
    }
  }

  const auto direct_orthogonal = direct_storage.orthogonalSpacingTableRules(direct_layer);
  const auto adapted_orthogonal = adapted_storage.orthogonalSpacingTableRules(adapted_layer);
  ASSERT_EQ(direct_orthogonal.size(), adapted_orthogonal.size());
  for (std::size_t rule_index = 0; rule_index < direct_orthogonal.size(); ++rule_index) {
    const auto& lhs = direct_storage.orthogonalSpacingTableRule(direct_orthogonal[rule_index]);
    const auto& rhs = adapted_storage.orthogonalSpacingTableRule(adapted_orthogonal[rule_index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << rule_index;
    ASSERT_EQ(lhs.items.size(), rhs.items.size()) << rule_index;
    for (std::size_t index = 0; index < lhs.items.size(); ++index) {
      EXPECT_EQ(lhs.items[index].within, rhs.items[index].within) << rule_index << ':' << index;
      EXPECT_EQ(lhs.items[index].spacing, rhs.items[index].spacing) << rule_index << ':' << index;
    }
  }

  const auto direct_cutclasses = direct_storage.lef58CutClassRules(direct_layer);
  const auto adapted_cutclasses = adapted_storage.lef58CutClassRules(adapted_layer);
  ASSERT_GE(direct_cutclasses.size(), adapted_cutclasses.size());
  for (std::size_t index = 0; index < adapted_cutclasses.size(); ++index) {
    const auto& lhs = direct_storage.lef58CutClassRule(direct_cutclasses[index]);
    const auto& rhs = adapted_storage.lef58CutClassRule(adapted_cutclasses[index]);
    EXPECT_EQ(std::tie(lhs.flags, lhs.name, lhs.via_width, lhs.via_length, lhs.num_cut, lhs.orient),
              std::tie(rhs.flags, rhs.name, rhs.via_width, rhs.via_length, rhs.num_cut, rhs.orient))
        << index;
  }

  const auto direct_lef58_enclosures = direct_storage.lef58EnclosureRules(direct_layer);
  const auto adapted_lef58_enclosures = adapted_storage.lef58EnclosureRules(adapted_layer);
  ASSERT_EQ(direct_lef58_enclosures.size(), adapted_lef58_enclosures.size());
  for (std::size_t index = 0; index < direct_lef58_enclosures.size(); ++index) {
    const auto& lhs = direct_storage.lef58EnclosureRule(direct_lef58_enclosures[index]);
    const auto& rhs = adapted_storage.lef58EnclosureRule(adapted_lef58_enclosures[index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_EQ(lhs.cutclass_name, rhs.cutclass_name) << index;
    EXPECT_EQ(lhs.side, rhs.side) << index;
    EXPECT_EQ(lhs.overhang1, rhs.overhang1) << index;
    EXPECT_EQ(lhs.overhang2, rhs.overhang2) << index;
    EXPECT_EQ(lhs.end_overhang1, rhs.end_overhang1) << index;
    EXPECT_EQ(lhs.side_overhang2, rhs.side_overhang2) << index;
    EXPECT_EQ(lhs.min_width, rhs.min_width) << index;
    EXPECT_EQ(lhs.cut_within, rhs.cut_within) << index;
    EXPECT_EQ(lhs.min_length, rhs.min_length) << index;
    EXPECT_EQ(lhs.redundant_cut_within, rhs.redundant_cut_within) << index;
  }

  const auto direct_enclosure_edges = direct_storage.lef58EnclosureEdgeRules(direct_layer);
  const auto adapted_enclosure_edges = adapted_storage.lef58EnclosureEdgeRules(adapted_layer);
  ASSERT_GE(direct_enclosure_edges.size(), adapted_enclosure_edges.size());
  for (std::size_t index = 0; index < adapted_enclosure_edges.size(); ++index) {
    const auto& lhs = direct_storage.lef58EnclosureEdgeRule(direct_enclosure_edges[index]);
    const auto& rhs = adapted_storage.lef58EnclosureEdgeRule(adapted_enclosure_edges[index]);
    EXPECT_EQ(std::tie(lhs.flags, lhs.cutclass_name, lhs.side, lhs.overhang, lhs.min_width, lhs.max_width, lhs.par_length,
                       lhs.par_within, lhs.cut_within, lhs.except_within, lhs.convex_length, lhs.adjacent_length,
                       lhs.convex_par_within, lhs.convex_corner_length),
              std::tie(rhs.flags, rhs.cutclass_name, rhs.side, rhs.overhang, rhs.min_width, rhs.max_width, rhs.par_length,
                       rhs.par_within, rhs.cut_within, rhs.except_within, rhs.convex_length, rhs.adjacent_length,
                       rhs.convex_par_within, rhs.convex_corner_length))
        << index;
  }

  const auto direct_eol_enclosure = direct_storage.lef58EolEnclosureRule(direct_layer);
  const auto adapted_eol_enclosure = adapted_storage.lef58EolEnclosureRule(adapted_layer);
  ASSERT_TRUE(!adapted_eol_enclosure || direct_eol_enclosure);
  if (adapted_eol_enclosure) {
    const auto& lhs = direct_storage.lef58EolEnclosureRule(direct_eol_enclosure);
    const auto& rhs = adapted_storage.lef58EolEnclosureRule(adapted_eol_enclosure);
    EXPECT_EQ(std::tie(lhs.flags, lhs.eol_width, lhs.min_eol_width, lhs.edge_direction, lhs.cutclass_name, lhs.side,
                       lhs.application, lhs.overhang, lhs.extract_overhang, lhs.parallel_space, lhs.backward_ext,
                       lhs.forward_ext, lhs.min_length),
              std::tie(rhs.flags, rhs.eol_width, rhs.min_eol_width, rhs.edge_direction, rhs.cutclass_name, rhs.side,
                       rhs.application, rhs.overhang, rhs.extract_overhang, rhs.parallel_space, rhs.backward_ext,
                       rhs.forward_ext, rhs.min_length));
  }

  const auto direct_eol_spacing = direct_storage.lef58EolSpacingRule(direct_layer);
  const auto adapted_eol_spacing = adapted_storage.lef58EolSpacingRule(adapted_layer);
  ASSERT_TRUE(!adapted_eol_spacing || direct_eol_spacing);
  if (adapted_eol_spacing) {
    const auto& lhs = direct_storage.lef58EolSpacingRule(direct_eol_spacing);
    const auto& rhs = adapted_storage.lef58EolSpacingRule(adapted_eol_spacing);
    EXPECT_EQ(std::tie(lhs.cutclass_name, lhs.cut_spacing1, lhs.cut_spacing2, lhs.eol_width, lhs.prl, lhs.smaller_overhang,
                       lhs.equal_overhang, lhs.side_ext, lhs.backward_ext, lhs.span_length),
              std::tie(rhs.cutclass_name, rhs.cut_spacing1, rhs.cut_spacing2, rhs.eol_width, rhs.prl, rhs.smaller_overhang,
                       rhs.equal_overhang, rhs.side_ext, rhs.backward_ext, rhs.span_length));
    ASSERT_EQ(lhs.to_classes.size(), rhs.to_classes.size());
    for (std::size_t index = 0; index < lhs.to_classes.size(); ++index) {
      EXPECT_EQ(std::tie(lhs.to_classes[index].cutclass_name, lhs.to_classes[index].cut_spacing1,
                         lhs.to_classes[index].cut_spacing2),
                std::tie(rhs.to_classes[index].cutclass_name, rhs.to_classes[index].cut_spacing1,
                         rhs.to_classes[index].cut_spacing2))
          << index;
    }
  }

  const auto direct_lef58_tables = direct_storage.lef58SpacingTableRules(direct_layer);
  const auto adapted_lef58_tables = adapted_storage.lef58SpacingTableRules(adapted_layer);
  ASSERT_EQ(direct_lef58_tables.size(), adapted_lef58_tables.size());
  for (std::size_t index = 0; index < direct_lef58_tables.size(); ++index) {
    const auto& lhs = direct_storage.lef58SpacingTableRule(direct_lef58_tables[index]);
    const auto& rhs = adapted_storage.lef58SpacingTableRule(adapted_lef58_tables[index]);
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_EQ(lhs.default_spacing, rhs.default_spacing) << index;
    EXPECT_EQ(lhs.second_layer_name, rhs.second_layer_name) << index;
    EXPECT_EQ(lhs.prl, rhs.prl) << index;
    EXPECT_EQ(lhs.prl_direction, rhs.prl_direction) << index;
    EXPECT_EQ(lhs.cutclass1_names, rhs.cutclass1_names) << index;
    EXPECT_EQ(lhs.cutclass2_names, rhs.cutclass2_names) << index;
    EXPECT_EQ(lhs.cutclass1_edges, rhs.cutclass1_edges) << index;
    EXPECT_EQ(lhs.cutclass2_edges, rhs.cutclass2_edges) << index;
    ASSERT_EQ(lhs.prl_for_aligned_cut.size(), rhs.prl_for_aligned_cut.size()) << index;
    for (std::size_t pair = 0; pair < lhs.prl_for_aligned_cut.size(); ++pair) {
      EXPECT_EQ(lhs.prl_for_aligned_cut[pair].from, rhs.prl_for_aligned_cut[pair].from) << index << ':' << pair;
      EXPECT_EQ(lhs.prl_for_aligned_cut[pair].to, rhs.prl_for_aligned_cut[pair].to) << index << ':' << pair;
    }
    ASSERT_EQ(lhs.prl_entries.size(), rhs.prl_entries.size()) << index;
    for (std::size_t entry = 0; entry < lhs.prl_entries.size(); ++entry) {
      EXPECT_EQ(lhs.prl_entries[entry].from, rhs.prl_entries[entry].from) << index << ':' << entry;
      EXPECT_EQ(lhs.prl_entries[entry].to, rhs.prl_entries[entry].to) << index << ':' << entry;
      EXPECT_EQ(lhs.prl_entries[entry].prl, rhs.prl_entries[entry].prl) << index << ':' << entry;
    }
    ASSERT_EQ(lhs.cells.size(), rhs.cells.size()) << index;
    for (std::size_t cell = 0; cell < lhs.cells.size(); ++cell) {
      EXPECT_EQ(lhs.cells[cell].has_cut_spacing1, rhs.cells[cell].has_cut_spacing1) << index << ':' << cell;
      EXPECT_EQ(lhs.cells[cell].has_cut_spacing2, rhs.cells[cell].has_cut_spacing2) << index << ':' << cell;
      EXPECT_EQ(lhs.cells[cell].cut_spacing1, rhs.cells[cell].cut_spacing1) << index << ':' << cell;
      EXPECT_EQ(lhs.cells[cell].cut_spacing2, rhs.cells[cell].cut_spacing2) << index << ':' << cell;
    }
  }

  const auto direct_density = direct_storage.currentDensityRules(direct_layer);
  const auto adapted_density = adapted_storage.currentDensityRules(adapted_layer);
  ASSERT_EQ(direct_density.size(), adapted_density.size());
  for (std::size_t index = 0; index < direct_density.size(); ++index) {
    const auto& lhs = direct_storage.currentDensityRule(direct_density[index]);
    const auto& rhs = adapted_storage.currentDensityRule(adapted_density[index]);
    EXPECT_EQ(lhs.signal, rhs.signal) << index;
    EXPECT_EQ(lhs.type, rhs.type) << index;
    EXPECT_EQ(lhs.flags, rhs.flags) << index;
    EXPECT_DOUBLE_EQ(lhs.scalar, rhs.scalar) << index;
    EXPECT_EQ(lhs.frequencies, rhs.frequencies) << index;
    EXPECT_EQ(lhs.cut_areas, rhs.cut_areas) << index;
    EXPECT_EQ(lhs.table_entries, rhs.table_entries) << index;
  }
}

void expectGlobals(const TechStore& direct, const TechStore& adapted)
{
  EXPECT_EQ(direct.globalStorage().hasUnits(), adapted.globalStorage().hasUnits());
  if (direct.globalStorage().hasUnits() && adapted.globalStorage().hasUnits()) {
    const auto& lhs = direct.globalStorage().getUnits();
    const auto& rhs = adapted.globalStorage().getUnits();
    EXPECT_EQ(lhs.flags, rhs.flags);
    EXPECT_EQ(lhs.nanoseconds, rhs.nanoseconds);
    EXPECT_EQ(lhs.picofarads, rhs.picofarads);
    EXPECT_EQ(lhs.ohms, rhs.ohms);
    EXPECT_EQ(lhs.milliwatts, rhs.milliwatts);
    EXPECT_EQ(lhs.milliamps, rhs.milliamps);
    EXPECT_EQ(lhs.volts, rhs.volts);
    EXPECT_EQ(lhs.database_units_per_micron, rhs.database_units_per_micron);
    EXPECT_EQ(lhs.megahertz, rhs.megahertz);
  }

  EXPECT_EQ(direct.globalStorage().hasManufacturingGrid(), adapted.globalStorage().hasManufacturingGrid());
  if (direct.globalStorage().hasManufacturingGrid() && adapted.globalStorage().hasManufacturingGrid()) {
    EXPECT_EQ(direct.globalStorage().getManufacturingGrid().value, adapted.globalStorage().getManufacturingGrid().value);
  }

  EXPECT_EQ(direct.globalStorage().hasMaxViaStack(), adapted.globalStorage().hasMaxViaStack());
  if (direct.globalStorage().hasMaxViaStack() && adapted.globalStorage().hasMaxViaStack()) {
    const auto& lhs = direct.globalStorage().getMaxViaStack();
    const auto& rhs = adapted.globalStorage().getMaxViaStack();
    EXPECT_EQ(lhs.flags, rhs.flags);
    EXPECT_EQ(lhs.max_stack_count, rhs.max_stack_count);
    if ((lhs.flags & TechMaxViaStackFlag::kHasRange) != 0u && (rhs.flags & TechMaxViaStackFlag::kHasRange) != 0u) {
      EXPECT_EQ(direct.layerInfo(TechLayerId{lhs.bottom_layer.entity()}).name,
                adapted.layerInfo(TechLayerId{rhs.bottom_layer.entity()}).name);
      EXPECT_EQ(direct.layerInfo(TechLayerId{lhs.top_layer.entity()}).name, adapted.layerInfo(TechLayerId{rhs.top_layer.entity()}).name);
    }
  }
}

void expectRectangleSets(std::span<const Rect> direct, std::span<const Rect> adapted)
{
  ASSERT_EQ(direct.size(), adapted.size());
  auto lhs = std::vector<Rect>(direct.begin(), direct.end());
  auto rhs = std::vector<Rect>(adapted.begin(), adapted.end());
  const auto less = [](Rect first, Rect second) {
    return std::tie(first.ll_x, first.ll_y, first.ur_x, first.ur_y) < std::tie(second.ll_x, second.ll_y, second.ur_x, second.ur_y);
  };
  std::sort(lhs.begin(), lhs.end(), less);
  std::sort(rhs.begin(), rhs.end(), less);
  EXPECT_EQ(lhs, rhs);
}

void expectTechnologyCommonSubset(const TechStore& direct, const TechStore& adapted)
{
  expectGlobals(direct, adapted);
  ASSERT_EQ(layerCount(direct), layerCount(adapted));
  ASSERT_EQ(direct.layerSequence().size(), adapted.layerSequence().size());
  for (std::size_t index = 0; index < direct.layerSequence().size(); ++index) {
    EXPECT_EQ(direct.layerInfo(direct.layerSequence()[index]).name, adapted.layerInfo(adapted.layerSequence()[index]).name) << index;
  }

  const auto& adapted_registry = adapted.techRegistry().registry();
  for (const auto adapted_entity : adapted_registry.view<const TechLayerInfo>()) {
    const auto adapted_layer = TechLayerId{adapted_entity};
    const auto& adapted_info = adapted.layerInfo(adapted_layer);
    SCOPED_TRACE(adapted_info.name);
    const auto direct_layer = direct.findLayer(adapted_info.name);
    ASSERT_TRUE(direct_layer);
    EXPECT_EQ(direct.layerInfo(direct_layer).name, adapted_info.name);
    EXPECT_EQ(direct.layerInfo(direct_layer).flags, adapted_info.flags);
    EXPECT_EQ(direct.layerInfo(direct_layer).lef58_type, adapted_info.lef58_type);
    ASSERT_EQ(layerKind(direct, direct_layer), layerKind(adapted, adapted_layer));

    switch (layerKind(adapted, adapted_layer)) {
      case LayerKind::kRouting: {
        const auto direct_id = TechRoutingLayerId{direct_layer.entity()};
        const auto adapted_id = TechRoutingLayerId{adapted_layer.entity()};
        expectRoutingLayer(direct.routingLayerStorage().routingLayer(direct_id), adapted.routingLayerStorage().routingLayer(adapted_id));
        expectRoutingRules(direct, direct_id, adapted, adapted_id);
        break;
      }
      case LayerKind::kCut: {
        const auto direct_id = TechCutLayerId{direct_layer.entity()};
        const auto adapted_id = TechCutLayerId{adapted_layer.entity()};
        const auto& lhs = direct.cutLayerStorage().cutLayer(direct_id);
        const auto& rhs = adapted.cutLayerStorage().cutLayer(adapted_id);
        EXPECT_EQ(lhs.flags, rhs.flags);
        EXPECT_EQ(lhs.width, rhs.width);
        EXPECT_DOUBLE_EQ(lhs.resistance_per_cut, rhs.resistance_per_cut);
        expectCutRules(direct, direct_id, adapted, adapted_id);
        break;
      }
      case LayerKind::kImplant: {
        const auto direct_id = TechImplantLayerId{direct_layer.entity()};
        const auto adapted_id = TechImplantLayerId{adapted_layer.entity()};
        const auto& lhs = direct.implantLayerStorage().implantLayer(direct_id);
        const auto& rhs = adapted.implantLayerStorage().implantLayer(adapted_id);
        EXPECT_EQ(lhs.flags, rhs.flags);
        EXPECT_EQ(lhs.min_width, rhs.min_width);
        const auto lhs_rules = direct.implantLayerStorage().spacingRules(direct_id);
        const auto rhs_rules = adapted.implantLayerStorage().spacingRules(adapted_id);
        ASSERT_EQ(lhs_rules.size(), rhs_rules.size());
        for (std::size_t index = 0; index < lhs_rules.size(); ++index) {
          const auto& lhs_rule = lhs_rules[index];
          const auto& rhs_rule = rhs_rules[index];
          EXPECT_EQ(lhs_rule.flags, rhs_rule.flags) << index;
          EXPECT_EQ(lhs_rule.min_spacing, rhs_rule.min_spacing) << index;
          if ((lhs_rule.flags & TechImplantSpacingRuleFlag::kHasOtherLayer) != 0u
              && (rhs_rule.flags & TechImplantSpacingRuleFlag::kHasOtherLayer) != 0u) {
            EXPECT_EQ(direct.layerInfo(TechLayerId{lhs_rule.other_layer.entity()}).name,
                      adapted.layerInfo(TechLayerId{rhs_rule.other_layer.entity()}).name)
                << index;
          }
        }
        break;
      }
      case LayerKind::kMasterslice: {
        const auto lhs = direct.mastersliceLayerStorage().mastersliceLayer(TechMastersliceLayerId{direct_layer.entity()});
        const auto rhs = adapted.mastersliceLayerStorage().mastersliceLayer(TechMastersliceLayerId{adapted_layer.entity()});
        EXPECT_EQ(lhs.subtype, rhs.subtype);
        break;
      }
      case LayerKind::kOverlap:
        break;
    }
  }

  const auto adapted_generate_rules = adapted.viaRuleGenerateStorage().viaRuleGenerates();
  for (const auto adapted_id : adapted_generate_rules) {
    const auto& adapted_rule = adapted.viaRuleGenerateStorage().viaRuleGenerate(adapted_id);
    SCOPED_TRACE("VIARULE GENERATE " + adapted_rule.name);
    const auto direct_id = direct.viaRuleGenerateStorage().findViaRuleGenerate(adapted_rule.name);
    ASSERT_TRUE(direct_id);
    const auto& direct_bottom = direct.viaRuleGenerateStorage().bottomLayer(direct_id);
    const auto& adapted_bottom = adapted.viaRuleGenerateStorage().bottomLayer(adapted_id);
    EXPECT_EQ(direct.layerInfo(direct_bottom.layer.layer()).name, adapted.layerInfo(adapted_bottom.layer.layer()).name);
    EXPECT_EQ(direct_bottom.flags & TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure,
              adapted_bottom.flags & TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure);
    EXPECT_EQ(direct_bottom.enclosure_overhang1, adapted_bottom.enclosure_overhang1);
    EXPECT_EQ(direct_bottom.enclosure_overhang2, adapted_bottom.enclosure_overhang2);

    const auto& direct_cut = direct.viaRuleGenerateStorage().cutLayer(direct_id);
    const auto& adapted_cut = adapted.viaRuleGenerateStorage().cutLayer(adapted_id);
    EXPECT_EQ(direct.layerInfo(TechLayerId{direct_cut.layer.entity()}).name,
              adapted.layerInfo(TechLayerId{adapted_cut.layer.entity()}).name);
    EXPECT_EQ(direct_cut.flags, adapted_cut.flags);
    EXPECT_EQ(direct_cut.cut_rect, adapted_cut.cut_rect);
    EXPECT_EQ(direct_cut.spacing_x, adapted_cut.spacing_x);
    EXPECT_EQ(direct_cut.spacing_y, adapted_cut.spacing_y);
    EXPECT_DOUBLE_EQ(direct_cut.resistance_per_cut, adapted_cut.resistance_per_cut);

    const auto& direct_top = direct.viaRuleGenerateStorage().topLayer(direct_id);
    const auto& adapted_top = adapted.viaRuleGenerateStorage().topLayer(adapted_id);
    EXPECT_EQ(direct.layerInfo(direct_top.layer.layer()).name, adapted.layerInfo(adapted_top.layer.layer()).name);
    EXPECT_EQ(direct_top.flags & TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure,
              adapted_top.flags & TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure);
    EXPECT_EQ(direct_top.enclosure_overhang1, adapted_top.enclosure_overhang1);
    EXPECT_EQ(direct_top.enclosure_overhang2, adapted_top.enclosure_overhang2);
  }

  const auto adapted_vias = adapted.viaMasterStorage().viaMasters();
  for (const auto adapted_id : adapted_vias) {
    const auto& adapted_master = adapted.viaMasterStorage().viaMaster(adapted_id);
    SCOPED_TRACE("VIA " + adapted_master.name);
    const auto direct_id = direct.viaMasterStorage().findViaMaster(adapted_master.name);
    ASSERT_TRUE(direct_id);
    const auto& direct_master = direct.viaMasterStorage().viaMaster(direct_id);
    constexpr auto kLegacyMasterFlags = TechViaMasterFlag::kDefault | TechViaMasterFlag::kHasResistance;
    EXPECT_EQ(direct_master.flags & kLegacyMasterFlags, adapted_master.flags & kLegacyMasterFlags);
    EXPECT_DOUBLE_EQ(direct_master.resistance, adapted_master.resistance);
    EXPECT_EQ(direct.viaMasterStorage().hasGeneratedViaMaster(direct_id), adapted.viaMasterStorage().hasGeneratedViaMaster(adapted_id));

    const auto& direct_geometry = direct.viaMasterStorage().geometry(direct_id);
    const auto& adapted_geometry = adapted.viaMasterStorage().geometry(adapted_id);
    EXPECT_EQ(direct.layerInfo(direct_geometry.bottom_layer.layer()).name, adapted.layerInfo(adapted_geometry.bottom_layer.layer()).name);
    EXPECT_EQ(direct.layerInfo(TechLayerId{direct_geometry.cut_layer.entity()}).name,
              adapted.layerInfo(TechLayerId{adapted_geometry.cut_layer.entity()}).name);
    EXPECT_EQ(direct.layerInfo(direct_geometry.top_layer.layer()).name, adapted.layerInfo(adapted_geometry.top_layer.layer()).name);
    expectRectangleSets(direct.viaMasterStorage().bottomRects(direct_id), adapted.viaMasterStorage().bottomRects(adapted_id));
    expectRectangleSets(direct.viaMasterStorage().cutRects(direct_id), adapted.viaMasterStorage().cutRects(adapted_id));
    expectRectangleSets(direct.viaMasterStorage().topRects(direct_id), adapted.viaMasterStorage().topRects(adapted_id));

    if (direct.viaMasterStorage().hasGeneratedViaMaster(direct_id)
        && adapted.viaMasterStorage().hasGeneratedViaMaster(adapted_id)) {
      const auto& lhs = direct.viaMasterStorage().generatedViaMaster(direct_id);
      const auto& rhs = adapted.viaMasterStorage().generatedViaMaster(adapted_id);
      EXPECT_EQ(direct.viaRuleGenerateStorage().viaRuleGenerate(lhs.via_rule_generate).name,
                adapted.viaRuleGenerateStorage().viaRuleGenerate(rhs.via_rule_generate).name);
      EXPECT_EQ(lhs.cut_size_x, rhs.cut_size_x);
      EXPECT_EQ(lhs.cut_size_y, rhs.cut_size_y);
      EXPECT_EQ(lhs.cut_spacing_x, rhs.cut_spacing_x);
      EXPECT_EQ(lhs.cut_spacing_y, rhs.cut_spacing_y);
      EXPECT_EQ(lhs.bottom_enclosure_x, rhs.bottom_enclosure_x);
      EXPECT_EQ(lhs.bottom_enclosure_y, rhs.bottom_enclosure_y);
      EXPECT_EQ(lhs.top_enclosure_x, rhs.top_enclosure_x);
      EXPECT_EQ(lhs.top_enclosure_y, rhs.top_enclosure_y);
      EXPECT_EQ(lhs.row_count, rhs.row_count);
      EXPECT_EQ(lhs.column_count, rhs.column_count);
      EXPECT_EQ(lhs.origin_x, rhs.origin_x);
      EXPECT_EQ(lhs.origin_y, rhs.origin_y);
      EXPECT_EQ(lhs.bottom_offset_x, rhs.bottom_offset_x);
      EXPECT_EQ(lhs.bottom_offset_y, rhs.bottom_offset_y);
      EXPECT_EQ(lhs.top_offset_x, rhs.top_offset_x);
      EXPECT_EQ(lhs.top_offset_y, rhs.top_offset_y);
      EXPECT_EQ(lhs.pattern, rhs.pattern);
    }
  }
}

struct LegacyLogicalTerm
{
  ::idb::IdbTerm* attributes = nullptr;
  std::vector<::idb::IdbPort*> ports;
};

std::vector<LegacyLogicalTerm> normalizeLegacyTerms(::idb::IdbCellMaster& master)
{
  std::vector<LegacyLogicalTerm> result;
  std::unordered_map<std::string, std::size_t> positions;
  for (auto* term : master.get_term_list()) {
    if (term == nullptr) {
      ADD_FAILURE() << "legacy CellMaster contains a null TERM row";
      continue;
    }
    const auto [found, inserted] = positions.emplace(term->get_name(), result.size());
    if (inserted) {
      result.push_back(LegacyLogicalTerm{.attributes = term, .ports = term->get_port_list()});
      continue;
    }

    auto& target = result[found->second];
    EXPECT_EQ(term->get_direction(), target.attributes->get_direction()) << "duplicate PIN direction";
    EXPECT_EQ(term->get_type(), target.attributes->get_type()) << "duplicate PIN use";
    EXPECT_EQ(term->get_shape(), target.attributes->get_shape()) << "duplicate PIN shape";
    const auto& ports = term->get_port_list();
    target.ports.insert(target.ports.end(), ports.begin(), ports.end());
  }
  return result;
}

std::vector<::idb::IdbLayerShape*> legacyPortClauses(::idb::IdbPort& port)
{
  std::vector<::idb::IdbLayerShape*> result;
  for (auto* shape : port.get_layer_shape()) {
    const auto has_valid_rect = shape != nullptr && std::any_of(shape->get_rect_list().begin(), shape->get_rect_list().end(), [](auto* rect) {
                                  return rect != nullptr && rect->get_low_x() != rect->get_high_x() && rect->get_low_y() != rect->get_high_y();
                                });
    if (shape != nullptr && shape->get_layer() != nullptr && has_valid_rect) {
      result.push_back(shape);
    }
  }
  return result;
}

std::vector<::idb::IdbLayerShape*> legacyObsClauses(::idb::IdbCellMaster& master)
{
  std::vector<::idb::IdbLayerShape*> result;
  for (auto* obs : master.get_obs_list()) {
    if (obs == nullptr) {
      continue;
    }
    for (auto* obs_layer : obs->get_obs_layer_list()) {
      if (obs_layer == nullptr || obs_layer->get_shape() == nullptr || obs_layer->get_shape()->get_layer() == nullptr) {
        continue;
      }
      const auto& rects = obs_layer->get_shape()->get_rect_list();
      if (std::any_of(rects.begin(), rects.end(), [](auto* rect) {
            return rect != nullptr && rect->get_low_x() != rect->get_high_x() && rect->get_low_y() != rect->get_high_y();
          })) {
        result.push_back(obs_layer->get_shape());
      }
    }
  }
  return result;
}

void expectRectangles(std::span<const Rect> direct, const std::vector<::idb::IdbRect*>& legacy, int64_t offset_x = 0, int64_t offset_y = 0)
{
  std::vector<::idb::IdbRect*> valid_legacy;
  std::copy_if(legacy.begin(), legacy.end(), std::back_inserter(valid_legacy), [](auto* rect) {
    return rect != nullptr && rect->get_low_x() != rect->get_high_x() && rect->get_low_y() != rect->get_high_y();
  });
  if (direct.size() != valid_legacy.size()) {
    ADD_FAILURE() << "rectangle count differs: direct=" << direct.size() << " legacy=" << valid_legacy.size();
    return;
  }
  std::vector<Rect> normalized_direct;
  std::vector<Rect> normalized_legacy;
  normalized_direct.reserve(direct.size());
  normalized_legacy.reserve(legacy.size());
  bool same_order = true;
  for (std::size_t index = 0; index < direct.size(); ++index) {
    const Rect expected{.ll_x = valid_legacy[index]->get_low_x(),
                        .ll_y = valid_legacy[index]->get_low_y(),
                        .ur_x = valid_legacy[index]->get_high_x(),
                        .ur_y = valid_legacy[index]->get_high_y()};
    const Rect normalized{.ll_x = static_cast<int32_t>(direct[index].ll_x + offset_x),
                          .ll_y = static_cast<int32_t>(direct[index].ll_y + offset_y),
                          .ur_x = static_cast<int32_t>(direct[index].ur_x + offset_x),
                          .ur_y = static_cast<int32_t>(direct[index].ur_y + offset_y)};
    normalized_direct.push_back(normalized.normalized());
    normalized_legacy.push_back(expected.normalized());
    same_order = same_order && normalized_direct.back() == normalized_legacy.back();
  }
  if (same_order) {
    return;
  }

  const auto less = [](Rect lhs, Rect rhs) {
    if (lhs.ll_x != rhs.ll_x) {
      return lhs.ll_x < rhs.ll_x;
    }
    if (lhs.ll_y != rhs.ll_y) {
      return lhs.ll_y < rhs.ll_y;
    }
    if (lhs.ur_x != rhs.ur_x) {
      return lhs.ur_x < rhs.ur_x;
    }
    return lhs.ur_y < rhs.ur_y;
  };
  std::sort(normalized_direct.begin(), normalized_direct.end(), less);
  std::sort(normalized_legacy.begin(), normalized_legacy.end(), less);
  for (std::size_t index = 0; index < normalized_direct.size(); ++index) {
    if (normalized_direct[index] == normalized_legacy[index]) {
      continue;
    }
    ADD_FAILURE() << "rectangle set differs after coordinate/order normalization at " << index << ": direct=("
                  << normalized_direct[index].ll_x << ',' << normalized_direct[index].ll_y << ',' << normalized_direct[index].ur_x << ','
                  << normalized_direct[index].ur_y << ") legacy=(" << normalized_legacy[index].ll_x << ',' << normalized_legacy[index].ll_y
                  << ',' << normalized_legacy[index].ur_x << ',' << normalized_legacy[index].ur_y << ')';
    return;
  }
}

void expectSites(::idb::IdbLayout& legacy, const LibraryStore& direct)
{
  const auto& legacy_sites = legacy.get_sites()->get_site_list();
  ASSERT_EQ(direct.siteStorage().siteCount(), legacy_sites.size());
  for (auto* legacy_site : legacy_sites) {
    ASSERT_NE(legacy_site, nullptr);
    SCOPED_TRACE(legacy_site->get_name());
    const auto direct_id = direct.siteStorage().findSite(legacy_site->get_name());
    ASSERT_TRUE(direct_id);
    const auto& site = direct.siteStorage().site(direct_id);
    EXPECT_EQ(site.name, legacy_site->get_name());
    EXPECT_EQ(site.width, legacy_site->get_width());
    EXPECT_EQ(site.height, legacy_site->get_height());
    EXPECT_EQ(site.overlap, legacy_site->is_overlap());
    EXPECT_EQ(static_cast<uint8_t>(site.site_class), static_cast<uint8_t>(legacy_site->get_site_class()));
    EXPECT_EQ(site.symmetry_x, legacy_site->get_symmetry() == ::idb::IdbSymmetry::kX);
    EXPECT_EQ(site.symmetry_y, legacy_site->get_symmetry() == ::idb::IdbSymmetry::kY);
    EXPECT_EQ(site.symmetry_r90, legacy_site->get_symmetry() == ::idb::IdbSymmetry::kR90);
  }
}

void expectPort(const TechStore& direct_tech, const LibraryStore& direct_library, LibraryMasterTermId direct_term_id,
                LibraryMasterPortId direct_port_id, ::idb::IdbPort& legacy_port, int64_t origin_x, int64_t origin_y)
{
  const auto& port = direct_library.masterPortStorage().masterPort(direct_port_id);
  EXPECT_EQ(direct_library.masterPortStorage().owner(direct_port_id), direct_term_id);
  EXPECT_EQ(static_cast<uint8_t>(port.port_class), static_cast<uint8_t>(legacy_port.get_port_class()));
  ASSERT_EQ(port.vias.size(), legacy_port.get_via_list().size());
  for (std::size_t index = 0; index < port.vias.size(); ++index) {
    auto* legacy_via = legacy_port.get_via_list()[index];
    ASSERT_NE(legacy_via, nullptr);
    ASSERT_NE(legacy_via->get_coordinate(), nullptr);
    EXPECT_EQ(direct_tech.viaMasterStorage().viaMaster(port.vias[index].via).name, legacy_via->get_name());
    EXPECT_EQ(port.vias[index].origin.x, legacy_via->get_coordinate()->get_x());
    EXPECT_EQ(port.vias[index].origin.y, legacy_via->get_coordinate()->get_y());
  }

  struct DirectLayerGroup
  {
    TechLayerId layer;
    std::vector<Rect> rects;
  };
  std::vector<DirectLayerGroup> direct_groups;
  for (const auto& clause : port.layer_clauses) {
    EXPECT_EQ(direct_library.geometryPool().polygonCount(clause.geometry), 0u);
    auto group = std::find_if(direct_groups.begin(), direct_groups.end(),
                              [&](const DirectLayerGroup& candidate) { return candidate.layer == clause.layer; });
    if (group == direct_groups.end()) {
      direct_groups.push_back(DirectLayerGroup{.layer = clause.layer});
      group = std::prev(direct_groups.end());
    }
    const auto rects = direct_library.geometryPool().rectangles(clause.geometry);
    group->rects.insert(group->rects.end(), rects.begin(), rects.end());
  }

  const auto legacy_clauses = legacyPortClauses(legacy_port);
  ASSERT_EQ(direct_groups.size(), legacy_clauses.size());
  for (std::size_t clause_index = 0; clause_index < direct_groups.size(); ++clause_index) {
    SCOPED_TRACE("PORT LAYER clause " + std::to_string(clause_index));
    const auto& direct_group = direct_groups[clause_index];
    auto* legacy_clause = legacy_clauses[clause_index];
    ASSERT_NE(legacy_clause, nullptr);
    EXPECT_EQ(direct_tech.layerInfo(direct_group.layer).name, legacy_clause->get_layer()->get_name());
    expectRectangles(direct_group.rects, legacy_clause->get_rect_list(), origin_x, origin_y);
  }
}

void expectMaster(const TechStore& direct_tech, const LibraryStore& direct_library, ::idb::IdbCellMaster& legacy_master)
{
  const auto master_id = direct_library.cellMasterStorage().findCellMaster(legacy_master.get_name());
  ASSERT_TRUE(master_id);
  const auto& master = direct_library.cellMasterStorage().cellMaster(master_id);
  EXPECT_EQ(master.name, legacy_master.get_name());
  EXPECT_EQ(static_cast<uint8_t>(master.type), static_cast<uint8_t>(legacy_master.get_type()));
  EXPECT_EQ(master.core_filler, legacy_master.is_core_filler());
  EXPECT_EQ(master.pad_filler, legacy_master.is_pad_filler());
  EXPECT_EQ(master.symmetry_x, legacy_master.is_symmetry_x());
  EXPECT_EQ(master.symmetry_y, legacy_master.is_symmetry_y());
  EXPECT_EQ(master.symmetry_r90, legacy_master.is_symmetry_R90());
  EXPECT_EQ(master.origin_x, legacy_master.get_origin_x());
  EXPECT_EQ(master.origin_y, legacy_master.get_origin_y());
  EXPECT_EQ(master.width, legacy_master.get_width());
  EXPECT_EQ(master.height, legacy_master.get_height());
  if (legacy_master.get_site() == nullptr) {
    EXPECT_FALSE(master.site.has_value());
  } else {
    ASSERT_TRUE(master.site.has_value());
    EXPECT_EQ(direct_library.siteStorage().site(*master.site).name, legacy_master.get_site()->get_name());
  }

  const auto legacy_terms = normalizeLegacyTerms(legacy_master);
  ASSERT_EQ(master.terms.size(), legacy_terms.size());
  for (std::size_t term_index = 0; term_index < master.terms.size(); ++term_index) {
    const auto term_id = master.terms[term_index];
    const auto& term = direct_library.masterTermStorage().masterTerm(term_id);
    auto* legacy_term = legacy_terms[term_index].attributes;
    ASSERT_NE(legacy_term, nullptr);
    SCOPED_TRACE("PIN " + term.name);
    EXPECT_EQ(direct_library.masterTermStorage().owner(term_id), master_id);
    EXPECT_EQ(term.master, master_id);
    EXPECT_EQ(term.name, legacy_term->get_name());
    EXPECT_EQ(static_cast<uint8_t>(term.direction), static_cast<uint8_t>(legacy_term->get_direction()));
    EXPECT_EQ(static_cast<uint8_t>(term.use), static_cast<uint8_t>(legacy_term->get_type()));
    EXPECT_EQ(static_cast<uint8_t>(term.shape), static_cast<uint8_t>(legacy_term->get_shape()));
    EXPECT_EQ(legacy_term->get_cell_master(), &legacy_master);
    ASSERT_EQ(term.ports.size(), legacy_terms[term_index].ports.size());
    for (std::size_t port_index = 0; port_index < term.ports.size(); ++port_index) {
      SCOPED_TRACE("PORT " + std::to_string(port_index));
      auto* legacy_port = legacy_terms[term_index].ports[port_index];
      ASSERT_NE(legacy_port, nullptr);
      expectPort(direct_tech, direct_library, term_id, term.ports[port_index], *legacy_port, master.origin_x, master.origin_y);
    }
  }

  const auto legacy_obs = legacyObsClauses(legacy_master);
  EXPECT_EQ(direct_library.cellMasterStorage().hasObs(master_id), !legacy_obs.empty());
  if (!legacy_obs.empty()) {
    const auto& obs = direct_library.cellMasterStorage().obs(master_id);
    ASSERT_EQ(obs.layer_clauses.size(), legacy_obs.size());
    EXPECT_TRUE(obs.vias.empty());
    for (std::size_t clause_index = 0; clause_index < obs.layer_clauses.size(); ++clause_index) {
      SCOPED_TRACE("OBS LAYER clause " + std::to_string(clause_index));
      const auto& direct_clause = obs.layer_clauses[clause_index];
      auto* legacy_clause = legacy_obs[clause_index];
      ASSERT_NE(legacy_clause, nullptr);
      EXPECT_EQ(direct_tech.layerInfo(direct_clause.layer).name, legacy_clause->get_layer()->get_name());
      EXPECT_EQ(direct_library.geometryPool().polygonCount(direct_clause.geometry), 0u);
      expectRectangles(direct_library.geometryPool().rectangles(direct_clause.geometry), legacy_clause->get_rect_list(), master.origin_x,
                       master.origin_y);
    }
  }
}

void expectLibrary(::idb::IdbLayout& legacy, const TechStore& direct_tech, const LibraryStore& direct_library)
{
  EXPECT_EQ(direct_library.geometryPool().polygonCount(), 0u);
  EXPECT_EQ(direct_library.geometryPool().pointCount(), 0u);
  expectSites(legacy, direct_library);

  const auto& legacy_masters = legacy.get_cell_master_list()->get_cell_master();
  ASSERT_EQ(direct_library.cellMasterStorage().cellMasterCount(), legacy_masters.size());
  for (auto* legacy_master : legacy_masters) {
    ASSERT_NE(legacy_master, nullptr);
    SCOPED_TRACE(legacy_master->get_name());
    expectMaster(direct_tech, direct_library, *legacy_master);
  }
}

void requireCorpusFiles(const lef_test::LefPdkDomain& domain)
{
  if (!std::filesystem::exists(domain.technology)) {
    throw std::runtime_error("missing technology LEF: " + domain.technology.string());
  }
  for (const auto& cell : domain.cells) {
    if (!std::filesystem::exists(cell)) {
      throw std::runtime_error("missing cell LEF: " + cell.string());
    }
  }
}

void compareDomain(const lef_test::LefPdkDomain& domain)
{
  requireCorpusFiles(domain);
  const GeometryPoolOptions geometry_options{.polygon_mode = PolygonStorageMode::kRectangularized};

  TechStore direct_tech{TechStoreOptions{.geometry = geometry_options}};
  LefTechImporter(direct_tech).import(domain.technology);
  LibraryStore direct_library{direct_tech.techRegistry(), LibraryStoreOptions{.geometry = geometry_options}};
  std::vector<std::filesystem::path> direct_files;
  direct_files.reserve(domain.cells.size() + 1u);
  direct_files.push_back(domain.technology);
  direct_files.insert(direct_files.end(), domain.cells.begin(), domain.cells.end());
  LefLibraryImporter(direct_tech, direct_library).import(direct_files);

  LegacyLefReader builder;
  std::vector<std::string> technology_files{domain.technology.string()};
  auto* service = builder.buildLef(technology_files, true);
  if (service == nullptr || service->get_layout() == nullptr) {
    throw std::runtime_error("legacy iDB failed to load technology LEF for " + domain.name);
  }
  std::vector<std::string> cell_files;
  cell_files.reserve(domain.cells.size());
  for (const auto& cell : domain.cells) {
    cell_files.push_back(cell.string());
  }
  service = builder.buildLef(cell_files, false);
  if (service == nullptr || service->get_layout() == nullptr) {
    throw std::runtime_error("legacy iDB failed to load cell LEFs for " + domain.name);
  }
  auto& legacy = *service->get_layout();

  TechStore adapted_tech{TechStoreOptions{.geometry = geometry_options}};
  IdbTechImporter adapted_tech_importer(adapted_tech);
  adapted_tech_importer.import(legacy);
  LibraryStore adapted_library{adapted_tech.techRegistry(), LibraryStoreOptions{.geometry = geometry_options}};
  IdbLibraryImporter(adapted_library, adapted_tech_importer).import(legacy);
  expectTechnologyCommonSubset(direct_tech, adapted_tech);
  expectLibrary(legacy, direct_tech, direct_library);
  expectLibrary(legacy, adapted_tech, adapted_library);
}

void expectDomainInIsolatedProcess(const lef_test::LefPdkDomain& domain)
{
  std::cout.flush();
  std::cerr.flush();
  std::fflush(nullptr);
  const pid_t child = fork();
  ASSERT_GE(child, 0) << "fork failed for " << domain.name;
  if (child == 0) {
    int exit_code = 0;
    try {
      compareDomain(domain);
      exit_code = testing::Test::HasFailure() ? 1 : 0;
    } catch (const std::exception& error) {
      std::cerr << domain.name << ": " << error.what() << '\n';
      exit_code = 2;
    } catch (...) {
      std::cerr << domain.name << ": unknown semantic-test failure\n";
      exit_code = 3;
    }
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
    _exit(exit_code);
  }

  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status)) << domain.name << " semantic child terminated abnormally";
  EXPECT_EQ(WEXITSTATUS(status), 0) << domain.name << " semantic child failed";
}

class TemporaryLef
{
 public:
  explicit TemporaryLef(std::string_view contents)
  {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    _path = std::filesystem::temp_directory_path() / ("idb-refactor-fixed-point-" + std::to_string(nonce) + ".lef");
    std::ofstream output(_path);
    if (!output) {
      throw std::runtime_error("failed to create canonical LEF test file");
    }
    output << contents;
    if (!output) {
      throw std::runtime_error("failed to write canonical LEF test file");
    }
  }

  ~TemporaryLef()
  {
    std::error_code error;
    std::filesystem::remove(_path, error);
  }

  TemporaryLef(const TemporaryLef&) = delete;
  TemporaryLef& operator=(const TemporaryLef&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return _path; }

 private:
  std::filesystem::path _path;
};

void expectCanonicalFixedPoint(const lef_test::LefPdkDomain& domain)
{
  requireCorpusFiles(domain);
  TechStore source;
  LefTechImporter(source).import(domain.technology);

  std::ostringstream first;
  LefTechExporter::write(first, source);
  const TemporaryLef canonical(first.str());

  TechStore reimported;
  LefTechImporter(reimported).import(canonical.path());
  std::ostringstream second;
  LefTechExporter::write(second, reimported);
  EXPECT_EQ(second.str(), first.str());
}

TEST(LefFullCorpusSemanticDifferentialTest, MatchesEverySharedSky130FieldAndRectangle)
{
  const auto corpus = lef_test::fullSky130Corpus(std::filesystem::path{ECC_TOOLS_SOURCE_DIR});
  for (const auto& domain : corpus) {
    SCOPED_TRACE(domain.name);
    expectDomainInIsolatedProcess(domain);
  }
}

TEST(LefFullCorpusSemanticDifferentialTest, MatchesEverySharedIhp130FieldAndRectangle)
{
  const auto corpus = lef_test::fullIhp130Corpus(std::filesystem::path{ECC_TOOLS_SOURCE_DIR});
  for (const auto& domain : corpus) {
    SCOPED_TRACE(domain.name);
    expectDomainInIsolatedProcess(domain);
  }
}

TEST(LefFullCorpusSemanticDifferentialTest, MatchesOpenRoadOdbGscl45TechnologyWhenAvailable)
{
  const auto lef = std::filesystem::path{ECC_TOOLS_SOURCE_DIR}.parent_path() / "OpenROAD/src/odb/test/data/gscl45nm.lef";
  if (!std::filesystem::exists(lef)) GTEST_SKIP() << "OpenROAD ODB corpus is not checked out next to ecc-tools";

  TechStore direct;
  ASSERT_NO_THROW(LefTechImporter(direct).import(lef));

  LegacyLefReader builder;
  std::vector<std::string> files{lef.string()};
  auto* service = builder.buildLef(files, true);
  ASSERT_NE(service, nullptr);
  ASSERT_NE(service->get_layout(), nullptr);

  TechStore adapted;
  ASSERT_NO_THROW(IdbTechImporter(adapted).import(*service->get_layout()));
  expectTechnologyCommonSubset(direct, adapted);
}

TEST(LefFullCorpusSemanticDifferentialTest, TechCanonicalExportReachesAFixedPointForEveryDomain)
{
  auto corpus = lef_test::fullSky130Corpus(std::filesystem::path{ECC_TOOLS_SOURCE_DIR});
  auto ihp = lef_test::fullIhp130Corpus(std::filesystem::path{ECC_TOOLS_SOURCE_DIR});
  corpus.insert(corpus.end(), ihp.begin(), ihp.end());
  for (const auto& domain : corpus) {
    SCOPED_TRACE(domain.name);
    expectCanonicalFixedPoint(domain);
  }
}

}  // namespace
}  // namespace eccdb
