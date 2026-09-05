#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "tech/common/TechLayerTypes.h"
#include "tech/cut_layer/model/CutRuleComponents.h"
#include "tech/routing_layer/model/RoutingRuleComponents.h"

namespace eccdb {

class TechStore;
class TechCutLayerStorage;
class TechRoutingLayerStorage;

namespace lef_detail {

struct PreparedRoutingLef58Properties
{
  uint64_t layer_flags = 0;
  std::vector<TechRoutingLef58AreaRule> area_rules;
  std::vector<TechRoutingLef58CornerFillSpacingRule> corner_fill_spacing_rules;
  std::vector<TechRoutingLef58CornerSpacingRule> corner_spacing_rules;
  std::vector<TechRoutingLef58MinimumCutRule> minimum_cut_rules;
  std::vector<TechRoutingLef58MinStepRule> min_step_rules;
  std::vector<TechRoutingLef58WidthTableRule> width_table_rules;
  std::vector<TechRoutingLef58SpacingEolRule> spacing_eol_rules;
  std::vector<TechRoutingLef58SpacingNotchLengthRule> spacing_notch_length_rules;
  std::vector<TechRoutingLef58SpacingTableJogToJogRule> jog_to_jog_rules;
  std::vector<TechRoutingPrlSpacingTableRule> prl_spacing_tables;
};

struct PreparedCutLef58Properties
{
  std::vector<TechCutLef58CutClassRule> cut_class_rules;
  std::vector<TechCutLef58EnclosureRule> enclosure_rules;
  std::vector<TechCutLef58EnclosureEdgeRule> enclosure_edge_rules;
  std::optional<TechCutLef58EolEnclosureRule> eol_enclosure_rule;
  std::optional<TechCutLef58EolSpacingRule> eol_spacing_rule;
  std::vector<TechCutLef58SpacingTableRule> spacing_table_rules;
  std::vector<TechCutOrthogonalSpacingTableRule> orthogonal_spacing_table_rules;
};

struct PreparedTrimmedMetalRule
{
  std::string routing_layer;
  uint32_t flags = 0;
  uint32_t mask = 0;
};

[[nodiscard]] PreparedRoutingLef58Properties prepareRoutingLef58Properties(const std::vector<TechProperty>& properties,
                                                                           int32_t database_units_per_micron);
[[nodiscard]] PreparedCutLef58Properties prepareCutLef58Properties(const std::vector<TechProperty>& properties,
                                                                   int32_t database_units_per_micron);
[[nodiscard]] std::optional<PreparedTrimmedMetalRule> prepareMastersliceLef58Properties(
    const std::vector<TechProperty>& properties);

void commitRoutingLef58Properties(TechRoutingLayerStorage& storage, TechRoutingLayerId owner,
                                  const PreparedRoutingLef58Properties& properties);
void commitCutLef58Properties(TechCutLayerStorage& storage, TechCutLayerId owner, const PreparedCutLef58Properties& properties);
void commitTrimmedMetalRule(TechStore& database, TechMastersliceLayerId owner, const PreparedTrimmedMetalRule& rule);

}  // namespace lef_detail
}  // namespace eccdb
