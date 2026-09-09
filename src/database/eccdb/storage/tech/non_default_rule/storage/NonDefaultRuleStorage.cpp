#include "tech/non_default_rule/storage/NonDefaultRuleStorage.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/masterslice_layer/model/MastersliceLayerComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {
namespace {

TechViaGeometry makeViaGeometry(GeometryPool& geometry_pool, TechViaMasterShapeInput shapes)
{
  const auto normalize = [](std::vector<Rect>& rects) {
    for (auto& rect : rects) {
      rect = rect.normalized();
      if (!rect.hasArea()) {
        throw std::invalid_argument("NDR VIA rectangle has no area");
      }
    }
  };
  normalize(shapes.bottom_geometry.rects);
  normalize(shapes.cut_geometry.rects);
  normalize(shapes.top_geometry.rects);

  const auto mark = geometry_pool.checkpoint();
  try {
    TechViaGeometry result;
    result.bottom_layer = shapes.bottom_layer;
    result.bottom_geometry = geometry_pool.append(shapes.bottom_geometry);
    result.cut_layer = shapes.cut_layer;
    result.cut_geometry = geometry_pool.append(shapes.cut_geometry);
    result.top_layer = shapes.top_layer;
    result.top_geometry = geometry_pool.append(shapes.top_geometry);
    result.bounding_box = geometry_pool.bounds(result.bottom_geometry)
                              .united(geometry_pool.bounds(result.cut_geometry))
                              .united(geometry_pool.bounds(result.top_geometry));
    return result;
  } catch (...) {
    geometry_pool.rollback(mark);
    throw;
  }
}

}  // namespace

TechNonDefaultRuleId TechNonDefaultRuleStorage::createNonDefaultRule(TechNonDefaultRule rule)
{
  validateRule(rule);

  const auto entity = _registry.create();
  try {
    _registry.emplace<TechNonDefaultRule>(entity, std::move(rule));
    _registry.emplace<TechNdrRoutingRules>(entity);
    _registry.emplace<TechNdrMinCutsRules>(entity);
    _registry.emplace<TechNdrUseVias>(entity);
    _registry.emplace<TechNdrUseViaRules>(entity);
    _registry.emplace<TechNdrProperties>(entity);
    _registry.emplace<TechNdrViaDefinitions>(entity);
    _registry.emplace<TechNdrSameNetSpacingRules>(entity);
    return TechNonDefaultRuleId{entity};
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
}

bool TechNonDefaultRuleStorage::destroyNonDefaultRule(TechNonDefaultRuleId id)
{
  if (!contains(id)) {
    return false;
  }

  const auto vias = viaDefinitions(id);
  for (const auto via : vias) {
    if (isViaReferencedOutsideOwner(via, id)) {
      throw std::logic_error("cannot destroy NDR while another NDR references one of its VIA definitions");
    }
  }
  for (const auto via : vias) {
    _registry.destroy(via.entity());
  }
  _registry.destroy(id.entity());
  return true;
}

void TechNonDefaultRuleStorage::renameNonDefaultRule(TechNonDefaultRuleId id, std::string name)
{
  ensureRule(id);
  if (name.empty()) {
    throw std::invalid_argument("nondefault rule name is required");
  }

  const auto existing = findNonDefaultRule(name);
  if (existing && existing != id) {
    throw std::invalid_argument("duplicate nondefault rule name");
  }
  _registry.get<TechNonDefaultRule>(id.entity()).name = std::move(name);
}

void TechNonDefaultRuleStorage::setHardSpacing(TechNonDefaultRuleId id, bool enabled)
{
  ensureRule(id);
  auto& rule = _registry.get<TechNonDefaultRule>(id.entity());
  if (enabled) {
    rule.flags |= TechNonDefaultRuleFlag::kHardSpacing;
  } else {
    rule.flags &= ~TechNonDefaultRuleFlag::kHardSpacing;
  }
}

void TechNonDefaultRuleStorage::setRoutingRule(TechNonDefaultRuleId owner, TechNdrRoutingRule rule)
{
  validateRoutingRule(owner, rule);
  auto& values = _registry.get<TechNdrRoutingRules>(owner.entity()).values;
  const auto found = std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.layer == rule.layer; });
  if (found != values.end()) {
    *found = std::move(rule);
  } else {
    values.push_back(std::move(rule));
  }
}

void TechNonDefaultRuleStorage::setMinCutsRule(TechNonDefaultRuleId owner, TechNdrMinCutsRule rule)
{
  validateMinCutsRule(owner, rule);
  auto& values = _registry.get<TechNdrMinCutsRules>(owner.entity()).values;
  const auto found = std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.layer == rule.layer; });
  if (found != values.end()) {
    *found = std::move(rule);
  } else {
    values.push_back(std::move(rule));
  }
}

void TechNonDefaultRuleStorage::addUseVia(TechNonDefaultRuleId owner, TechViaMasterId via)
{
  validateUseVia(owner, via);
  _registry.get<TechNdrUseVias>(owner.entity()).values.push_back(via);
}

void TechNonDefaultRuleStorage::addUseViaRule(TechNonDefaultRuleId owner, TechViaRuleGenerateId via_rule)
{
  validateUseViaRule(owner, via_rule);
  _registry.get<TechNdrUseViaRules>(owner.entity()).values.push_back(via_rule);
}

void TechNonDefaultRuleStorage::addProperty(TechNonDefaultRuleId owner, TechNdrProperty property)
{
  validateProperty(owner, property);
  _registry.get<TechNdrProperties>(owner.entity()).values.push_back(std::move(property));
}

TechViaMasterId TechNonDefaultRuleStorage::addFixedViaDefinition(TechNonDefaultRuleId owner, TechViaMaster master,
                                                                 TechViaMasterShapeInput shapes)
{
  const auto mark = _geometry.checkpoint();
  try {
    validateViaDefinition(owner, master, shapes);
    auto geometry = makeViaGeometry(_geometry, std::move(shapes));
    return createViaDefinition(owner, std::move(master), std::move(geometry), nullptr);
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

TechViaMasterId TechNonDefaultRuleStorage::addGeneratedViaDefinition(TechNonDefaultRuleId owner, TechViaMaster master,
                                                                     TechGeneratedViaMaster generated, TechViaMasterShapeInput shapes)
{
  const auto mark = _geometry.checkpoint();
  try {
    validateViaDefinition(owner, master, shapes);
    if (!generated.via_rule_generate || !_registry.valid(generated.via_rule_generate.entity())
        || !_registry.all_of<TechViaRuleGenerate, TechViaRuleGenerateBottomLayer, TechViaRuleGenerateCutLayer, TechViaRuleGenerateTopLayer>(
            generated.via_rule_generate.entity())) {
      throw std::invalid_argument("generated NDR VIA requires a valid generate rule");
    }
    auto geometry = makeViaGeometry(_geometry, std::move(shapes));
    return createViaDefinition(owner, std::move(master), std::move(geometry), &generated);
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

void TechNonDefaultRuleStorage::addSameNetSpacingRule(TechNonDefaultRuleId owner, TechNdrSameNetSpacingRule rule)
{
  validateSameNetSpacingRule(owner, rule);
  _registry.get<TechNdrSameNetSpacingRules>(owner.entity()).values.push_back(std::move(rule));
}

bool TechNonDefaultRuleStorage::destroyViaDefinition(TechViaMasterId id)
{
  if (!containsViaDefinition(id)) {
    return false;
  }

  const auto view = _registry.view<const TechNonDefaultRule, const TechNdrUseVias>();
  for (const auto entity : view) {
    const auto& values = view.get<const TechNdrUseVias>(entity).values;
    if (std::find(values.begin(), values.end(), id) != values.end()) {
      throw std::logic_error("cannot destroy an NDR VIA referenced by USEVIA");
    }
  }

  const auto owner = viaDefinitionOwner(id);
  auto& definitions = _registry.get<TechNdrViaDefinitions>(owner.entity()).values;
  const auto found = std::find(definitions.begin(), definitions.end(), id);
  if (found == definitions.end()) {
    throw std::logic_error("NDR VIA is absent from its owner");
  }
  definitions.erase(found);
  _registry.destroy(id.entity());
  return true;
}

bool TechNonDefaultRuleStorage::contains(TechNonDefaultRuleId id) const
{
  return _registry.valid(id.entity())
         && _registry.all_of<TechNonDefaultRule, TechNdrRoutingRules, TechNdrMinCutsRules, TechNdrUseVias, TechNdrUseViaRules,
                             TechNdrProperties, TechNdrViaDefinitions, TechNdrSameNetSpacingRules>(id.entity());
}

bool TechNonDefaultRuleStorage::containsViaDefinition(TechViaMasterId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechNdrViaDefinition, TechViaMaster, TechViaGeometry>(id.entity());
}

TechNonDefaultRuleId TechNonDefaultRuleStorage::findNonDefaultRuleById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  const auto result = TechNonDefaultRuleId{entity};
  return contains(result) ? result : TechNonDefaultRuleId{};
}

TechNonDefaultRuleId TechNonDefaultRuleStorage::findNonDefaultRule(std::string_view name) const
{
  const auto view = _registry.view<const TechNonDefaultRule>();
  for (const auto entity : view) {
    if (view.get<const TechNonDefaultRule>(entity).name == name) {
      return TechNonDefaultRuleId{entity};
    }
  }
  return {};
}

TechViaMasterId TechNonDefaultRuleStorage::findViaDefinitionById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  const auto result = TechViaMasterId{entity};
  return containsViaDefinition(result) ? result : TechViaMasterId{};
}

TechViaMasterId TechNonDefaultRuleStorage::findViaDefinition(std::string_view name) const
{
  const auto view = _registry.view<const TechNdrViaDefinition, const TechViaMaster, const TechViaGeometry>();
  for (const auto entity : view) {
    if (view.get<const TechViaMaster>(entity).name == name) {
      return TechViaMasterId{entity};
    }
  }
  return {};
}

const TechNonDefaultRule& TechNonDefaultRuleStorage::nonDefaultRule(TechNonDefaultRuleId id) const
{
  ensureRule(id);
  return _registry.get<TechNonDefaultRule>(id.entity());
}

const TechNdrRoutingRule* TechNonDefaultRuleStorage::routingRule(TechNonDefaultRuleId owner, TechRoutingLayerId layer) const
{
  const auto& values = routingRules(owner);
  const auto found = std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.layer == layer; });
  return found == values.end() ? nullptr : &*found;
}

const TechNdrMinCutsRule* TechNonDefaultRuleStorage::minCutsRule(TechNonDefaultRuleId owner, TechCutLayerId layer) const
{
  const auto& values = minCutsRules(owner);
  const auto found = std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.layer == layer; });
  return found == values.end() ? nullptr : &*found;
}

const TechViaMaster& TechNonDefaultRuleStorage::viaDefinition(TechViaMasterId id) const
{
  ensureViaDefinition(id);
  return _registry.get<TechViaMaster>(id.entity());
}

const TechViaGeometry& TechNonDefaultRuleStorage::viaDefinitionGeometry(TechViaMasterId id) const
{
  ensureViaDefinition(id);
  return _registry.get<TechViaGeometry>(id.entity());
}

const TechGeneratedViaMaster* TechNonDefaultRuleStorage::generatedViaDefinition(TechViaMasterId id) const
{
  ensureViaDefinition(id);
  return _registry.try_get<TechGeneratedViaMaster>(id.entity());
}

TechNonDefaultRuleId TechNonDefaultRuleStorage::viaDefinitionOwner(TechViaMasterId id) const
{
  ensureViaDefinition(id);
  const auto owner = _registry.get<TechNdrViaDefinition>(id.entity()).owner;
  ensureRule(owner);
  return owner;
}

const std::vector<TechNdrRoutingRule>& TechNonDefaultRuleStorage::routingRules(TechNonDefaultRuleId owner) const
{
  ensureRule(owner);
  return _registry.get<TechNdrRoutingRules>(owner.entity()).values;
}

const std::vector<TechNdrMinCutsRule>& TechNonDefaultRuleStorage::minCutsRules(TechNonDefaultRuleId owner) const
{
  ensureRule(owner);
  return _registry.get<TechNdrMinCutsRules>(owner.entity()).values;
}

const std::vector<TechViaMasterId>& TechNonDefaultRuleStorage::useVias(TechNonDefaultRuleId owner) const
{
  ensureRule(owner);
  return _registry.get<TechNdrUseVias>(owner.entity()).values;
}

const std::vector<TechViaRuleGenerateId>& TechNonDefaultRuleStorage::useViaRules(TechNonDefaultRuleId owner) const
{
  ensureRule(owner);
  return _registry.get<TechNdrUseViaRules>(owner.entity()).values;
}

const std::vector<TechNdrProperty>& TechNonDefaultRuleStorage::properties(TechNonDefaultRuleId owner) const
{
  ensureRule(owner);
  return _registry.get<TechNdrProperties>(owner.entity()).values;
}

const std::vector<TechViaMasterId>& TechNonDefaultRuleStorage::viaDefinitions(TechNonDefaultRuleId owner) const
{
  ensureRule(owner);
  return _registry.get<TechNdrViaDefinitions>(owner.entity()).values;
}

const std::vector<TechNdrSameNetSpacingRule>& TechNonDefaultRuleStorage::sameNetSpacingRules(TechNonDefaultRuleId owner) const
{
  ensureRule(owner);
  return _registry.get<TechNdrSameNetSpacingRules>(owner.entity()).values;
}

std::vector<TechNonDefaultRuleId> TechNonDefaultRuleStorage::nonDefaultRules() const
{
  std::vector<TechNonDefaultRuleId> result;
  const auto view = _registry.view<const TechNonDefaultRule>();
  result.reserve(view.size());
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

std::vector<TechNonDefaultRuleId> TechNonDefaultRuleStorage::nonDefaultRulesForRoutingLayer(TechRoutingLayerId layer) const
{
  std::vector<TechNonDefaultRuleId> result;
  const auto view = _registry.view<const TechNonDefaultRule, const TechNdrRoutingRules>();
  for (const auto entity : view) {
    const auto& values = view.get<const TechNdrRoutingRules>(entity).values;
    if (std::any_of(values.begin(), values.end(), [&](const auto& value) { return value.layer == layer; })) {
      result.emplace_back(entity);
    }
  }
  return result;
}

std::vector<TechNonDefaultRuleId> TechNonDefaultRuleStorage::nonDefaultRulesForCutLayer(TechCutLayerId layer) const
{
  std::vector<TechNonDefaultRuleId> result;
  const auto view = _registry.view<const TechNonDefaultRule, const TechNdrMinCutsRules>();
  for (const auto entity : view) {
    const auto& values = view.get<const TechNdrMinCutsRules>(entity).values;
    if (std::any_of(values.begin(), values.end(), [&](const auto& value) { return value.layer == layer; })) {
      result.emplace_back(entity);
    }
  }
  return result;
}

TechViaMasterId TechNonDefaultRuleStorage::createViaDefinition(TechNonDefaultRuleId owner, TechViaMaster master, TechViaGeometry geometry,
                                                               TechGeneratedViaMaster* generated)
{
  ensureRule(owner);
  const auto entity = _registry.create();
  try {
    _registry.emplace<TechNdrViaDefinition>(entity, TechNdrViaDefinition{.owner = owner});
    _registry.emplace<TechViaMaster>(entity, std::move(master));
    _registry.emplace<TechViaGeometry>(entity, std::move(geometry));
    if (generated != nullptr) {
      _registry.emplace<TechGeneratedViaMaster>(entity, std::move(*generated));
    }
    const auto id = TechViaMasterId{entity};
    _registry.get<TechNdrViaDefinitions>(owner.entity()).values.push_back(id);
    return id;
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
}

bool TechNonDefaultRuleStorage::isViaReferencedOutsideOwner(TechViaMasterId via, TechNonDefaultRuleId owner) const
{
  const auto view = _registry.view<const TechNonDefaultRule, const TechNdrUseVias>();
  for (const auto entity : view) {
    if (entity == owner.entity()) {
      continue;
    }
    const auto& values = view.get<const TechNdrUseVias>(entity).values;
    if (std::find(values.begin(), values.end(), via) != values.end()) {
      return true;
    }
  }
  return false;
}

void TechNonDefaultRuleStorage::ensureRule(TechNonDefaultRuleId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid nondefault rule id");
  }
}

void TechNonDefaultRuleStorage::ensureViaDefinition(TechViaMasterId id) const
{
  if (!containsViaDefinition(id)) {
    throw std::out_of_range("invalid NDR VIA definition id");
  }
}

void TechNonDefaultRuleStorage::validateRule(const TechNonDefaultRule& rule) const
{
  if (rule.name.empty()) {
    throw std::invalid_argument("nondefault rule name is required");
  }
  if (findNonDefaultRule(rule.name)) {
    throw std::invalid_argument("duplicate nondefault rule name");
  }
}

void TechNonDefaultRuleStorage::validateRoutingRule(TechNonDefaultRuleId owner, const TechNdrRoutingRule& rule) const
{
  ensureRule(owner);
  if (!_registry.valid(rule.layer.entity()) || !_registry.all_of<TechLayerInfo, TechRoutingLayer>(rule.layer.entity())) {
    throw std::invalid_argument("NDR routing rule requires a valid routing layer");
  }
  if (rule.width <= 0) {
    throw std::invalid_argument("NDR routing rule width is required");
  }
}

void TechNonDefaultRuleStorage::validateMinCutsRule(TechNonDefaultRuleId owner, const TechNdrMinCutsRule& rule) const
{
  ensureRule(owner);
  if (!_registry.valid(rule.layer.entity()) || !_registry.all_of<TechLayerInfo, TechCutLayer>(rule.layer.entity())) {
    throw std::invalid_argument("NDR min-cuts rule requires a valid cut layer");
  }
  if (rule.cut_count == 0u) {
    throw std::invalid_argument("NDR min-cuts count must be positive");
  }
}

void TechNonDefaultRuleStorage::validateUseVia(TechNonDefaultRuleId owner, TechViaMasterId via) const
{
  ensureRule(owner);
  if (!via || !_registry.valid(via.entity()) || !_registry.all_of<TechViaMaster, TechViaGeometry>(via.entity())) {
    throw std::invalid_argument("NDR use-via requires a valid VIA");
  }
}

void TechNonDefaultRuleStorage::validateUseViaRule(TechNonDefaultRuleId owner, TechViaRuleGenerateId via_rule) const
{
  ensureRule(owner);
  if (!via_rule || !_registry.valid(via_rule.entity())
      || !_registry.all_of<TechViaRuleGenerate, TechViaRuleGenerateBottomLayer, TechViaRuleGenerateCutLayer, TechViaRuleGenerateTopLayer>(
          via_rule.entity())) {
    throw std::invalid_argument("NDR use-via-rule requires a generated via rule");
  }
}

void TechNonDefaultRuleStorage::validateProperty(TechNonDefaultRuleId owner, const TechNdrProperty& property) const
{
  ensureRule(owner);
  if (property.name.empty()) {
    throw std::invalid_argument("NDR property name is required");
  }
}

void TechNonDefaultRuleStorage::validateViaDefinition(TechNonDefaultRuleId owner, const TechViaMaster& master,
                                                      const TechViaMasterShapeInput& shapes) const
{
  ensureRule(owner);
  if (master.name.empty()) {
    throw std::invalid_argument("NDR VIA name is required");
  }
  const auto vias = _registry.view<const TechViaMaster>();
  for (const auto entity : vias) {
    if (vias.get<const TechViaMaster>(entity).name == master.name) {
      throw std::invalid_argument("duplicate VIA name");
    }
  }
  if ((master.flags & TechViaMasterFlag::kHasResistance) != 0u && master.resistance < 0.0) {
    throw std::invalid_argument("NDR VIA resistance is invalid");
  }
  for (const auto& property : master.properties) {
    if (property.name.empty()) {
      throw std::invalid_argument("NDR VIA property name is required");
    }
  }

  const auto valid_conductor = [&](TechConductorLayerRef layer) {
    if (!layer || !_registry.valid(layer.entity)) {
      return false;
    }
    return (layer.kind == TechConductorLayerKind::kRouting && _registry.all_of<TechLayerInfo, TechRoutingLayer>(layer.entity))
           || (layer.kind == TechConductorLayerKind::kMasterslice && _registry.all_of<TechLayerInfo, TechMastersliceLayer>(layer.entity));
  };
  if (!valid_conductor(shapes.bottom_layer) || !valid_conductor(shapes.top_layer) || !_registry.valid(shapes.cut_layer.entity())
      || !_registry.all_of<TechLayerInfo, TechCutLayer>(shapes.cut_layer.entity())) {
    throw std::invalid_argument("NDR VIA requires valid conductor/cut/conductor layers");
  }
  if (shapes.bottom_geometry.empty() || shapes.cut_geometry.empty() || shapes.top_geometry.empty()) {
    throw std::invalid_argument("NDR VIA requires geometry on all three layers");
  }
}

void TechNonDefaultRuleStorage::validateSameNetSpacingRule(TechNonDefaultRuleId owner, const TechNdrSameNetSpacingRule& rule) const
{
  ensureRule(owner);
  if (!_registry.valid(rule.first_layer.entity()) || !_registry.all_of<TechLayerInfo>(rule.first_layer.entity())
      || !_registry.valid(rule.second_layer.entity()) || !_registry.all_of<TechLayerInfo>(rule.second_layer.entity())) {
    throw std::invalid_argument("NDR same-net spacing requires two valid layers");
  }
  if (rule.spacing <= 0) {
    throw std::invalid_argument("NDR same-net spacing must be positive");
  }
}

}  // namespace eccdb
