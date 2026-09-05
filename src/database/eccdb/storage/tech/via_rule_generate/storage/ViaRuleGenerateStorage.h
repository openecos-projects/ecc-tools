#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "tech/TechRegistry.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {

// CRUD/query facade for complete LEF VIARULE ... GENERATE objects. Each rule
// and its bottom/cut/top clauses occupy one entity in the shared TechRegistry.
class ViaRuleGenerateStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  explicit ViaRuleGenerateStorage(TechRegistry& registry) : _registry(registry.registry()) {}

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }

  // Names use the catalog's existing shared technology string pool.
  [[nodiscard]] TechViaRuleGenerateId createViaRuleGenerate(TechViaRuleGenerate rule, TechViaRuleGenerateBottomLayer bottom,
                                                            TechViaRuleGenerateCutLayer cut, TechViaRuleGenerateTopLayer top);

  [[nodiscard]] bool contains(TechViaRuleGenerateId id) const;
  [[nodiscard]] TechViaRuleGenerateId findViaRuleGenerateById(uint32_t id) const;
  [[nodiscard]] TechViaRuleGenerateId findViaRuleGenerate(std::string_view name) const;

  [[nodiscard]] std::vector<TechViaRuleGenerateId> viaRuleGenerates() const;
  [[nodiscard]] std::vector<TechViaRuleGenerateId> defaultViaRuleGenerates() const;
  [[nodiscard]] std::vector<TechViaRuleGenerateId> viaRuleGeneratesForCutLayer(TechCutLayerId cut_layer) const;
  [[nodiscard]] TechViaRuleGenerateId defaultViaRuleGenerateForCutLayer(TechCutLayerId cut_layer) const;
  [[nodiscard]] std::vector<TechViaRuleGenerateId> viaRuleGeneratesForLayer(TechLayerId layer) const;

  [[nodiscard]] TechViaRuleGenerate& viaRuleGenerate(TechViaRuleGenerateId id);
  [[nodiscard]] const TechViaRuleGenerate& viaRuleGenerate(TechViaRuleGenerateId id) const;
  [[nodiscard]] TechViaRuleGenerateBottomLayer& bottomLayer(TechViaRuleGenerateId id);
  [[nodiscard]] const TechViaRuleGenerateBottomLayer& bottomLayer(TechViaRuleGenerateId id) const;
  [[nodiscard]] TechViaRuleGenerateCutLayer& cutLayer(TechViaRuleGenerateId id);
  [[nodiscard]] const TechViaRuleGenerateCutLayer& cutLayer(TechViaRuleGenerateId id) const;
  [[nodiscard]] TechViaRuleGenerateTopLayer& topLayer(TechViaRuleGenerateId id);
  [[nodiscard]] const TechViaRuleGenerateTopLayer& topLayer(TechViaRuleGenerateId id) const;

 private:
  void ensureRule(TechViaRuleGenerateId id) const;
  void validateRule(const TechViaRuleGenerate& rule) const;
  void validateConductorLayer(const TechViaRuleGenerateBottomLayer& layer, const char* role) const;
  void validateConductorLayer(const TechViaRuleGenerateTopLayer& layer, const char* role) const;
  void validateCutLayer(const TechViaRuleGenerateCutLayer& layer) const;

  registry_type& _registry;
};

}  // namespace eccdb
