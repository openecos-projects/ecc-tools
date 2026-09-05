#pragma once

#include <algorithm>
#include <cstdint>
#include <entt/entt.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/common/TechRuleRelations.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/cut_layer/model/CutRuleComponents.h"
#include "tech/cut_layer/model/CutRuleIds.h"

namespace eccdb {

// CUT facade over the shared Tech registry. A Rule is always its own entity;
// its variable-length attributes remain ordinary vectors in that Rule's
// component. Layer-side relations express whether the Rule is single-valued
// or repeatable for its LEF construct.
class TechCutLayerStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  explicit TechCutLayerStorage(TechRegistry& registry) : _registry(registry.registry()) {}

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }

  [[nodiscard]] TechCutLayerId createLayer(TechLayerInfo info, TechCutLayer cut);
  [[nodiscard]] bool contains(TechCutLayerId id) const;
  [[nodiscard]] TechCutLayerId findLayerById(uint32_t id) const;

  [[nodiscard]] TechLayerInfo& layerInfo(TechCutLayerId id);
  [[nodiscard]] const TechLayerInfo& layerInfo(TechCutLayerId id) const;
  [[nodiscard]] TechCutLayer& cutLayer(TechCutLayerId id);
  [[nodiscard]] const TechCutLayer& cutLayer(TechCutLayerId id) const;
  [[nodiscard]] uint32_t ruleCount(TechCutLayerId id) const;

  // Native LEF CUT rules.
  [[nodiscard]] TechCutSpacingRuleId addSpacingRule(TechCutLayerId owner, TechCutSpacingRule rule);
  [[nodiscard]] std::vector<TechCutSpacingRuleId> spacingRules(TechCutLayerId owner) const;

  [[nodiscard]] TechCutEnclosureRuleId addEnclosureRule(TechCutLayerId owner, TechCutEnclosureRule rule);
  // Compatibility lookup. Repeatable rules must be enumerated with enclosureRules().
  [[nodiscard]] TechCutEnclosureRuleId enclosureRule(TechCutLayerId owner, CutLayerSide side) const;
  [[nodiscard]] std::vector<TechCutEnclosureRuleId> enclosureRules(TechCutLayerId owner) const;

  // The legacy iDB model exposes native ARRAYSPACING as one CUT-layer property.
  [[nodiscard]] TechCutArraySpacingRuleId setArraySpacingRule(TechCutLayerId owner, TechCutArraySpacingRule rule);
  [[nodiscard]] TechCutArraySpacingRuleId arraySpacingRule(TechCutLayerId owner) const;

  [[nodiscard]] TechCutOrthogonalSpacingTableRuleId addOrthogonalSpacingTableRule(
      TechCutLayerId owner, TechCutOrthogonalSpacingTableRule rule);
  [[nodiscard]] std::vector<TechCutOrthogonalSpacingTableRuleId> orthogonalSpacingTableRules(TechCutLayerId owner) const;

  // Repeatable LEF58 CUT rules.
  [[nodiscard]] TechCutLef58CutClassRuleId addLef58CutClassRule(TechCutLayerId owner, TechCutLef58CutClassRule rule);
  [[nodiscard]] std::vector<TechCutLef58CutClassRuleId> lef58CutClassRules(TechCutLayerId owner) const;
  [[nodiscard]] TechCutLef58EnclosureRuleId addLef58EnclosureRule(TechCutLayerId owner, TechCutLef58EnclosureRule rule);
  [[nodiscard]] std::vector<TechCutLef58EnclosureRuleId> lef58EnclosureRules(TechCutLayerId owner) const;
  [[nodiscard]] TechCutLef58EnclosureEdgeRuleId addLef58EnclosureEdgeRule(TechCutLayerId owner, TechCutLef58EnclosureEdgeRule rule);
  [[nodiscard]] std::vector<TechCutLef58EnclosureEdgeRuleId> lef58EnclosureEdgeRules(TechCutLayerId owner) const;

  // These two LEF58 properties are single-valued in the current iDB model.
  [[nodiscard]] TechCutLef58EolEnclosureRuleId setLef58EolEnclosureRule(TechCutLayerId owner, TechCutLef58EolEnclosureRule rule);
  [[nodiscard]] TechCutLef58EolEnclosureRuleId lef58EolEnclosureRule(TechCutLayerId owner) const;
  [[nodiscard]] TechCutLef58EolSpacingRuleId setLef58EolSpacingRule(TechCutLayerId owner, TechCutLef58EolSpacingRule rule);
  [[nodiscard]] TechCutLef58EolSpacingRuleId lef58EolSpacingRule(TechCutLayerId owner) const;

  [[nodiscard]] TechCutLef58SpacingTableRuleId addLef58SpacingTableRule(TechCutLayerId owner, TechCutLef58SpacingTableRule rule);
  [[nodiscard]] std::vector<TechCutLef58SpacingTableRuleId> lef58SpacingTableRules(TechCutLayerId owner) const;
  [[nodiscard]] const TechCutLef58SpacingTableCell& lef58SpacingTableCell(TechCutLef58SpacingTableRuleId rule, uint32_t class1_index,
                                                                          uint32_t class2_index) const;

  // AC/DC current-density rules and their rectangular tables.
  [[nodiscard]] TechCutCurrentDensityRuleId addCurrentDensityRule(TechCutLayerId owner, TechCutCurrentDensityRule rule);
  [[nodiscard]] std::vector<TechCutCurrentDensityRuleId> currentDensityRules(TechCutLayerId owner) const;
  [[nodiscard]] double currentDensityTableEntry(TechCutCurrentDensityRuleId rule, uint32_t frequency_index, uint32_t cut_area_index) const;
  [[nodiscard]] double currentDensityAt(TechCutCurrentDensityRuleId rule, double frequency, int64_t cut_area) const;

  // Explicit Rule access keeps the public schema visible without a generic
  // addRule/rule/rules template layer.
  [[nodiscard]] bool hasSpacingRule(TechCutSpacingRuleId id) const;
  [[nodiscard]] TechCutSpacingRuleId findSpacingRuleById(uint32_t id) const;
  [[nodiscard]] TechCutSpacingRule& spacingRule(TechCutSpacingRuleId id);
  [[nodiscard]] const TechCutSpacingRule& spacingRule(TechCutSpacingRuleId id) const;
  [[nodiscard]] TechCutLayerId spacingRuleOwner(TechCutSpacingRuleId id) const;
  [[nodiscard]] bool destroySpacingRule(TechCutSpacingRuleId id);

  [[nodiscard]] bool hasEnclosureRule(TechCutEnclosureRuleId id) const;
  [[nodiscard]] TechCutEnclosureRuleId findEnclosureRuleById(uint32_t id) const;
  [[nodiscard]] TechCutEnclosureRule& enclosureRule(TechCutEnclosureRuleId id);
  [[nodiscard]] const TechCutEnclosureRule& enclosureRule(TechCutEnclosureRuleId id) const;
  [[nodiscard]] TechCutLayerId enclosureRuleOwner(TechCutEnclosureRuleId id) const;
  [[nodiscard]] bool destroyEnclosureRule(TechCutEnclosureRuleId id);

  [[nodiscard]] bool hasArraySpacingRule(TechCutArraySpacingRuleId id) const;
  [[nodiscard]] TechCutArraySpacingRuleId findArraySpacingRuleById(uint32_t id) const;
  [[nodiscard]] const TechCutArraySpacingRule& arraySpacingRule(TechCutArraySpacingRuleId id) const;
  [[nodiscard]] TechCutLayerId arraySpacingRuleOwner(TechCutArraySpacingRuleId id) const;
  [[nodiscard]] bool destroyArraySpacingRule(TechCutArraySpacingRuleId id);

  [[nodiscard]] bool hasOrthogonalSpacingTableRule(TechCutOrthogonalSpacingTableRuleId id) const;
  [[nodiscard]] TechCutOrthogonalSpacingTableRuleId findOrthogonalSpacingTableRuleById(uint32_t id) const;
  [[nodiscard]] TechCutOrthogonalSpacingTableRule& orthogonalSpacingTableRule(TechCutOrthogonalSpacingTableRuleId id);
  [[nodiscard]] const TechCutOrthogonalSpacingTableRule& orthogonalSpacingTableRule(TechCutOrthogonalSpacingTableRuleId id) const;
  [[nodiscard]] TechCutLayerId orthogonalSpacingTableRuleOwner(TechCutOrthogonalSpacingTableRuleId id) const;
  [[nodiscard]] bool destroyOrthogonalSpacingTableRule(TechCutOrthogonalSpacingTableRuleId id);

  [[nodiscard]] bool hasLef58CutClassRule(TechCutLef58CutClassRuleId id) const;
  [[nodiscard]] TechCutLef58CutClassRuleId findLef58CutClassRuleById(uint32_t id) const;
  [[nodiscard]] TechCutLef58CutClassRule& lef58CutClassRule(TechCutLef58CutClassRuleId id);
  [[nodiscard]] const TechCutLef58CutClassRule& lef58CutClassRule(TechCutLef58CutClassRuleId id) const;
  [[nodiscard]] TechCutLayerId lef58CutClassRuleOwner(TechCutLef58CutClassRuleId id) const;
  [[nodiscard]] bool destroyLef58CutClassRule(TechCutLef58CutClassRuleId id);

  [[nodiscard]] bool hasLef58EnclosureRule(TechCutLef58EnclosureRuleId id) const;
  [[nodiscard]] TechCutLef58EnclosureRuleId findLef58EnclosureRuleById(uint32_t id) const;
  [[nodiscard]] TechCutLef58EnclosureRule& lef58EnclosureRule(TechCutLef58EnclosureRuleId id);
  [[nodiscard]] const TechCutLef58EnclosureRule& lef58EnclosureRule(TechCutLef58EnclosureRuleId id) const;
  [[nodiscard]] TechCutLayerId lef58EnclosureRuleOwner(TechCutLef58EnclosureRuleId id) const;
  [[nodiscard]] bool destroyLef58EnclosureRule(TechCutLef58EnclosureRuleId id);

  [[nodiscard]] bool hasLef58EnclosureEdgeRule(TechCutLef58EnclosureEdgeRuleId id) const;
  [[nodiscard]] TechCutLef58EnclosureEdgeRuleId findLef58EnclosureEdgeRuleById(uint32_t id) const;
  [[nodiscard]] TechCutLef58EnclosureEdgeRule& lef58EnclosureEdgeRule(TechCutLef58EnclosureEdgeRuleId id);
  [[nodiscard]] const TechCutLef58EnclosureEdgeRule& lef58EnclosureEdgeRule(TechCutLef58EnclosureEdgeRuleId id) const;
  [[nodiscard]] TechCutLayerId lef58EnclosureEdgeRuleOwner(TechCutLef58EnclosureEdgeRuleId id) const;
  [[nodiscard]] bool destroyLef58EnclosureEdgeRule(TechCutLef58EnclosureEdgeRuleId id);

  [[nodiscard]] bool hasLef58EolEnclosureRule(TechCutLef58EolEnclosureRuleId id) const;
  [[nodiscard]] TechCutLef58EolEnclosureRuleId findLef58EolEnclosureRuleById(uint32_t id) const;
  [[nodiscard]] TechCutLef58EolEnclosureRule& lef58EolEnclosureRule(TechCutLef58EolEnclosureRuleId id);
  [[nodiscard]] const TechCutLef58EolEnclosureRule& lef58EolEnclosureRule(TechCutLef58EolEnclosureRuleId id) const;
  [[nodiscard]] TechCutLayerId lef58EolEnclosureRuleOwner(TechCutLef58EolEnclosureRuleId id) const;
  [[nodiscard]] bool destroyLef58EolEnclosureRule(TechCutLef58EolEnclosureRuleId id);

  [[nodiscard]] bool hasLef58EolSpacingRule(TechCutLef58EolSpacingRuleId id) const;
  [[nodiscard]] TechCutLef58EolSpacingRuleId findLef58EolSpacingRuleById(uint32_t id) const;
  [[nodiscard]] const TechCutLef58EolSpacingRule& lef58EolSpacingRule(TechCutLef58EolSpacingRuleId id) const;
  [[nodiscard]] TechCutLayerId lef58EolSpacingRuleOwner(TechCutLef58EolSpacingRuleId id) const;
  [[nodiscard]] bool destroyLef58EolSpacingRule(TechCutLef58EolSpacingRuleId id);

  [[nodiscard]] bool hasLef58SpacingTableRule(TechCutLef58SpacingTableRuleId id) const;
  [[nodiscard]] TechCutLef58SpacingTableRuleId findLef58SpacingTableRuleById(uint32_t id) const;
  [[nodiscard]] const TechCutLef58SpacingTableRule& lef58SpacingTableRule(TechCutLef58SpacingTableRuleId id) const;
  [[nodiscard]] TechCutLayerId lef58SpacingTableRuleOwner(TechCutLef58SpacingTableRuleId id) const;
  [[nodiscard]] bool destroyLef58SpacingTableRule(TechCutLef58SpacingTableRuleId id);

  [[nodiscard]] bool hasCurrentDensityRule(TechCutCurrentDensityRuleId id) const;
  [[nodiscard]] TechCutCurrentDensityRuleId findCurrentDensityRuleById(uint32_t id) const;
  [[nodiscard]] const TechCutCurrentDensityRule& currentDensityRule(TechCutCurrentDensityRuleId id) const;
  [[nodiscard]] TechCutLayerId currentDensityRuleOwner(TechCutCurrentDensityRuleId id) const;
  [[nodiscard]] bool destroyCurrentDensityRule(TechCutCurrentDensityRuleId id);

 private:
  static void validateLayerInfo(const TechLayerInfo& info);
  void ensureLayer(TechCutLayerId id) const;
  void ensureOwnedRule(TechEntity id) const;
  [[nodiscard]] TechEntity createOwnedRule(TechCutLayerId owner);
  [[nodiscard]] TechCutLayerId ownerOf(TechEntity rule) const;

  template <typename Rule>
  [[nodiscard]] EnttId<TechEntity, Rule> addRepeatedRule(TechCutLayerId owner, Rule rule)
  {
    const auto entity = createOwnedRule(owner);
    try {
      _registry.emplace<Rule>(entity, std::move(rule));
      _registry.get_or_emplace<TechRuleRefs<EnttId<TechEntity, Rule>>>(owner.entity()).values.emplace_back(entity);
    } catch (...) {
      if (_registry.valid(entity)) {
        _registry.destroy(entity);
      }
      throw;
    }
    return EnttId<TechEntity, Rule>{entity};
  }

  template <typename Rule>
  [[nodiscard]] std::vector<EnttId<TechEntity, Rule>> repeatedRules(TechCutLayerId owner) const
  {
    ensureLayer(owner);
    const auto* refs = _registry.try_get<TechRuleRefs<EnttId<TechEntity, Rule>>>(owner.entity());
    if (refs == nullptr) {
      return {};
    }
    for (const auto id : refs->values) {
      if (!_registry.valid(id.entity()) || !_registry.all_of<TechRuleOwner, Rule>(id.entity())
          || _registry.get<TechRuleOwner>(id.entity()).owner != owner.entity()) {
        throw std::logic_error("corrupt CUT typed rule references");
      }
    }
    return refs->values;
  }

  template <typename Rule>
  [[nodiscard]] bool destroyRepeatedRule(EnttId<TechEntity, Rule> id)
  {
    if (!_registry.valid(id.entity()) || !_registry.all_of<TechRuleOwner, Rule>(id.entity())) {
      return false;
    }
    const auto owner = ownerOf(id.entity());
    auto* refs = _registry.try_get<TechRuleRefs<EnttId<TechEntity, Rule>>>(owner.entity());
    if (refs == nullptr) {
      throw std::logic_error("missing CUT typed rule references");
    }
    const auto it = std::find(refs->values.begin(), refs->values.end(), id);
    if (it == refs->values.end()) {
      throw std::logic_error("CUT rule is absent from its owner references");
    }
    refs->values.erase(it);
    const bool remove_refs = refs->values.empty();
    _registry.destroy(id.entity());
    if (remove_refs) {
      _registry.remove<TechRuleRefs<EnttId<TechEntity, Rule>>>(owner.entity());
    }
    return true;
  }

  registry_type& _registry;
};

}  // namespace eccdb
