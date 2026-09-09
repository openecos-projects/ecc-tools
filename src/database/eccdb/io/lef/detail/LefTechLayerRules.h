#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "tech/cut_layer/model/CutRuleComponents.h"
#include "tech/routing_layer/model/RoutingRuleComponents.h"

namespace LefDefParser {
class lefiLayer;
}

namespace eccdb {

class TechCutLayerStorage;
class TechRoutingLayerStorage;

namespace lef_detail {

struct StagedRoutingSpacingRule
{
  double min_spacing = 0.0;
  std::optional<std::pair<double, double>> range;
  std::optional<double> notch_length;
};

struct StagedRoutingEndOfLineSpacingRule
{
  double min_spacing = 0.0;
  double eol_width = 0.0;
  double eol_within = 0.0;
  bool has_parallel_edge = false;
  double parallel_space = 0.0;
  double parallel_within = 0.0;
  bool two_edges = false;
};

struct StagedRoutingMinEncloseAreaRule
{
  double area = 0.0;
  std::optional<double> width;
};

struct StagedRoutingMinStepRule
{
  double min_step_length = 0.0;
  TechRoutingMinStepType type = TechRoutingMinStepType::kNone;
  std::optional<double> max_length_sum;
  std::optional<int32_t> max_edges;
};

struct StagedRoutingMinimumCutRule
{
  int32_t num_cuts = 0;
  double width = 0.0;
  std::optional<double> within_cut_distance;
  TechRoutingMinimumCutOrient orient = TechRoutingMinimumCutOrient::kNone;
  std::optional<std::pair<double, double>> length;
};

struct StagedRoutingPrlSpacingTable
{
  std::vector<double> widths;
  std::vector<double> parallel_run_lengths;
  std::vector<double> cells;
};

struct StagedRoutingInfluenceSpacingTable
{
  std::vector<std::array<double, 3>> entries;
};

struct StagedRoutingTwoWidthsAxis
{
  double width = 0.0;
  std::optional<double> prl;
  std::vector<double> spacings;
};

struct StagedRoutingTwoWidthsSpacingTable
{
  std::vector<StagedRoutingTwoWidthsAxis> widths;
};

struct StagedRoutingCurrentDensity
{
  TechRoutingCurrentDensitySignal signal = TechRoutingCurrentDensitySignal::kAc;
  TechRoutingCurrentDensityType type = TechRoutingCurrentDensityType::kUnknown;
  bool has_scalar = false;
  double scalar = 0.0;
  std::vector<double> frequencies;
  std::vector<double> widths;
  std::vector<double> table_entries;
};

struct StagedRoutingRules
{
  std::vector<StagedRoutingSpacingRule> spacing_rules;
  std::vector<StagedRoutingEndOfLineSpacingRule> end_of_line_spacing_rules;
  std::vector<StagedRoutingMinEncloseAreaRule> min_enclose_area_rules;
  std::vector<StagedRoutingMinStepRule> min_step_rules;
  std::vector<StagedRoutingMinimumCutRule> minimum_cut_rules;
  std::vector<StagedRoutingPrlSpacingTable> prl_spacing_tables;
  std::vector<StagedRoutingInfluenceSpacingTable> influence_spacing_tables;
  std::vector<StagedRoutingTwoWidthsSpacingTable> two_widths_spacing_tables;
  std::vector<StagedRoutingCurrentDensity> current_density_rules;
};

struct StagedCutSpacingRule
{
  double spacing = 0.0;
  bool same_net = false;
  bool center_to_center = false;
  bool same_net_pg_only = false;
  bool stack = false;
  bool except_same_pg_net = false;
  bool parallel_overlap = false;
  std::optional<std::string> second_layer_name;
  std::optional<double> cut_area;
  std::optional<std::pair<uint32_t, double>> adjacent_cuts;
};

struct StagedCutEnclosureRule
{
  CutLayerSide side = CutLayerSide::kUnknown;
  double overhang1 = 0.0;
  double overhang2 = 0.0;
  std::optional<double> min_width;
  std::optional<double> except_extra_cut;
  std::optional<double> min_length;
};

struct StagedCutArraySpacingItem
{
  uint32_t array_cut_count = 0;
  double spacing = 0.0;
};

struct StagedCutArraySpacingRule
{
  bool long_array = false;
  std::optional<double> via_width;
  double cut_spacing = 0.0;
  std::vector<StagedCutArraySpacingItem> items;
};

struct StagedCutOrthogonalSpacingTableItem
{
  double within = 0.0;
  double spacing = 0.0;
};

struct StagedCutCurrentDensity
{
  TechCutCurrentDensitySignal signal = TechCutCurrentDensitySignal::kAc;
  TechCutCurrentDensityType type = TechCutCurrentDensityType::kUnknown;
  bool has_scalar = false;
  double scalar = 0.0;
  std::vector<double> frequencies;
  std::vector<double> cut_areas;
  std::vector<double> table_entries;
};

struct StagedCutRules
{
  std::vector<StagedCutSpacingRule> spacing_rules;
  std::vector<StagedCutEnclosureRule> enclosure_rules;
  std::optional<StagedCutArraySpacingRule> array_spacing_rule;
  std::vector<StagedCutOrthogonalSpacingTableItem> orthogonal_spacing_table;
  std::vector<StagedCutCurrentDensity> current_density_rules;
};

struct PreparedRoutingRules
{
  std::vector<TechRoutingSpacingRule> spacing_rules;
  std::vector<TechRoutingEndOfLineSpacingRule> end_of_line_spacing_rules;
  std::vector<TechRoutingSpacingNotchLengthRule> spacing_notch_length_rules;
  std::vector<TechRoutingMinEncloseAreaRule> min_enclose_area_rules;
  std::vector<TechRoutingMinStepRule> min_step_rules;
  std::vector<TechRoutingMinimumCutRule> minimum_cut_rules;
  std::vector<TechRoutingPrlSpacingTableRule> prl_spacing_tables;
  std::vector<TechRoutingInfluenceSpacingTableRule> influence_spacing_tables;
  std::vector<TechRoutingTwoWidthsSpacingTableRule> two_widths_spacing_tables;
  std::vector<TechRoutingCurrentDensityRule> current_density_rules;
};

struct PreparedCutRules
{
  std::vector<TechCutSpacingRule> spacing_rules;
  std::vector<TechCutEnclosureRule> enclosure_rules;
  std::optional<TechCutArraySpacingRule> array_spacing_rule;
  std::optional<TechCutOrthogonalSpacingTableRule> orthogonal_spacing_table_rule;
  std::vector<TechCutCurrentDensityRule> current_density_rules;
};

[[nodiscard]] StagedRoutingRules stageRoutingRules(::LefDefParser::lefiLayer& source);
[[nodiscard]] StagedCutRules stageCutRules(::LefDefParser::lefiLayer& source);

[[nodiscard]] PreparedRoutingRules prepareRoutingRules(const StagedRoutingRules& source, int32_t database_units_per_micron);
[[nodiscard]] PreparedCutRules prepareCutRules(const StagedCutRules& source, int32_t database_units_per_micron);

void commitRoutingRules(TechRoutingLayerStorage& storage, TechRoutingLayerId owner, const PreparedRoutingRules& rules);
void commitCutRules(TechCutLayerStorage& storage, TechCutLayerId owner, const PreparedCutRules& rules);

}  // namespace lef_detail
}  // namespace eccdb
