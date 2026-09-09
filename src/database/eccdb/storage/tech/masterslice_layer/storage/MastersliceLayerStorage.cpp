#include "tech/masterslice_layer/storage/MastersliceLayerStorage.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {

TechMastersliceLayerId TechMastersliceLayerStorage::createLayer(TechLayerInfo info, TechMastersliceLayer masterslice)
{
  validateInfo(info);
  const auto entity = _registry.create();
  try {
    _registry.emplace<TechLayerInfo>(entity, std::move(info));
    _registry.emplace<TechMastersliceLayer>(entity, masterslice);
    return TechMastersliceLayerId{entity};
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
}

bool TechMastersliceLayerStorage::contains(TechMastersliceLayerId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechLayerInfo, TechMastersliceLayer>(id.entity());
}

TechMastersliceLayerId TechMastersliceLayerStorage::findLayerById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechLayerInfo, TechMastersliceLayer>(entity) ? TechMastersliceLayerId{entity}
                                                                                                  : TechMastersliceLayerId{};
}

TechLayerInfo& TechMastersliceLayerStorage::layerInfo(TechMastersliceLayerId id)
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

const TechLayerInfo& TechMastersliceLayerStorage::layerInfo(TechMastersliceLayerId id) const
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

TechMastersliceLayer& TechMastersliceLayerStorage::mastersliceLayer(TechMastersliceLayerId id)
{
  ensureLayer(id);
  return _registry.get<TechMastersliceLayer>(id.entity());
}

const TechMastersliceLayer& TechMastersliceLayerStorage::mastersliceLayer(TechMastersliceLayerId id) const
{
  ensureLayer(id);
  return _registry.get<TechMastersliceLayer>(id.entity());
}

void TechMastersliceLayerStorage::setTrimmedMetalRule(TechMastersliceLayerId owner, TechTrimmedMetalRule rule)
{
  validateTrimmedMetalRule(owner, rule);
  _registry.emplace_or_replace<TechTrimmedMetalRule>(owner.entity(), std::move(rule));
}

void TechMastersliceLayerStorage::clearTrimmedMetalRule(TechMastersliceLayerId owner)
{
  ensureLayer(owner);
  _registry.remove<TechTrimmedMetalRule>(owner.entity());
}

bool TechMastersliceLayerStorage::hasTrimmedMetalRule(TechMastersliceLayerId owner) const
{
  return contains(owner) && _registry.all_of<TechTrimmedMetalRule>(owner.entity());
}

const TechTrimmedMetalRule& TechMastersliceLayerStorage::trimmedMetalRule(TechMastersliceLayerId owner) const
{
  if (!hasTrimmedMetalRule(owner)) {
    throw std::out_of_range("masterslice layer has no trimmed-metal rule");
  }
  return _registry.get<TechTrimmedMetalRule>(owner.entity());
}

void TechMastersliceLayerStorage::ensureLayer(TechMastersliceLayerId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid tech masterslice layer id");
  }
}

void TechMastersliceLayerStorage::validateInfo(const TechLayerInfo& info) const
{
  if (info.name.empty()) {
    throw std::invalid_argument("masterslice layer name is required");
  }
  if (info.mask_count == 0) {
    throw std::invalid_argument("masterslice layer mask count must be positive");
  }
}

void TechMastersliceLayerStorage::validateTrimmedMetalRule(TechMastersliceLayerId owner, const TechTrimmedMetalRule& rule) const
{
  ensureLayer(owner);
  if (mastersliceLayer(owner).subtype != TechMastersliceType::kTrimMetal) {
    throw std::invalid_argument("trimmed-metal rule requires a TRIMMETAL masterslice layer");
  }
  if ((rule.flags & ~TechTrimmedMetalRuleFlag::kHasMask) != 0u) {
    throw std::invalid_argument("trimmed-metal rule has unknown flags");
  }
  if (!_registry.valid(rule.metal_layer.entity()) || !_registry.all_of<TechLayerInfo, TechRoutingLayer>(rule.metal_layer.entity())) {
    throw std::invalid_argument("trimmed-metal rule requires a valid routing layer");
  }
  const bool has_mask = (rule.flags & TechTrimmedMetalRuleFlag::kHasMask) != 0u;
  if (!has_mask && rule.mask != 0u) {
    throw std::invalid_argument("trimmed-metal rule mask requires its presence flag");
  }
  if (has_mask && (rule.mask == 0u || rule.mask > _registry.get<TechLayerInfo>(rule.metal_layer.entity()).mask_count)) {
    throw std::invalid_argument("trimmed-metal rule mask is outside the target routing layer mask count");
  }
}

}  // namespace eccdb
