#include "lef/detail/LefTechLayerRules.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "lefiLayer.hpp"
#include "tech/cut_layer/storage/CutLayerStorage.h"
#include "tech/routing_layer/storage/RoutingLayerStorage.h"

namespace eccdb::lef_detail {
namespace {

std::string upper(const char* value)
{
  std::string result = value == nullptr ? std::string{} : std::string(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  return result;
}

int32_t toDatabaseUnits(double value, int32_t database_units_per_micron, const char* field)
{
  if (database_units_per_micron <= 0) {
    throw std::invalid_argument("database units per micron must be positive");
  }
  if (!std::isfinite(value) || value < 0.0) {
    throw std::runtime_error(std::string(field) + " must be non-negative");
  }
  const auto scaled = value * static_cast<double>(database_units_per_micron);
  if (!std::isfinite(scaled) || scaled > static_cast<double>(std::numeric_limits<int32_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside int32 DBU range");
  }
  return static_cast<int32_t>(std::llround(scaled));
}

int64_t toDatabaseArea(double value, int32_t database_units_per_micron, const char* field)
{
  if (database_units_per_micron <= 0) {
    throw std::invalid_argument("database units per micron must be positive");
  }
  if (!std::isfinite(value) || value < 0.0) {
    throw std::runtime_error(std::string(field) + " must be non-negative");
  }
  const auto units = static_cast<long double>(database_units_per_micron);
  const auto scaled = value * units * units;
  if (!std::isfinite(scaled) || scaled > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside int64 DBU area range");
  }
  return static_cast<int64_t>(std::llround(scaled));
}

TechRoutingMinStepType routingMinStepType(const char* value)
{
  const auto type = upper(value);
  if (type == "INSIDECORNER") {
    return TechRoutingMinStepType::kInsideCorner;
  }
  if (type == "OUTSIDECORNER") {
    return TechRoutingMinStepType::kOutsideCorner;
  }
  if (type == "STEP") {
    return TechRoutingMinStepType::kStep;
  }
  return TechRoutingMinStepType::kNone;
}

TechRoutingMinimumCutOrient routingMinimumCutOrient(const char* value)
{
  const auto orient = upper(value);
  if (orient == "FROMABOVE") {
    return TechRoutingMinimumCutOrient::kFromAbove;
  }
  if (orient == "FROMBELOW") {
    return TechRoutingMinimumCutOrient::kFromBelow;
  }
  return TechRoutingMinimumCutOrient::kNone;
}

TechRoutingCurrentDensityType routingCurrentDensityType(const char* value)
{
  const auto type = upper(value);
  if (type == "PEAK") {
    return TechRoutingCurrentDensityType::kPeak;
  }
  if (type == "AVERAGE") {
    return TechRoutingCurrentDensityType::kAverage;
  }
  if (type == "RMS") {
    return TechRoutingCurrentDensityType::kRms;
  }
  return TechRoutingCurrentDensityType::kUnknown;
}

TechCutCurrentDensityType cutCurrentDensityType(const char* value)
{
  const auto type = upper(value);
  if (type == "PEAK") {
    return TechCutCurrentDensityType::kPeak;
  }
  if (type == "AVERAGE") {
    return TechCutCurrentDensityType::kAverage;
  }
  if (type == "RMS") {
    return TechCutCurrentDensityType::kRms;
  }
  return TechCutCurrentDensityType::kUnknown;
}

StagedRoutingCurrentDensity stageRoutingCurrentDensity(const lefiLayerDensity& source, TechRoutingCurrentDensitySignal signal)
{
  StagedRoutingCurrentDensity result{.signal = signal, .type = routingCurrentDensityType(source.type())};
  result.has_scalar = source.hasOneEntry() != 0;
  if (result.has_scalar) {
    result.scalar = source.oneEntry();
  }
  result.frequencies.reserve(static_cast<size_t>(source.numFrequency()));
  for (int index = 0; index < source.numFrequency(); ++index) {
    result.frequencies.push_back(source.frequency(index));
  }
  result.widths.reserve(static_cast<size_t>(source.numWidths()));
  for (int index = 0; index < source.numWidths(); ++index) {
    result.widths.push_back(source.width(index));
  }
  result.table_entries.reserve(static_cast<size_t>(source.numTableEntries()));
  for (int index = 0; index < source.numTableEntries(); ++index) {
    result.table_entries.push_back(source.tableEntry(index));
  }
  return result;
}

StagedCutCurrentDensity stageCutCurrentDensity(const lefiLayerDensity& source, TechCutCurrentDensitySignal signal)
{
  StagedCutCurrentDensity result{.signal = signal, .type = cutCurrentDensityType(source.type())};
  result.has_scalar = source.hasOneEntry() != 0;
  if (result.has_scalar) {
    result.scalar = source.oneEntry();
  }
  result.frequencies.reserve(static_cast<size_t>(source.numFrequency()));
  for (int index = 0; index < source.numFrequency(); ++index) {
    result.frequencies.push_back(source.frequency(index));
  }
  result.cut_areas.reserve(static_cast<size_t>(source.numCutareas()));
  for (int index = 0; index < source.numCutareas(); ++index) {
    result.cut_areas.push_back(source.cutArea(index));
  }
  result.table_entries.reserve(static_cast<size_t>(source.numTableEntries()));
  for (int index = 0; index < source.numTableEntries(); ++index) {
    result.table_entries.push_back(source.tableEntry(index));
  }
  return result;
}

void stageRoutingSpacingTables(lefiLayer& source, StagedRoutingRules& result)
{
  for (int table_index = 0; table_index < source.numSpacingTable(); ++table_index) {
    const auto* table = source.spacingTable(table_index);
    if (table == nullptr) {
      throw std::runtime_error("SI2 returned a null ROUTING SPACINGTABLE");
    }
    if (table->isParallel()) {
      const auto* parallel = table->parallel();
      if (parallel == nullptr) {
        throw std::runtime_error("SI2 returned a null PARALLELRUNLENGTH table");
      }
      StagedRoutingPrlSpacingTable staged;
      staged.parallel_run_lengths.reserve(static_cast<size_t>(parallel->numLength()));
      for (int index = 0; index < parallel->numLength(); ++index) {
        staged.parallel_run_lengths.push_back(parallel->length(index));
      }
      staged.widths.reserve(static_cast<size_t>(parallel->numWidth()));
      staged.cells.reserve(static_cast<size_t>(parallel->numWidth()) * static_cast<size_t>(parallel->numLength()));
      for (int width_index = 0; width_index < parallel->numWidth(); ++width_index) {
        staged.widths.push_back(parallel->width(width_index));
        for (int length_index = 0; length_index < parallel->numLength(); ++length_index) {
          staged.cells.push_back(parallel->widthSpacing(width_index, length_index));
        }
      }
      result.prl_spacing_tables.push_back(std::move(staged));
      continue;
    }
    if (table->isInfluence()) {
      const auto* influence = table->influence();
      if (influence == nullptr) {
        throw std::runtime_error("SI2 returned a null INFLUENCE table");
      }
      StagedRoutingInfluenceSpacingTable staged;
      staged.entries.reserve(static_cast<size_t>(influence->numInfluenceEntry()));
      for (int index = 0; index < influence->numInfluenceEntry(); ++index) {
        staged.entries.push_back(std::array<double, 3>{influence->width(index), influence->distance(index), influence->spacing(index)});
      }
      result.influence_spacing_tables.push_back(std::move(staged));
      continue;
    }

    const auto* two_widths = table->twoWidths();
    if (two_widths == nullptr) {
      throw std::runtime_error("SI2 returned an unknown ROUTING SPACINGTABLE form");
    }
    StagedRoutingTwoWidthsSpacingTable staged;
    staged.widths.reserve(static_cast<size_t>(two_widths->numWidth()));
    for (int width_index = 0; width_index < two_widths->numWidth(); ++width_index) {
      StagedRoutingTwoWidthsAxis axis{.width = two_widths->width(width_index)};
      if (two_widths->hasWidthPRL(width_index)) {
        axis.prl = two_widths->widthPRL(width_index);
      }
      axis.spacings.reserve(static_cast<size_t>(two_widths->numWidthSpacing(width_index)));
      for (int spacing_index = 0; spacing_index < two_widths->numWidthSpacing(width_index); ++spacing_index) {
        axis.spacings.push_back(two_widths->widthSpacing(width_index, spacing_index));
      }
      staged.widths.push_back(std::move(axis));
    }
    result.two_widths_spacing_tables.push_back(std::move(staged));
  }
}

}  // namespace

StagedRoutingRules stageRoutingRules(lefiLayer& source)
{
  StagedRoutingRules result;
  result.spacing_rules.reserve(static_cast<size_t>(source.numSpacing()));
  for (int index = 0; index < source.numSpacing(); ++index) {
    if (source.hasSpacingEndOfLine(index)) {
      StagedRoutingEndOfLineSpacingRule rule{.min_spacing = source.spacing(index),
                                             .eol_width = source.spacingEolWidth(index),
                                             .eol_within = source.spacingEolWithin(index),
                                             .has_parallel_edge = source.hasSpacingParellelEdge(index) != 0};
      if (rule.has_parallel_edge) {
        rule.parallel_space = source.spacingParSpace(index);
        rule.parallel_within = source.spacingParWithin(index);
        rule.two_edges = source.hasSpacingTwoEdges(index) != 0;
      }
      result.end_of_line_spacing_rules.push_back(rule);
      continue;
    }
    StagedRoutingSpacingRule rule{.min_spacing = source.spacing(index)};
    if (source.hasSpacingRange(index)) {
      rule.range = std::pair{source.spacingRangeMin(index), source.spacingRangeMax(index)};
    }
    if (source.hasSpacingNotchLength(index)) {
      rule.notch_length = source.spacingNotchLength(index);
    }
    result.spacing_rules.push_back(std::move(rule));
  }

  result.min_enclose_area_rules.reserve(static_cast<size_t>(source.numMinenclosedarea()));
  for (int index = 0; index < source.numMinenclosedarea(); ++index) {
    StagedRoutingMinEncloseAreaRule rule{.area = source.minenclosedarea(index)};
    if (source.hasMinenclosedareaWidth(index)) {
      rule.width = source.minenclosedareaWidth(index);
    }
    result.min_enclose_area_rules.push_back(std::move(rule));
  }

  result.min_step_rules.reserve(static_cast<size_t>(source.numMinstep()));
  for (int index = 0; index < source.numMinstep(); ++index) {
    StagedRoutingMinStepRule rule{.min_step_length = source.minstep(index)};
    if (source.hasMinstepType(index)) {
      rule.type = routingMinStepType(source.minstepType(index));
    }
    if (source.hasMinstepLengthsum(index)) {
      rule.max_length_sum = source.minstepLengthsum(index);
    }
    if (source.hasMinstepMaxedges(index)) {
      rule.max_edges = source.minstepMaxedges(index);
    }
    result.min_step_rules.push_back(std::move(rule));
  }

  result.minimum_cut_rules.reserve(static_cast<size_t>(source.numMinimumcut()));
  for (int index = 0; index < source.numMinimumcut(); ++index) {
    StagedRoutingMinimumCutRule rule{.num_cuts = source.minimumcut(index), .width = source.minimumcutWidth(index)};
    if (source.hasMinimumcutWithin(index)) {
      rule.within_cut_distance = source.minimumcutWithin(index);
    }
    if (source.hasMinimumcutConnection(index)) {
      rule.orient = routingMinimumCutOrient(source.minimumcutConnection(index));
    }
    if (source.hasMinimumcutNumCuts(index)) {
      rule.length = std::pair{source.minimumcutLength(index), source.minimumcutDistance(index)};
    }
    result.minimum_cut_rules.push_back(std::move(rule));
  }

  stageRoutingSpacingTables(source, result);
  result.current_density_rules.reserve(static_cast<size_t>(source.numAccurrentDensity() + source.numDccurrentDensity()));
  for (int index = 0; index < source.numAccurrentDensity(); ++index) {
    const auto* density = source.accurrent(index);
    if (density == nullptr) {
      throw std::runtime_error("SI2 returned a null ROUTING ACCURRENTDENSITY");
    }
    result.current_density_rules.push_back(stageRoutingCurrentDensity(*density, TechRoutingCurrentDensitySignal::kAc));
  }
  for (int index = 0; index < source.numDccurrentDensity(); ++index) {
    const auto* density = source.dccurrent(index);
    if (density == nullptr) {
      throw std::runtime_error("SI2 returned a null ROUTING DCCURRENTDENSITY");
    }
    result.current_density_rules.push_back(stageRoutingCurrentDensity(*density, TechRoutingCurrentDensitySignal::kDc));
  }
  return result;
}

StagedCutRules stageCutRules(lefiLayer& source)
{
  StagedCutRules result;
  result.spacing_rules.reserve(static_cast<size_t>(source.numSpacing()));
  for (int index = 0; index < source.numSpacing(); ++index) {
    StagedCutSpacingRule rule{.spacing = source.spacing(index),
                              .same_net = source.hasSpacingSamenet(index) != 0,
                              .center_to_center = source.hasSpacingCenterToCenter(index) != 0,
                              .same_net_pg_only = source.hasSpacingSamenetPGonly(index) != 0,
                              .stack = source.hasSpacingLayerStack(index) != 0,
                              .except_same_pg_net = source.hasSpacingAdjacentExcept(index) != 0,
                              .parallel_overlap = source.hasSpacingParallelOverlap(index) != 0};
    if (source.hasSpacingName(index)) {
      rule.second_layer_name = source.spacingName(index);
    }
    if (source.hasSpacingArea(index)) {
      rule.cut_area = source.spacingArea(index);
    }
    if (source.hasSpacingAdjacent(index)) {
      rule.adjacent_cuts = std::pair{static_cast<uint32_t>(source.spacingAdjacentCuts(index)), source.spacingAdjacentWithin(index)};
    }
    result.spacing_rules.push_back(std::move(rule));
  }

  result.enclosure_rules.reserve(static_cast<size_t>(source.numEnclosure()));
  for (int index = 0; index < source.numEnclosure(); ++index) {
    CutLayerSide side = CutLayerSide::kUnknown;
    if (source.hasEnclosureRule(index)) {
      const auto side_name = upper(source.enclosureRule(index));
      if (side_name == "ABOVE") {
        side = CutLayerSide::kAbove;
      } else if (side_name == "BELOW") {
        side = CutLayerSide::kBelow;
      }
    }
    StagedCutEnclosureRule rule{
        .side = side, .overhang1 = source.enclosureOverhang1(index), .overhang2 = source.enclosureOverhang2(index)};
    if (source.hasEnclosureWidth(index)) {
      rule.min_width = source.enclosureMinWidth(index);
    }
    if (source.hasEnclosureExceptExtraCut(index)) {
      rule.except_extra_cut = source.enclosureExceptExtraCut(index);
    }
    if (source.hasEnclosureMinLength(index)) {
      rule.min_length = source.enclosureMinLength(index);
    }
    result.enclosure_rules.push_back(std::move(rule));
  }

  if (source.hasArraySpacing()) {
    StagedCutArraySpacingRule rule{.long_array = source.hasLongArray() != 0, .cut_spacing = source.cutSpacing()};
    if (source.hasViaWidth()) {
      rule.via_width = source.viaWidth();
    }
    rule.items.reserve(static_cast<size_t>(source.numArrayCuts()));
    for (int index = 0; index < source.numArrayCuts(); ++index) {
      rule.items.push_back(StagedCutArraySpacingItem{.array_cut_count = static_cast<uint32_t>(source.arrayCuts(index)),
                                                     .spacing = source.arraySpacing(index)});
    }
    result.array_spacing_rule = std::move(rule);
  }

  if (source.hasSpacingTableOrtho()) {
    const auto* table = source.orthogonal();
    if (table == nullptr) {
      throw std::runtime_error("SI2 returned a null CUT SPACINGTABLE ORTHOGONAL");
    }
    result.orthogonal_spacing_table.reserve(static_cast<size_t>(table->numOrthogonal()));
    for (int index = 0; index < table->numOrthogonal(); ++index) {
      result.orthogonal_spacing_table.push_back(
          StagedCutOrthogonalSpacingTableItem{.within = table->cutWithin(index), .spacing = table->orthoSpacing(index)});
    }
  }

  result.current_density_rules.reserve(static_cast<size_t>(source.numAccurrentDensity() + source.numDccurrentDensity()));
  for (int index = 0; index < source.numAccurrentDensity(); ++index) {
    const auto* density = source.accurrent(index);
    if (density == nullptr) {
      throw std::runtime_error("SI2 returned a null CUT ACCURRENTDENSITY");
    }
    result.current_density_rules.push_back(stageCutCurrentDensity(*density, TechCutCurrentDensitySignal::kAc));
  }
  for (int index = 0; index < source.numDccurrentDensity(); ++index) {
    const auto* density = source.dccurrent(index);
    if (density == nullptr) {
      throw std::runtime_error("SI2 returned a null CUT DCCURRENTDENSITY");
    }
    result.current_density_rules.push_back(stageCutCurrentDensity(*density, TechCutCurrentDensitySignal::kDc));
  }
  return result;
}

PreparedRoutingRules prepareRoutingRules(const StagedRoutingRules& source, int32_t database_units_per_micron)
{
  PreparedRoutingRules result;
  result.spacing_rules.reserve(source.spacing_rules.size());
  for (const auto& source_rule : source.spacing_rules) {
    TechRoutingSpacingRule rule{.min_spacing = toDatabaseUnits(source_rule.min_spacing, database_units_per_micron, "ROUTING SPACING")};
    if (source_rule.range) {
      rule.type = TechRoutingSpacingType::kRange;
      rule.min_width = toDatabaseUnits(source_rule.range->first, database_units_per_micron, "ROUTING SPACING RANGE minimum width");
      rule.max_width = toDatabaseUnits(source_rule.range->second, database_units_per_micron, "ROUTING SPACING RANGE maximum width");
    }
    if (source_rule.notch_length) {
      result.spacing_notch_length_rules.push_back(TechRoutingSpacingNotchLengthRule{
          .min_spacing = rule.min_spacing,
          .notch_length = toDatabaseUnits(*source_rule.notch_length, database_units_per_micron, "ROUTING SPACING NOTCHLENGTH")});
    } else {
      result.spacing_rules.push_back(rule);
    }
  }

  result.end_of_line_spacing_rules.reserve(source.end_of_line_spacing_rules.size());
  for (const auto& source_rule : source.end_of_line_spacing_rules) {
    TechRoutingEndOfLineSpacingRule rule{
        .min_spacing = toDatabaseUnits(source_rule.min_spacing, database_units_per_micron, "ROUTING SPACING ENDOFLINE spacing"),
        .eol_width = toDatabaseUnits(source_rule.eol_width, database_units_per_micron, "ROUTING SPACING ENDOFLINE width"),
        .eol_within = toDatabaseUnits(source_rule.eol_within, database_units_per_micron, "ROUTING SPACING ENDOFLINE WITHIN")};
    if (source_rule.has_parallel_edge) {
      rule.flags |= TechRoutingEndOfLineSpacingRuleFlag::kHasParallelEdge;
      rule.parallel_space
          = toDatabaseUnits(source_rule.parallel_space, database_units_per_micron, "ROUTING SPACING PARALLELEDGE spacing");
      rule.parallel_within
          = toDatabaseUnits(source_rule.parallel_within, database_units_per_micron, "ROUTING SPACING PARALLELEDGE WITHIN");
      if (source_rule.two_edges) {
        rule.flags |= TechRoutingEndOfLineSpacingRuleFlag::kTwoEdges;
      }
    }
    result.end_of_line_spacing_rules.push_back(rule);
  }

  result.min_enclose_area_rules.reserve(source.min_enclose_area_rules.size());
  for (const auto& source_rule : source.min_enclose_area_rules) {
    TechRoutingMinEncloseAreaRule rule{.area = toDatabaseArea(source_rule.area, database_units_per_micron, "ROUTING MINENCLOSEDAREA")};
    if (source_rule.width) {
      rule.width = toDatabaseUnits(*source_rule.width, database_units_per_micron, "ROUTING MINENCLOSEDAREA WIDTH");
    }
    result.min_enclose_area_rules.push_back(rule);
  }

  result.min_step_rules.reserve(source.min_step_rules.size());
  for (const auto& source_rule : source.min_step_rules) {
    TechRoutingMinStepRule rule{
        .min_step_length = toDatabaseUnits(source_rule.min_step_length, database_units_per_micron, "ROUTING MINSTEP"),
        .type = source_rule.type};
    if (source_rule.max_length_sum) {
      rule.flags |= TechRoutingMinStepRuleFlag::kHasMaxLengthSum;
      rule.max_length_sum = toDatabaseUnits(*source_rule.max_length_sum, database_units_per_micron, "ROUTING MINSTEP LENGTHSUM");
    }
    if (source_rule.max_edges) {
      rule.flags |= TechRoutingMinStepRuleFlag::kHasMaxEdges;
      rule.max_edges = *source_rule.max_edges;
    }
    result.min_step_rules.push_back(rule);
  }

  result.minimum_cut_rules.reserve(source.minimum_cut_rules.size());
  for (const auto& source_rule : source.minimum_cut_rules) {
    TechRoutingMinimumCutRule rule{.num_cuts = source_rule.num_cuts,
                                   .width = toDatabaseUnits(source_rule.width, database_units_per_micron, "ROUTING MINIMUMCUT WIDTH"),
                                   .orient = source_rule.orient};
    if (source_rule.within_cut_distance) {
      rule.flags |= TechRoutingMinimumCutRuleFlag::kHasWithinCutDistance;
      rule.within_cut_distance = toDatabaseUnits(*source_rule.within_cut_distance, database_units_per_micron, "ROUTING MINIMUMCUT WITHIN");
    }
    if (source_rule.length) {
      rule.flags |= TechRoutingMinimumCutRuleFlag::kHasLength;
      rule.length = toDatabaseUnits(source_rule.length->first, database_units_per_micron, "ROUTING MINIMUMCUT LENGTH");
      rule.length_distance = toDatabaseUnits(source_rule.length->second, database_units_per_micron, "ROUTING MINIMUMCUT LENGTH WITHIN");
    }
    result.minimum_cut_rules.push_back(rule);
  }

  result.prl_spacing_tables.reserve(source.prl_spacing_tables.size());
  for (const auto& source_table : source.prl_spacing_tables) {
    TechRoutingPrlSpacingTableRule table;
    table.widths.reserve(source_table.widths.size());
    for (const auto value : source_table.widths) {
      table.widths.push_back(toDatabaseUnits(value, database_units_per_micron, "ROUTING PRL WIDTH"));
    }
    table.parallel_run_lengths.reserve(source_table.parallel_run_lengths.size());
    for (const auto value : source_table.parallel_run_lengths) {
      table.parallel_run_lengths.push_back(toDatabaseUnits(value, database_units_per_micron, "ROUTING PRL length"));
    }
    table.cells.reserve(source_table.cells.size());
    for (const auto value : source_table.cells) {
      table.cells.push_back(toDatabaseUnits(value, database_units_per_micron, "ROUTING PRL spacing"));
    }
    result.prl_spacing_tables.push_back(std::move(table));
  }

  result.influence_spacing_tables.reserve(source.influence_spacing_tables.size());
  for (const auto& source_table : source.influence_spacing_tables) {
    TechRoutingInfluenceSpacingTableRule table;
    table.entries.reserve(source_table.entries.size());
    for (const auto& entry : source_table.entries) {
      table.entries.push_back(TechRoutingInfluenceSpacingTableEntry{
          .width = toDatabaseUnits(entry[0], database_units_per_micron, "ROUTING INFLUENCE WIDTH"),
          .within = toDatabaseUnits(entry[1], database_units_per_micron, "ROUTING INFLUENCE WITHIN"),
          .spacing = toDatabaseUnits(entry[2], database_units_per_micron, "ROUTING INFLUENCE SPACING")});
    }
    result.influence_spacing_tables.push_back(std::move(table));
  }

  result.two_widths_spacing_tables.reserve(source.two_widths_spacing_tables.size());
  for (const auto& source_table : source.two_widths_spacing_tables) {
    TechRoutingTwoWidthsSpacingTableRule table;
    table.widths.reserve(source_table.widths.size());
    for (const auto& source_axis : source_table.widths) {
      TechRoutingTwoWidthsSpacingTableWidth axis{
          .width = toDatabaseUnits(source_axis.width, database_units_per_micron, "ROUTING TWOWIDTHS WIDTH")};
      if (source_axis.prl) {
        axis.has_prl = true;
        axis.prl = toDatabaseUnits(*source_axis.prl, database_units_per_micron, "ROUTING TWOWIDTHS PRL");
      }
      table.widths.push_back(axis);
      for (const auto spacing : source_axis.spacings) {
        table.cells.push_back(toDatabaseUnits(spacing, database_units_per_micron, "ROUTING TWOWIDTHS spacing"));
      }
    }
    result.two_widths_spacing_tables.push_back(std::move(table));
  }

  result.current_density_rules.reserve(source.current_density_rules.size());
  for (const auto& source_rule : source.current_density_rules) {
    TechRoutingCurrentDensityRule rule{.signal = source_rule.signal,
                                       .type = source_rule.type,
                                       .scalar = source_rule.scalar,
                                       .frequencies = source_rule.frequencies,
                                       .table_entries = source_rule.table_entries};
    if (source_rule.has_scalar) {
      rule.flags |= TechRoutingCurrentDensityRuleFlag::kHasScalar;
    }
    rule.widths.reserve(source_rule.widths.size());
    for (const auto width : source_rule.widths) {
      rule.widths.push_back(toDatabaseUnits(width, database_units_per_micron, "ROUTING CURRENTDENSITY WIDTH"));
    }
    result.current_density_rules.push_back(std::move(rule));
  }
  return result;
}

PreparedCutRules prepareCutRules(const StagedCutRules& source, int32_t database_units_per_micron)
{
  PreparedCutRules result;
  result.spacing_rules.reserve(source.spacing_rules.size());
  for (const auto& source_rule : source.spacing_rules) {
    TechCutSpacingRule rule{.spacing = toDatabaseUnits(source_rule.spacing, database_units_per_micron, "CUT SPACING")};
    if (source_rule.same_net) {
      rule.flags |= TechCutSpacingRuleFlag::kSameNet;
    }
    if (source_rule.center_to_center) {
      rule.flags |= TechCutSpacingRuleFlag::kCenterToCenter;
    }
    if (source_rule.same_net_pg_only) {
      rule.flags |= TechCutSpacingRuleFlag::kSameNetPgOnly;
    }
    if (source_rule.second_layer_name) {
      rule.flags |= TechCutSpacingRuleFlag::kHasSecondLayer;
      rule.second_layer_name = *source_rule.second_layer_name;
    }
    if (source_rule.stack) {
      rule.flags |= TechCutSpacingRuleFlag::kStack;
    }
    if (source_rule.except_same_pg_net) {
      rule.flags |= TechCutSpacingRuleFlag::kExceptSamePgNet;
    }
    if (source_rule.parallel_overlap) {
      rule.flags |= TechCutSpacingRuleFlag::kParallelOverlap;
    }
    if (source_rule.cut_area) {
      rule.flags |= TechCutSpacingRuleFlag::kHasCutArea;
      rule.cut_area = toDatabaseArea(*source_rule.cut_area, database_units_per_micron, "CUT SPACING AREA");
    }
    if (source_rule.adjacent_cuts) {
      rule.flags |= TechCutSpacingRuleFlag::kHasAdjacentCuts;
      rule.adjacent_cut_count = source_rule.adjacent_cuts->first;
      rule.adjacent_cut_within
          = toDatabaseUnits(source_rule.adjacent_cuts->second, database_units_per_micron, "CUT SPACING ADJACENTCUTS WITHIN");
    }
    result.spacing_rules.push_back(rule);
  }

  result.enclosure_rules.reserve(source.enclosure_rules.size());
  for (const auto& source_rule : source.enclosure_rules) {
    TechCutEnclosureRule rule{
        .side = source_rule.side,
        .overhang1 = toDatabaseUnits(source_rule.overhang1, database_units_per_micron, "CUT ENCLOSURE overhang1"),
        .overhang2 = toDatabaseUnits(source_rule.overhang2, database_units_per_micron, "CUT ENCLOSURE overhang2")};
    if (source_rule.min_width) {
      rule.flags |= TechCutEnclosureRuleFlag::kHasMinWidth;
      rule.min_width = toDatabaseUnits(*source_rule.min_width, database_units_per_micron, "CUT ENCLOSURE WIDTH");
    }
    if (source_rule.except_extra_cut) {
      rule.flags |= TechCutEnclosureRuleFlag::kExceptExtraCut | TechCutEnclosureRuleFlag::kHasCutWithin;
      rule.cut_within
          = toDatabaseUnits(*source_rule.except_extra_cut, database_units_per_micron, "CUT ENCLOSURE EXCEPTEXTRACUT");
    }
    if (source_rule.min_length) {
      rule.flags |= TechCutEnclosureRuleFlag::kHasMinLength;
      rule.min_length = toDatabaseUnits(*source_rule.min_length, database_units_per_micron, "CUT ENCLOSURE LENGTH");
    }
    result.enclosure_rules.push_back(rule);
  }

  if (source.array_spacing_rule) {
    TechCutArraySpacingRule rule{.cut_spacing
                                 = toDatabaseUnits(source.array_spacing_rule->cut_spacing, database_units_per_micron, "CUT ARRAYSPACING")};
    if (source.array_spacing_rule->long_array) {
      rule.flags |= TechCutArraySpacingRuleFlag::kLongArray;
    }
    if (source.array_spacing_rule->via_width) {
      rule.flags |= TechCutArraySpacingRuleFlag::kHasViaWidth;
      rule.via_width = toDatabaseUnits(*source.array_spacing_rule->via_width, database_units_per_micron, "CUT ARRAYSPACING WIDTH");
    }
    rule.items.reserve(source.array_spacing_rule->items.size());
    for (const auto& source_item : source.array_spacing_rule->items) {
      rule.items.push_back(TechCutArraySpacingItem{
          .array_cut_count = source_item.array_cut_count,
          .spacing = toDatabaseUnits(source_item.spacing, database_units_per_micron, "CUT ARRAYSPACING ARRAYCUTS SPACING")});
    }
    result.array_spacing_rule = std::move(rule);
  }

  if (!source.orthogonal_spacing_table.empty()) {
    TechCutOrthogonalSpacingTableRule rule;
    rule.items.reserve(source.orthogonal_spacing_table.size());
    for (const auto& item : source.orthogonal_spacing_table) {
      rule.items.push_back(TechCutOrthogonalSpacingTableItem{
          .within = toDatabaseUnits(item.within, database_units_per_micron, "CUT SPACINGTABLE ORTHOGONAL WITHIN"),
          .spacing = toDatabaseUnits(item.spacing, database_units_per_micron, "CUT SPACINGTABLE ORTHOGONAL SPACING")});
    }
    result.orthogonal_spacing_table_rule = std::move(rule);
  }

  result.current_density_rules.reserve(source.current_density_rules.size());
  for (const auto& source_rule : source.current_density_rules) {
    TechCutCurrentDensityRule rule{.signal = source_rule.signal,
                                   .type = source_rule.type,
                                   .scalar = source_rule.scalar,
                                   .frequencies = source_rule.frequencies,
                                   .table_entries = source_rule.table_entries};
    if (source_rule.has_scalar) {
      rule.flags |= TechCutCurrentDensityRuleFlag::kHasScalar;
    }
    rule.cut_areas.reserve(source_rule.cut_areas.size());
    for (const auto area : source_rule.cut_areas) {
      rule.cut_areas.push_back(toDatabaseArea(area, database_units_per_micron, "CUT CURRENTDENSITY CUTAREA"));
    }
    result.current_density_rules.push_back(std::move(rule));
  }
  return result;
}

void commitRoutingRules(TechRoutingLayerStorage& storage, TechRoutingLayerId owner, const PreparedRoutingRules& rules)
{
  for (const auto& rule : rules.spacing_rules) {
    static_cast<void>(storage.addSpacingRule(owner, rule));
  }
  for (const auto& rule : rules.end_of_line_spacing_rules) {
    static_cast<void>(storage.addEndOfLineSpacingRule(owner, rule));
  }
  for (const auto& rule : rules.spacing_notch_length_rules) {
    static_cast<void>(storage.addSpacingNotchLengthRule(owner, rule));
  }
  for (const auto& rule : rules.min_enclose_area_rules) {
    static_cast<void>(storage.addMinEncloseAreaRule(owner, rule));
  }
  for (const auto& rule : rules.min_step_rules) {
    static_cast<void>(storage.addMinStepRule(owner, rule));
  }
  for (const auto& rule : rules.minimum_cut_rules) {
    static_cast<void>(storage.addMinimumCutRule(owner, rule));
  }
  for (const auto& rule : rules.prl_spacing_tables) {
    static_cast<void>(storage.addPrlSpacingTableRule(owner, rule));
  }
  for (const auto& rule : rules.influence_spacing_tables) {
    static_cast<void>(storage.addInfluenceSpacingTableRule(owner, rule));
  }
  for (const auto& rule : rules.two_widths_spacing_tables) {
    static_cast<void>(storage.addTwoWidthsSpacingTableRule(owner, rule));
  }
  for (const auto& rule : rules.current_density_rules) {
    static_cast<void>(storage.addCurrentDensityRule(owner, rule));
  }
}

void commitCutRules(TechCutLayerStorage& storage, TechCutLayerId owner, const PreparedCutRules& rules)
{
  for (const auto& rule : rules.spacing_rules) {
    static_cast<void>(storage.addSpacingRule(owner, rule));
  }
  for (const auto& rule : rules.enclosure_rules) {
    static_cast<void>(storage.addEnclosureRule(owner, rule));
  }
  if (rules.array_spacing_rule) {
    static_cast<void>(storage.setArraySpacingRule(owner, *rules.array_spacing_rule));
  }
  if (rules.orthogonal_spacing_table_rule) {
    static_cast<void>(storage.addOrthogonalSpacingTableRule(owner, *rules.orthogonal_spacing_table_rule));
  }
  for (const auto& rule : rules.current_density_rules) {
    static_cast<void>(storage.addCurrentDensityRule(owner, rule));
  }
}

}  // namespace eccdb::lef_detail
