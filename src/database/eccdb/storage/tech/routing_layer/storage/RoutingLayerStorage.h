#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "tech/TechRegistry.h"
#include "tech/common/TechRuleRelations.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/routing_layer/model/RoutingRuleComponents.h"
#include "tech/routing_layer/model/RoutingRuleIds.h"

namespace eccdb {

// ROUTING facade over the shared Tech registry. A complete Rule is one EnTT
// entity; its variable-size attributes are ordinary vectors in that Rule
// component. Rule-internal rows therefore have no independent database ID.
class TechRoutingLayerStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  explicit TechRoutingLayerStorage(TechRegistry& registry) : _registry(registry.registry()) {}

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }

  [[nodiscard]] TechRoutingLayerId createLayer(TechLayerInfo info, TechRoutingLayer routing);
  [[nodiscard]] bool contains(TechRoutingLayerId id) const;
  [[nodiscard]] TechRoutingLayerId findLayerById(uint32_t id) const;
  [[nodiscard]] TechLayerInfo& layerInfo(TechRoutingLayerId id);
  [[nodiscard]] const TechLayerInfo& layerInfo(TechRoutingLayerId id) const;
  [[nodiscard]] TechRoutingLayer& routingLayer(TechRoutingLayerId id);
  [[nodiscard]] const TechRoutingLayer& routingLayer(TechRoutingLayerId id) const;
  [[nodiscard]] uint32_t ruleCount(TechRoutingLayerId id) const;

  // Native LEF rules with only fixed-size component fields.
  [[nodiscard]] TechRoutingSpacingRuleId addSpacingRule(TechRoutingLayerId owner, TechRoutingSpacingRule rule);
  [[nodiscard]] std::vector<TechRoutingSpacingRuleId> spacingRules(TechRoutingLayerId owner) const;
  [[nodiscard]] TechRoutingEndOfLineSpacingRuleId addEndOfLineSpacingRule(TechRoutingLayerId owner,
                                                                          TechRoutingEndOfLineSpacingRule rule);
  [[nodiscard]] std::vector<TechRoutingEndOfLineSpacingRuleId> endOfLineSpacingRules(TechRoutingLayerId owner) const;
  [[nodiscard]] TechRoutingMinEncloseAreaRuleId addMinEncloseAreaRule(TechRoutingLayerId owner, TechRoutingMinEncloseAreaRule rule);
  [[nodiscard]] std::vector<TechRoutingMinEncloseAreaRuleId> minEncloseAreaRules(TechRoutingLayerId owner) const;
  [[nodiscard]] TechRoutingMinStepRuleId addMinStepRule(TechRoutingLayerId owner, TechRoutingMinStepRule rule);
  [[nodiscard]] std::vector<TechRoutingMinStepRuleId> minStepRules(TechRoutingLayerId owner) const;
  [[nodiscard]] TechRoutingMinimumCutRuleId addMinimumCutRule(TechRoutingLayerId owner, TechRoutingMinimumCutRule rule);
  [[nodiscard]] std::vector<TechRoutingMinimumCutRuleId> minimumCutRules(TechRoutingLayerId owner) const;
  [[nodiscard]] TechRoutingSpacingNotchLengthRuleId addSpacingNotchLengthRule(TechRoutingLayerId owner,
                                                                              TechRoutingSpacingNotchLengthRule rule);
  [[nodiscard]] std::vector<TechRoutingSpacingNotchLengthRuleId> spacingNotchLengthRules(TechRoutingLayerId owner) const;

  // LEF SPACINGTABLE PARALLELRUNLENGTH. The cells are stored width-major.
  [[nodiscard]] TechRoutingPrlSpacingTableRuleId addPrlSpacingTableRule(TechRoutingLayerId owner, TechRoutingPrlSpacingTableRule rule);
  [[nodiscard]] std::vector<TechRoutingPrlSpacingTableRuleId> prlSpacingTableRules(TechRoutingLayerId owner) const;
  [[nodiscard]] std::span<const int32_t> prlSpacingTableWidths(TechRoutingPrlSpacingTableRuleId rule) const;
  [[nodiscard]] std::span<const int32_t> prlSpacingTableParallelRunLengths(TechRoutingPrlSpacingTableRuleId rule) const;
  [[nodiscard]] std::span<const int32_t> prlSpacingTableCells(TechRoutingPrlSpacingTableRuleId rule) const;
  [[nodiscard]] std::span<const TechRoutingPrlSpacingTableExceptWithin> prlSpacingTableExceptWithins(
      TechRoutingPrlSpacingTableRuleId rule) const;
  [[nodiscard]] std::span<const TechRoutingPrlSpacingTableInfluence> prlSpacingTableInfluences(TechRoutingPrlSpacingTableRuleId rule) const;
  [[nodiscard]] int32_t prlSpacingTableCell(TechRoutingPrlSpacingTableRuleId rule, uint32_t width_index,
                                            uint32_t parallel_run_length_index) const;
  [[nodiscard]] int32_t prlSpacingFor(TechRoutingPrlSpacingTableRuleId rule, int32_t width_a, int32_t width_b,
                                      int32_t parallel_run_length) const;
  [[nodiscard]] std::optional<int32_t> prlInfluenceSpacingFor(TechRoutingPrlSpacingTableRuleId rule, int32_t width, int32_t distance) const;

  [[nodiscard]] TechRoutingInfluenceSpacingTableRuleId addInfluenceSpacingTableRule(TechRoutingLayerId owner,
                                                                                    TechRoutingInfluenceSpacingTableRule rule);
  [[nodiscard]] std::vector<TechRoutingInfluenceSpacingTableRuleId> influenceSpacingTableRules(TechRoutingLayerId owner) const;
  [[nodiscard]] std::span<const TechRoutingInfluenceSpacingTableEntry> influenceSpacingTableEntries(
      TechRoutingInfluenceSpacingTableRuleId rule) const;
  [[nodiscard]] std::optional<int32_t> influenceSpacingFor(TechRoutingInfluenceSpacingTableRuleId rule, int32_t width,
                                                           int32_t distance) const;

  // LEF SPACINGTABLE TWOWIDTHS. Cells form an N x N row-major matrix.
  [[nodiscard]] TechRoutingTwoWidthsSpacingTableRuleId addTwoWidthsSpacingTableRule(TechRoutingLayerId owner,
                                                                                    TechRoutingTwoWidthsSpacingTableRule rule);
  [[nodiscard]] std::vector<TechRoutingTwoWidthsSpacingTableRuleId> twoWidthsSpacingTableRules(TechRoutingLayerId owner) const;
  [[nodiscard]] std::span<const TechRoutingTwoWidthsSpacingTableWidth> twoWidthsSpacingTableWidths(
      TechRoutingTwoWidthsSpacingTableRuleId rule) const;
  [[nodiscard]] std::span<const int32_t> twoWidthsSpacingTableCells(TechRoutingTwoWidthsSpacingTableRuleId rule) const;
  [[nodiscard]] int32_t twoWidthsSpacingTableCell(TechRoutingTwoWidthsSpacingTableRuleId rule, uint32_t row_index,
                                                  uint32_t column_index) const;
  [[nodiscard]] int32_t twoWidthsSpacingFor(TechRoutingTwoWidthsSpacingTableRuleId rule, int32_t width_a, int32_t width_b,
                                            int32_t parallel_run_length) const;

  // AC/DC current-density tables are frequency-major, width-minor matrices.
  [[nodiscard]] TechRoutingCurrentDensityRuleId addCurrentDensityRule(TechRoutingLayerId owner, TechRoutingCurrentDensityRule rule);
  [[nodiscard]] std::vector<TechRoutingCurrentDensityRuleId> currentDensityRules(TechRoutingLayerId owner) const;
  [[nodiscard]] std::span<const double> currentDensityFrequencies(TechRoutingCurrentDensityRuleId rule) const;
  [[nodiscard]] std::span<const int32_t> currentDensityWidths(TechRoutingCurrentDensityRuleId rule) const;
  [[nodiscard]] std::span<const double> currentDensityTableEntries(TechRoutingCurrentDensityRuleId rule) const;
  [[nodiscard]] double currentDensityTableEntry(TechRoutingCurrentDensityRuleId rule, uint32_t frequency_index, uint32_t width_index) const;
  [[nodiscard]] double currentDensityAt(TechRoutingCurrentDensityRuleId rule, double frequency, int32_t width) const;

  // LEF58 rules with variable-size child rows.
  [[nodiscard]] TechRoutingLef58AreaRuleId addLef58AreaRule(TechRoutingLayerId owner, TechRoutingLef58AreaRule rule);
  [[nodiscard]] std::vector<TechRoutingLef58AreaRuleId> lef58AreaRules(TechRoutingLayerId owner) const;
  [[nodiscard]] std::span<const TechRoutingLef58AreaExceptMinSize> lef58AreaExceptMinSizes(TechRoutingLef58AreaRuleId rule) const;
  [[nodiscard]] TechRoutingLef58CornerFillSpacingRuleId addLef58CornerFillSpacingRule(TechRoutingLayerId owner,
                                                                                      TechRoutingLef58CornerFillSpacingRule rule);
  [[nodiscard]] std::vector<TechRoutingLef58CornerFillSpacingRuleId> lef58CornerFillSpacingRules(TechRoutingLayerId owner) const;
  [[nodiscard]] TechRoutingLef58CornerSpacingRuleId addLef58CornerSpacingRule(TechRoutingLayerId owner,
                                                                              TechRoutingLef58CornerSpacingRule rule);
  [[nodiscard]] std::vector<TechRoutingLef58CornerSpacingRuleId> lef58CornerSpacingRules(TechRoutingLayerId owner) const;
  [[nodiscard]] std::span<const TechRoutingLef58CornerSpacingWidth> lef58CornerSpacingWidths(
      TechRoutingLef58CornerSpacingRuleId rule) const;
  [[nodiscard]] TechRoutingLef58MinimumCutRuleId addLef58MinimumCutRule(TechRoutingLayerId owner, TechRoutingLef58MinimumCutRule rule);
  [[nodiscard]] std::vector<TechRoutingLef58MinimumCutRuleId> lef58MinimumCutRules(TechRoutingLayerId owner) const;
  [[nodiscard]] std::span<const TechRoutingLef58MinimumCutClass> lef58MinimumCutClasses(TechRoutingLef58MinimumCutRuleId rule) const;
  [[nodiscard]] TechRoutingLef58MinStepRuleId addLef58MinStepRule(TechRoutingLayerId owner, TechRoutingLef58MinStepRule rule);
  [[nodiscard]] std::vector<TechRoutingLef58MinStepRuleId> lef58MinStepRules(TechRoutingLayerId owner) const;
  [[nodiscard]] TechRoutingLef58WidthTableRuleId addLef58WidthTableRule(TechRoutingLayerId owner, TechRoutingLef58WidthTableRule rule);
  [[nodiscard]] std::vector<TechRoutingLef58WidthTableRuleId> lef58WidthTableRules(TechRoutingLayerId owner) const;
  [[nodiscard]] std::span<const int32_t> lef58WidthTableWidths(TechRoutingLef58WidthTableRuleId rule) const;
  [[nodiscard]] TechRoutingLef58SpacingEolRuleId addLef58SpacingEolRule(TechRoutingLayerId owner, TechRoutingLef58SpacingEolRule rule);
  [[nodiscard]] std::vector<TechRoutingLef58SpacingEolRuleId> lef58SpacingEolRules(TechRoutingLayerId owner) const;
  [[nodiscard]] TechRoutingLef58SpacingNotchLengthRuleId addLef58SpacingNotchLengthRule(TechRoutingLayerId owner,
                                                                                        TechRoutingLef58SpacingNotchLengthRule rule);
  [[nodiscard]] std::vector<TechRoutingLef58SpacingNotchLengthRuleId> lef58SpacingNotchLengthRules(TechRoutingLayerId owner) const;
  [[nodiscard]] TechRoutingLef58SpacingTableJogToJogRuleId addLef58SpacingTableJogToJogRule(TechRoutingLayerId owner,
                                                                                            TechRoutingLef58SpacingTableJogToJogRule rule);
  [[nodiscard]] std::vector<TechRoutingLef58SpacingTableJogToJogRuleId> lef58SpacingTableJogToJogRules(TechRoutingLayerId owner) const;
  [[nodiscard]] std::span<const TechRoutingLef58JogToJogWidth> lef58JogToJogWidths(TechRoutingLef58SpacingTableJogToJogRuleId rule) const;

  // Typed ID access is intentionally the only generic part of the public API.
  // Creation and traversal remain named by the closed ROUTING rule schema.
  template <typename Rule>
  [[nodiscard]] bool hasRule(EnttId<TechEntity, Rule> id) const
  {
    return _registry.valid(id.entity()) && _registry.all_of<TechRuleOwner, Rule>(id.entity());
  }

  template <typename Rule>
  [[nodiscard]] EnttId<TechEntity, Rule> findRuleById(uint32_t id) const
  {
    const auto entity = static_cast<TechEntity>(id);
    return _registry.valid(entity) && _registry.all_of<TechRuleOwner, Rule>(entity) ? EnttId<TechEntity, Rule>{entity}
                                                                                    : EnttId<TechEntity, Rule>{};
  }

  template <typename Rule>
  [[nodiscard]] Rule& rule(EnttId<TechEntity, Rule> id)
  {
    ensureRule(id);
    return _registry.get<Rule>(id.entity());
  }

  template <typename Rule>
  [[nodiscard]] const Rule& rule(EnttId<TechEntity, Rule> id) const
  {
    ensureRule(id);
    return _registry.get<Rule>(id.entity());
  }

  template <typename Rule>
  [[nodiscard]] TechRoutingLayerId ruleOwner(EnttId<TechEntity, Rule> id) const
  {
    ensureRule(id);
    const auto owner = TechRoutingLayerId{_registry.get<TechRuleOwner>(id.entity()).owner};
    ensureLayer(owner);
    return owner;
  }

 private:
  static void validateLayerInfo(const TechLayerInfo& info);

  template <typename Rule>
  [[nodiscard]] EnttId<TechEntity, Rule> createRepeatedRule(TechRoutingLayerId owner, Rule rule)
  {
    const auto entity = createOwnedRule(owner);
    try {
      _registry.emplace<Rule>(entity, std::move(rule));
      auto& refs = _registry.get_or_emplace<TechRuleRefs<EnttId<TechEntity, Rule>>>(owner.entity());
      refs.values.emplace_back(entity);
    } catch (...) {
      if (_registry.valid(entity)) {
        _registry.destroy(entity);
      }
      throw;
    }
    return EnttId<TechEntity, Rule>{entity};
  }

  template <typename Rule>
  [[nodiscard]] std::vector<EnttId<TechEntity, Rule>> repeatedRules(TechRoutingLayerId owner) const
  {
    ensureLayer(owner);
    const auto* refs = _registry.try_get<TechRuleRefs<EnttId<TechEntity, Rule>>>(owner.entity());
    if (refs == nullptr) {
      return {};
    }

    std::vector<EnttId<TechEntity, Rule>> result;
    result.reserve(refs->values.size());
    for (const auto id : refs->values) {
      if (!hasRule(id) || _registry.get<TechRuleOwner>(id.entity()).owner != owner.entity()) {
        throw std::logic_error("corrupt ROUTING typed rule references");
      }
      result.push_back(id);
    }
    return result;
  }

  template <typename Rule>
  [[nodiscard]] uint32_t repeatedRuleCount(TechRoutingLayerId owner) const
  {
    const auto* refs = _registry.try_get<TechRuleRefs<EnttId<TechEntity, Rule>>>(owner.entity());
    return refs == nullptr ? 0u : static_cast<uint32_t>(refs->values.size());
  }

  template <typename Rule>
  void ensureRule(EnttId<TechEntity, Rule> id) const
  {
    if (!hasRule(id)) {
      throw std::out_of_range("invalid tech routing rule id");
    }
  }

  void ensureLayer(TechRoutingLayerId id) const;
  [[nodiscard]] TechEntity createOwnedRule(TechRoutingLayerId owner);

  static void validatePrlSpacingTable(const TechRoutingPrlSpacingTableRule& rule);
  static void validateInfluenceSpacingTable(const TechRoutingInfluenceSpacingTableRule& rule);
  static void validateTwoWidthsSpacingTable(const TechRoutingTwoWidthsSpacingTableRule& rule);
  static void validateCurrentDensity(TechRoutingCurrentDensityRule& rule);
  static void validateLef58WidthTable(const TechRoutingLef58WidthTableRule& rule);

  registry_type& _registry;
};

}  // namespace eccdb
