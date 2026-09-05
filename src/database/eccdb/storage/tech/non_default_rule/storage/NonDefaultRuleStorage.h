#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "geometry/GeometryPool.h"
#include "tech/TechRegistry.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/via_master/storage/ViaMasterInput.h"

namespace eccdb {

// CRUD facade for tech-scope LEF NONDEFAULTRULE objects.
//
// Each named NDR is one entity. Repeatable value clauses live in typed vector
// components on that entity. Only named NDR VIA definitions are child entities,
// because another NDR can refer to them through USEVIA.
class TechNonDefaultRuleStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  TechNonDefaultRuleStorage(TechRegistry& registry, GeometryPool& geometry) : _registry(registry.registry()), _geometry(geometry) {}

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }

  [[nodiscard]] TechNonDefaultRuleId createNonDefaultRule(TechNonDefaultRule rule);
  [[nodiscard]] bool destroyNonDefaultRule(TechNonDefaultRuleId id);
  void renameNonDefaultRule(TechNonDefaultRuleId id, std::string name);
  void setHardSpacing(TechNonDefaultRuleId id, bool enabled);

  void setRoutingRule(TechNonDefaultRuleId owner, TechNdrRoutingRule rule);
  void setMinCutsRule(TechNonDefaultRuleId owner, TechNdrMinCutsRule rule);
  void addUseVia(TechNonDefaultRuleId owner, TechViaMasterId via);
  void addUseViaRule(TechNonDefaultRuleId owner, TechViaRuleGenerateId via_rule);
  void addProperty(TechNonDefaultRuleId owner, TechNdrProperty property);
  [[nodiscard]] TechViaMasterId addFixedViaDefinition(TechNonDefaultRuleId owner, TechViaMaster master, TechViaMasterShapeInput shapes);
  [[nodiscard]] TechViaMasterId addGeneratedViaDefinition(TechNonDefaultRuleId owner, TechViaMaster master,
                                                          TechGeneratedViaMaster generated, TechViaMasterShapeInput shapes);
  void addSameNetSpacingRule(TechNonDefaultRuleId owner, TechNdrSameNetSpacingRule rule);

  [[nodiscard]] bool destroyViaDefinition(TechViaMasterId id);
  [[nodiscard]] bool contains(TechNonDefaultRuleId id) const;
  [[nodiscard]] bool containsViaDefinition(TechViaMasterId id) const;

  [[nodiscard]] TechNonDefaultRuleId findNonDefaultRuleById(uint32_t id) const;
  [[nodiscard]] TechNonDefaultRuleId findNonDefaultRule(std::string_view name) const;
  [[nodiscard]] TechViaMasterId findViaDefinitionById(uint32_t id) const;
  [[nodiscard]] TechViaMasterId findViaDefinition(std::string_view name) const;

  [[nodiscard]] const TechNonDefaultRule& nonDefaultRule(TechNonDefaultRuleId id) const;
  [[nodiscard]] const TechNdrRoutingRule* routingRule(TechNonDefaultRuleId owner, TechRoutingLayerId layer) const;
  [[nodiscard]] const TechNdrMinCutsRule* minCutsRule(TechNonDefaultRuleId owner, TechCutLayerId layer) const;
  [[nodiscard]] const TechViaMaster& viaDefinition(TechViaMasterId id) const;
  [[nodiscard]] const TechViaGeometry& viaDefinitionGeometry(TechViaMasterId id) const;
  [[nodiscard]] const TechGeneratedViaMaster* generatedViaDefinition(TechViaMasterId id) const;
  [[nodiscard]] TechNonDefaultRuleId viaDefinitionOwner(TechViaMasterId id) const;

  [[nodiscard]] const std::vector<TechNdrRoutingRule>& routingRules(TechNonDefaultRuleId owner) const;
  [[nodiscard]] const std::vector<TechNdrMinCutsRule>& minCutsRules(TechNonDefaultRuleId owner) const;
  [[nodiscard]] const std::vector<TechViaMasterId>& useVias(TechNonDefaultRuleId owner) const;
  [[nodiscard]] const std::vector<TechViaRuleGenerateId>& useViaRules(TechNonDefaultRuleId owner) const;
  [[nodiscard]] const std::vector<TechNdrProperty>& properties(TechNonDefaultRuleId owner) const;
  [[nodiscard]] const std::vector<TechViaMasterId>& viaDefinitions(TechNonDefaultRuleId owner) const;
  [[nodiscard]] const std::vector<TechNdrSameNetSpacingRule>& sameNetSpacingRules(TechNonDefaultRuleId owner) const;

  [[nodiscard]] std::vector<TechNonDefaultRuleId> nonDefaultRules() const;
  [[nodiscard]] std::vector<TechNonDefaultRuleId> nonDefaultRulesForRoutingLayer(TechRoutingLayerId layer) const;
  [[nodiscard]] std::vector<TechNonDefaultRuleId> nonDefaultRulesForCutLayer(TechCutLayerId layer) const;

 private:
  [[nodiscard]] TechViaMasterId createViaDefinition(TechNonDefaultRuleId owner, TechViaMaster master, TechViaGeometry geometry,
                                                    TechGeneratedViaMaster* generated);
  [[nodiscard]] bool isViaReferencedOutsideOwner(TechViaMasterId via, TechNonDefaultRuleId owner) const;

  void ensureRule(TechNonDefaultRuleId id) const;
  void ensureViaDefinition(TechViaMasterId id) const;
  void validateRule(const TechNonDefaultRule& rule) const;
  void validateRoutingRule(TechNonDefaultRuleId owner, const TechNdrRoutingRule& rule) const;
  void validateMinCutsRule(TechNonDefaultRuleId owner, const TechNdrMinCutsRule& rule) const;
  void validateUseVia(TechNonDefaultRuleId owner, TechViaMasterId via) const;
  void validateUseViaRule(TechNonDefaultRuleId owner, TechViaRuleGenerateId via_rule) const;
  void validateProperty(TechNonDefaultRuleId owner, const TechNdrProperty& property) const;
  void validateViaDefinition(TechNonDefaultRuleId owner, const TechViaMaster& master, const TechViaMasterShapeInput& shapes) const;
  void validateSameNetSpacingRule(TechNonDefaultRuleId owner, const TechNdrSameNetSpacingRule& rule) const;

  registry_type& _registry;
  GeometryPool& _geometry;
};

}  // namespace eccdb
