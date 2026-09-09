#include "tech/via_rule/storage/ViaRuleStorage.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {

TechViaRuleId ViaRuleStorage::createViaRule(TechViaRule rule, TechViaRuleLowerLayer lower, TechViaRuleUpperLayer upper,
                                            std::vector<TechViaMasterId> candidates, std::vector<TechViaRuleProperty> properties)
{
  validateRule(rule);
  validateLayer(lower, "lower");
  validateLayer(upper, "upper");
  if (lower.layer == upper.layer) {
    throw std::invalid_argument("via rule lower and upper layers must differ");
  }
  if (!techLayerIsBelow(layerSequence(), lower.layer.entity(), upper.layer.entity())) {
    throw std::invalid_argument("via rule lower layer must precede upper layer");
  }
  validateCandidates(candidates);
  validateProperties(properties);

  const auto entity = _registry.create();
  try {
    _registry.emplace<TechViaRule>(entity, std::move(rule));
    _registry.emplace<TechViaRuleLowerLayer>(entity, std::move(lower));
    _registry.emplace<TechViaRuleUpperLayer>(entity, std::move(upper));
    _registry.emplace<TechViaRuleCandidates>(entity, TechViaRuleCandidates{.values = std::move(candidates)});
    _registry.emplace<TechViaRuleProperties>(entity, TechViaRuleProperties{.values = std::move(properties)});
    return TechViaRuleId{entity};
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
}

bool ViaRuleStorage::contains(TechViaRuleId id) const
{
  return _registry.valid(id.entity())
         && _registry.all_of<TechViaRule, TechViaRuleLowerLayer, TechViaRuleUpperLayer, TechViaRuleCandidates, TechViaRuleProperties>(
             id.entity());
}

TechViaRuleId ViaRuleStorage::findViaRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity)
                 && _registry
                        .all_of<TechViaRule, TechViaRuleLowerLayer, TechViaRuleUpperLayer, TechViaRuleCandidates, TechViaRuleProperties>(
                            entity)
             ? TechViaRuleId{entity}
             : TechViaRuleId{};
}

TechViaRuleId ViaRuleStorage::findViaRule(std::string_view name) const
{
  const auto view = _registry.view<const TechViaRule, const TechViaRuleLowerLayer, const TechViaRuleUpperLayer, const TechViaRuleCandidates,
                                   const TechViaRuleProperties>();
  for (const auto entity : view) {
    if (view.get<const TechViaRule>(entity).name == name) {
      return TechViaRuleId{entity};
    }
  }
  return {};
}

std::vector<TechViaRuleId> ViaRuleStorage::viaRules() const
{
  std::vector<TechViaRuleId> result;
  const auto view = _registry.view<const TechViaRule, const TechViaRuleLowerLayer, const TechViaRuleUpperLayer, const TechViaRuleCandidates,
                                   const TechViaRuleProperties>();
  result.reserve(view.size_hint());
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

std::vector<TechViaRuleId> ViaRuleStorage::viaRulesForLayer(TechRoutingLayerId layer) const
{
  std::vector<TechViaRuleId> result;
  const auto view = _registry.view<const TechViaRule, const TechViaRuleLowerLayer, const TechViaRuleUpperLayer, const TechViaRuleCandidates,
                                   const TechViaRuleProperties>();
  for (const auto entity : view) {
    if (view.get<const TechViaRuleLowerLayer>(entity).layer == layer || view.get<const TechViaRuleUpperLayer>(entity).layer == layer) {
      result.emplace_back(entity);
    }
  }
  return result;
}

TechViaRule& ViaRuleStorage::viaRule(TechViaRuleId id)
{
  ensureRule(id);
  return _registry.get<TechViaRule>(id.entity());
}

const TechViaRule& ViaRuleStorage::viaRule(TechViaRuleId id) const
{
  ensureRule(id);
  return _registry.get<TechViaRule>(id.entity());
}

TechViaRuleLowerLayer& ViaRuleStorage::lowerLayer(TechViaRuleId id)
{
  ensureRule(id);
  return _registry.get<TechViaRuleLowerLayer>(id.entity());
}

const TechViaRuleLowerLayer& ViaRuleStorage::lowerLayer(TechViaRuleId id) const
{
  ensureRule(id);
  return _registry.get<TechViaRuleLowerLayer>(id.entity());
}

TechViaRuleUpperLayer& ViaRuleStorage::upperLayer(TechViaRuleId id)
{
  ensureRule(id);
  return _registry.get<TechViaRuleUpperLayer>(id.entity());
}

const TechViaRuleUpperLayer& ViaRuleStorage::upperLayer(TechViaRuleId id) const
{
  ensureRule(id);
  return _registry.get<TechViaRuleUpperLayer>(id.entity());
}

std::span<const TechViaMasterId> ViaRuleStorage::candidates(TechViaRuleId id) const
{
  ensureRule(id);
  return std::span<const TechViaMasterId>{_registry.get<TechViaRuleCandidates>(id.entity()).values};
}

std::span<const TechViaRuleProperty> ViaRuleStorage::properties(TechViaRuleId id) const
{
  ensureRule(id);
  return std::span<const TechViaRuleProperty>{_registry.get<TechViaRuleProperties>(id.entity()).values};
}

void ViaRuleStorage::ensureRule(TechViaRuleId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid tech via rule id");
  }
}

void ViaRuleStorage::validateRule(const TechViaRule& rule) const
{
  if (rule.name.empty()) {
    throw std::invalid_argument("via rule name is required");
  }
  if (findViaRule(rule.name)) {
    throw std::invalid_argument("duplicate via rule name");
  }
}

void ViaRuleStorage::validateLayer(const TechViaRuleLowerLayer& layer, const char* role) const
{
  if (!_registry.valid(layer.layer.entity()) || !_registry.all_of<TechLayerInfo, TechRoutingLayer>(layer.layer.entity())) {
    throw std::invalid_argument(std::string{"via rule "} + role + " layer is invalid");
  }
  if (layer.direction == RoutingDirection::kUnknown) {
    throw std::invalid_argument(std::string{"via rule "} + role + " direction is required");
  }
  if ((layer.flags & TechViaRuleLayerFlag::kHasWidth) != 0u && layer.min_width > layer.max_width) {
    throw std::invalid_argument(std::string{"via rule "} + role + " width range is invalid");
  }
}

void ViaRuleStorage::validateLayer(const TechViaRuleUpperLayer& layer, const char* role) const
{
  if (!_registry.valid(layer.layer.entity()) || !_registry.all_of<TechLayerInfo, TechRoutingLayer>(layer.layer.entity())) {
    throw std::invalid_argument(std::string{"via rule "} + role + " layer is invalid");
  }
  if (layer.direction == RoutingDirection::kUnknown) {
    throw std::invalid_argument(std::string{"via rule "} + role + " direction is required");
  }
  if ((layer.flags & TechViaRuleLayerFlag::kHasWidth) != 0u && layer.min_width > layer.max_width) {
    throw std::invalid_argument(std::string{"via rule "} + role + " width range is invalid");
  }
}

void ViaRuleStorage::validateCandidates(const std::vector<TechViaMasterId>& candidates) const
{
  if (candidates.empty()) {
    throw std::invalid_argument("via rule requires at least one candidate via");
  }
  for (const auto candidate : candidates) {
    if (!_registry.valid(candidate.entity()) || !_registry.all_of<TechViaMaster, TechViaGeometry>(candidate.entity())) {
      throw std::invalid_argument("via rule references an invalid candidate via");
    }
    if (_registry.all_of<TechGeneratedViaMaster>(candidate.entity())) {
      throw std::invalid_argument("ordinary via rule candidate must be a fixed via");
    }
  }
}

void ViaRuleStorage::validateProperties(const std::vector<TechViaRuleProperty>& properties) const
{
  for (const auto& property : properties) {
    if (property.name.empty()) {
      throw std::invalid_argument("via rule property name is required");
    }
  }
}

const TechLayerSequence& ViaRuleStorage::layerSequence() const
{
  const auto* sequence = _registry.valid(_root.entity()) ? _registry.try_get<TechLayerSequence>(_root.entity()) : nullptr;
  if (sequence == nullptr) {
    throw std::logic_error("technology root has no layer sequence");
  }
  return *sequence;
}

}  // namespace eccdb
