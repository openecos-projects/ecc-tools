#include "tech/via_rule_generate/storage/ViaRuleGenerateStorage.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/masterslice_layer/model/MastersliceLayerComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {

TechViaRuleGenerateId ViaRuleGenerateStorage::createViaRuleGenerate(TechViaRuleGenerate rule, TechViaRuleGenerateBottomLayer bottom,
                                                                    TechViaRuleGenerateCutLayer cut, TechViaRuleGenerateTopLayer top)
{
  validateRule(rule);
  validateConductorLayer(bottom, "bottom");
  validateCutLayer(cut);
  validateConductorLayer(top, "top");

  if (rule.isDefault() && defaultViaRuleGenerateForCutLayer(cut.layer)) {
    throw std::invalid_argument("duplicate default via generate rule for cut layer");
  }

  const auto entity = _registry.create();
  try {
    _registry.emplace<TechViaRuleGenerate>(entity, std::move(rule));
    _registry.emplace<TechViaRuleGenerateBottomLayer>(entity, std::move(bottom));
    _registry.emplace<TechViaRuleGenerateCutLayer>(entity, std::move(cut));
    _registry.emplace<TechViaRuleGenerateTopLayer>(entity, std::move(top));
    return TechViaRuleGenerateId{entity};
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
}

bool ViaRuleGenerateStorage::contains(TechViaRuleGenerateId id) const
{
  return _registry.valid(id.entity())
         && _registry.all_of<TechViaRuleGenerate, TechViaRuleGenerateBottomLayer, TechViaRuleGenerateCutLayer, TechViaRuleGenerateTopLayer>(
             id.entity());
}

TechViaRuleGenerateId ViaRuleGenerateStorage::findViaRuleGenerateById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity)
                 && _registry.all_of<TechViaRuleGenerate, TechViaRuleGenerateBottomLayer, TechViaRuleGenerateCutLayer,
                                     TechViaRuleGenerateTopLayer>(entity)
             ? TechViaRuleGenerateId{entity}
             : TechViaRuleGenerateId{};
}

TechViaRuleGenerateId ViaRuleGenerateStorage::findViaRuleGenerate(std::string_view name) const
{
  const auto view = _registry.view<const TechViaRuleGenerate, const TechViaRuleGenerateBottomLayer, const TechViaRuleGenerateCutLayer,
                                   const TechViaRuleGenerateTopLayer>();
  for (const auto entity : view) {
    if (view.get<const TechViaRuleGenerate>(entity).name == name) {
      return TechViaRuleGenerateId{entity};
    }
  }
  return {};
}

std::vector<TechViaRuleGenerateId> ViaRuleGenerateStorage::viaRuleGenerates() const
{
  std::vector<TechViaRuleGenerateId> result;
  const auto view = _registry.view<const TechViaRuleGenerate, const TechViaRuleGenerateBottomLayer, const TechViaRuleGenerateCutLayer,
                                   const TechViaRuleGenerateTopLayer>();
  result.reserve(view.size_hint());
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

std::vector<TechViaRuleGenerateId> ViaRuleGenerateStorage::defaultViaRuleGenerates() const
{
  std::vector<TechViaRuleGenerateId> result;
  const auto view = _registry.view<const TechViaRuleGenerate, const TechViaRuleGenerateBottomLayer, const TechViaRuleGenerateCutLayer,
                                   const TechViaRuleGenerateTopLayer>();
  for (const auto entity : view) {
    if (view.get<const TechViaRuleGenerate>(entity).isDefault()) {
      result.emplace_back(entity);
    }
  }
  return result;
}

std::vector<TechViaRuleGenerateId> ViaRuleGenerateStorage::viaRuleGeneratesForCutLayer(TechCutLayerId cut_layer) const
{
  std::vector<TechViaRuleGenerateId> result;
  const auto view = _registry.view<const TechViaRuleGenerate, const TechViaRuleGenerateBottomLayer, const TechViaRuleGenerateCutLayer,
                                   const TechViaRuleGenerateTopLayer>();
  for (const auto entity : view) {
    if (view.get<const TechViaRuleGenerateCutLayer>(entity).layer == cut_layer) {
      result.emplace_back(entity);
    }
  }
  return result;
}

TechViaRuleGenerateId ViaRuleGenerateStorage::defaultViaRuleGenerateForCutLayer(TechCutLayerId cut_layer) const
{
  const auto view = _registry.view<const TechViaRuleGenerate, const TechViaRuleGenerateBottomLayer, const TechViaRuleGenerateCutLayer,
                                   const TechViaRuleGenerateTopLayer>();
  for (const auto entity : view) {
    const auto& rule = view.get<const TechViaRuleGenerate>(entity);
    const auto& cut = view.get<const TechViaRuleGenerateCutLayer>(entity);
    if (rule.isDefault() && cut.layer == cut_layer) {
      return TechViaRuleGenerateId{entity};
    }
  }
  return {};
}

std::vector<TechViaRuleGenerateId> ViaRuleGenerateStorage::viaRuleGeneratesForLayer(TechLayerId layer) const
{
  std::vector<TechViaRuleGenerateId> result;
  const auto view = _registry.view<const TechViaRuleGenerate, const TechViaRuleGenerateBottomLayer, const TechViaRuleGenerateCutLayer,
                                   const TechViaRuleGenerateTopLayer>();
  for (const auto entity : view) {
    if (view.get<const TechViaRuleGenerateBottomLayer>(entity).layer.entity == layer.entity()
        || view.get<const TechViaRuleGenerateCutLayer>(entity).layer.entity() == layer.entity()
        || view.get<const TechViaRuleGenerateTopLayer>(entity).layer.entity == layer.entity()) {
      result.emplace_back(entity);
    }
  }
  return result;
}

TechViaRuleGenerate& ViaRuleGenerateStorage::viaRuleGenerate(TechViaRuleGenerateId id)
{
  ensureRule(id);
  return _registry.get<TechViaRuleGenerate>(id.entity());
}

const TechViaRuleGenerate& ViaRuleGenerateStorage::viaRuleGenerate(TechViaRuleGenerateId id) const
{
  ensureRule(id);
  return _registry.get<TechViaRuleGenerate>(id.entity());
}

TechViaRuleGenerateBottomLayer& ViaRuleGenerateStorage::bottomLayer(TechViaRuleGenerateId id)
{
  ensureRule(id);
  return _registry.get<TechViaRuleGenerateBottomLayer>(id.entity());
}

const TechViaRuleGenerateBottomLayer& ViaRuleGenerateStorage::bottomLayer(TechViaRuleGenerateId id) const
{
  ensureRule(id);
  return _registry.get<TechViaRuleGenerateBottomLayer>(id.entity());
}

TechViaRuleGenerateCutLayer& ViaRuleGenerateStorage::cutLayer(TechViaRuleGenerateId id)
{
  ensureRule(id);
  return _registry.get<TechViaRuleGenerateCutLayer>(id.entity());
}

const TechViaRuleGenerateCutLayer& ViaRuleGenerateStorage::cutLayer(TechViaRuleGenerateId id) const
{
  ensureRule(id);
  return _registry.get<TechViaRuleGenerateCutLayer>(id.entity());
}

TechViaRuleGenerateTopLayer& ViaRuleGenerateStorage::topLayer(TechViaRuleGenerateId id)
{
  ensureRule(id);
  return _registry.get<TechViaRuleGenerateTopLayer>(id.entity());
}

const TechViaRuleGenerateTopLayer& ViaRuleGenerateStorage::topLayer(TechViaRuleGenerateId id) const
{
  ensureRule(id);
  return _registry.get<TechViaRuleGenerateTopLayer>(id.entity());
}

void ViaRuleGenerateStorage::ensureRule(TechViaRuleGenerateId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid tech via generate rule id");
  }
}

void ViaRuleGenerateStorage::validateRule(const TechViaRuleGenerate& rule) const
{
  if (rule.name.empty()) {
    throw std::invalid_argument("via generate rule name is required");
  }
  if (findViaRuleGenerate(rule.name)) {
    throw std::invalid_argument("duplicate via generate rule name");
  }
  for (const auto& property : rule.properties) {
    if (property.name.empty()) {
      throw std::invalid_argument("via generate rule property name is required");
    }
  }
}

void ViaRuleGenerateStorage::validateConductorLayer(const TechViaRuleGenerateBottomLayer& layer, const char* role) const
{
  if (!layer.layer || !_registry.valid(layer.layer.entity)
      || (layer.layer.kind == TechConductorLayerKind::kRouting
              ? !_registry.all_of<TechLayerInfo, TechRoutingLayer>(layer.layer.entity)
              : layer.layer.kind != TechConductorLayerKind::kMasterslice
                    || !_registry.all_of<TechLayerInfo, TechMastersliceLayer>(layer.layer.entity))) {
    throw std::invalid_argument(std::string{"via generate "} + role + " conductor layer is invalid");
  }
  if ((layer.flags & TechViaRuleGenerateRoutingLayerFlag::kHasWidth) != 0u && layer.min_width > layer.max_width) {
    throw std::invalid_argument(std::string{"via generate "} + role + " width range is invalid");
  }
  if ((layer.flags & TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure) != 0u
      && (layer.enclosure_overhang1 < 0 || layer.enclosure_overhang2 < 0)) {
    throw std::invalid_argument(std::string{"via generate "} + role + " enclosure is invalid");
  }
  if (((layer.flags & TechViaRuleGenerateRoutingLayerFlag::kHasOverhang) != 0u && layer.overhang < 0)
      || ((layer.flags & TechViaRuleGenerateRoutingLayerFlag::kHasMetalOverhang) != 0u && layer.metal_overhang < 0)) {
    throw std::invalid_argument(std::string{"via generate "} + role + " overhang is invalid");
  }
}

void ViaRuleGenerateStorage::validateConductorLayer(const TechViaRuleGenerateTopLayer& layer, const char* role) const
{
  if (!layer.layer || !_registry.valid(layer.layer.entity)
      || (layer.layer.kind == TechConductorLayerKind::kRouting
              ? !_registry.all_of<TechLayerInfo, TechRoutingLayer>(layer.layer.entity)
              : layer.layer.kind != TechConductorLayerKind::kMasterslice
                    || !_registry.all_of<TechLayerInfo, TechMastersliceLayer>(layer.layer.entity))) {
    throw std::invalid_argument(std::string{"via generate "} + role + " conductor layer is invalid");
  }
  if ((layer.flags & TechViaRuleGenerateRoutingLayerFlag::kHasWidth) != 0u && layer.min_width > layer.max_width) {
    throw std::invalid_argument(std::string{"via generate "} + role + " width range is invalid");
  }
  if ((layer.flags & TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure) != 0u
      && (layer.enclosure_overhang1 < 0 || layer.enclosure_overhang2 < 0)) {
    throw std::invalid_argument(std::string{"via generate "} + role + " enclosure is invalid");
  }
  if (((layer.flags & TechViaRuleGenerateRoutingLayerFlag::kHasOverhang) != 0u && layer.overhang < 0)
      || ((layer.flags & TechViaRuleGenerateRoutingLayerFlag::kHasMetalOverhang) != 0u && layer.metal_overhang < 0)) {
    throw std::invalid_argument(std::string{"via generate "} + role + " overhang is invalid");
  }
}

void ViaRuleGenerateStorage::validateCutLayer(const TechViaRuleGenerateCutLayer& layer) const
{
  if (!_registry.valid(layer.layer.entity()) || !_registry.all_of<TechLayerInfo, TechCutLayer>(layer.layer.entity())) {
    throw std::invalid_argument("via generate cut layer is invalid");
  }
  if ((layer.flags & TechViaRuleGenerateCutLayerFlag::kHasRect) != 0u && !layer.cut_rect.hasArea()) {
    throw std::invalid_argument("via generate cut rectangle is invalid");
  }
  if ((layer.flags & TechViaRuleGenerateCutLayerFlag::kHasSpacing) != 0u && (layer.spacing_x < 0 || layer.spacing_y < 0)) {
    throw std::invalid_argument("via generate cut spacing is invalid");
  }
  if ((layer.flags & TechViaRuleGenerateCutLayerFlag::kHasResistance) != 0u && layer.resistance_per_cut < 0.0) {
    throw std::invalid_argument("via generate cut resistance is invalid");
  }
}

}  // namespace eccdb
