#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "tech/TechRegistry.h"
#include "tech/via_rule/model/ViaRuleComponents.h"

namespace eccdb {

// Facade for ordinary (non-GENERATE) LEF VIARULE objects. The two mandatory
// routing-layer clauses are components on the rule entity; variable candidates
// and properties are packed sidecar ranges.
class ViaRuleStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  ViaRuleStorage(TechRegistry& registry, TechRootId root) : _registry(registry.registry()), _root(root) {}

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }
  void rebindRoot(TechRootId root) noexcept { _root = root; }

  [[nodiscard]] TechViaRuleId createViaRule(TechViaRule rule, TechViaRuleLowerLayer lower, TechViaRuleUpperLayer upper,
                                            std::vector<TechViaMasterId> candidates, std::vector<TechViaRuleProperty> properties = {});

  [[nodiscard]] bool contains(TechViaRuleId id) const;
  [[nodiscard]] TechViaRuleId findViaRuleById(uint32_t id) const;
  [[nodiscard]] TechViaRuleId findViaRule(std::string_view name) const;
  [[nodiscard]] std::vector<TechViaRuleId> viaRules() const;
  [[nodiscard]] std::vector<TechViaRuleId> viaRulesForLayer(TechRoutingLayerId layer) const;

  [[nodiscard]] TechViaRule& viaRule(TechViaRuleId id);
  [[nodiscard]] const TechViaRule& viaRule(TechViaRuleId id) const;
  [[nodiscard]] TechViaRuleLowerLayer& lowerLayer(TechViaRuleId id);
  [[nodiscard]] const TechViaRuleLowerLayer& lowerLayer(TechViaRuleId id) const;
  [[nodiscard]] TechViaRuleUpperLayer& upperLayer(TechViaRuleId id);
  [[nodiscard]] const TechViaRuleUpperLayer& upperLayer(TechViaRuleId id) const;
  [[nodiscard]] std::span<const TechViaMasterId> candidates(TechViaRuleId id) const;
  [[nodiscard]] std::span<const TechViaRuleProperty> properties(TechViaRuleId id) const;

 private:
  void ensureRule(TechViaRuleId id) const;
  void validateRule(const TechViaRule& rule) const;
  void validateLayer(const TechViaRuleLowerLayer& layer, const char* role) const;
  void validateLayer(const TechViaRuleUpperLayer& layer, const char* role) const;
  void validateCandidates(const std::vector<TechViaMasterId>& candidates) const;
  void validateProperties(const std::vector<TechViaRuleProperty>& properties) const;
  [[nodiscard]] const TechLayerSequence& layerSequence() const;

  registry_type& _registry;
  TechRootId _root;
};

}  // namespace eccdb
