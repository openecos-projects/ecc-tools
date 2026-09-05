#include "tech/implant_layer/storage/ImplantLayerStorage.h"

#include <stdexcept>
#include <utility>

namespace eccdb {

TechImplantLayerId TechImplantLayerStorage::createLayer(TechLayerInfo info, TechImplantLayer implant)
{
  validateInfo(info);
  validateLayer(implant);
  const auto entity = _registry.create();
  try {
    _registry.emplace<TechLayerInfo>(entity, std::move(info));
    _registry.emplace<TechImplantLayer>(entity, std::move(implant));
    _registry.emplace<TechImplantSpacingRules>(entity);
    return TechImplantLayerId{entity};
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
}

bool TechImplantLayerStorage::contains(TechImplantLayerId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechLayerInfo, TechImplantLayer, TechImplantSpacingRules>(id.entity());
}

TechImplantLayerId TechImplantLayerStorage::findLayerById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  const auto result = TechImplantLayerId{entity};
  return contains(result) ? result : TechImplantLayerId{};
}

TechLayerInfo& TechImplantLayerStorage::layerInfo(TechImplantLayerId id)
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

const TechLayerInfo& TechImplantLayerStorage::layerInfo(TechImplantLayerId id) const
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

TechImplantLayer& TechImplantLayerStorage::implantLayer(TechImplantLayerId id)
{
  ensureLayer(id);
  return _registry.get<TechImplantLayer>(id.entity());
}

const TechImplantLayer& TechImplantLayerStorage::implantLayer(TechImplantLayerId id) const
{
  ensureLayer(id);
  return _registry.get<TechImplantLayer>(id.entity());
}

void TechImplantLayerStorage::addSpacingRule(TechImplantLayerId owner, TechImplantSpacingRule rule)
{
  validateSpacingRule(owner, rule);
  _registry.get<TechImplantSpacingRules>(owner.entity()).values.push_back(std::move(rule));
}

const std::vector<TechImplantSpacingRule>& TechImplantLayerStorage::spacingRules(TechImplantLayerId owner) const
{
  ensureLayer(owner);
  return _registry.get<TechImplantSpacingRules>(owner.entity()).values;
}

void TechImplantLayerStorage::ensureLayer(TechImplantLayerId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid tech implant layer id");
  }
}

void TechImplantLayerStorage::validateInfo(const TechLayerInfo& info) const
{
  if (info.name.empty()) {
    throw std::invalid_argument("implant layer name is required");
  }
  if (info.mask_count == 0) {
    throw std::invalid_argument("implant layer mask count must be positive");
  }
}

void TechImplantLayerStorage::validateLayer(const TechImplantLayer& implant) const
{
  if ((implant.flags & ~TechImplantLayerFlag::kHasMinWidth) != 0u) {
    throw std::invalid_argument("implant layer has unknown flags");
  }
  const bool has_min_width = (implant.flags & TechImplantLayerFlag::kHasMinWidth) != 0u;
  if (has_min_width && implant.min_width <= 0) {
    throw std::invalid_argument("implant layer minimum width must be positive");
  }
  if (!has_min_width && implant.min_width != 0) {
    throw std::invalid_argument("implant layer minimum width requires its presence flag");
  }
}

void TechImplantLayerStorage::validateSpacingRule(TechImplantLayerId owner, const TechImplantSpacingRule& rule) const
{
  ensureLayer(owner);
  if (rule.min_spacing <= 0) {
    throw std::invalid_argument("implant layer minimum spacing must be positive");
  }
  if ((rule.flags & ~TechImplantSpacingRuleFlag::kHasOtherLayer) != 0u) {
    throw std::invalid_argument("implant spacing rule has unknown flags");
  }
  const bool has_other_layer = (rule.flags & TechImplantSpacingRuleFlag::kHasOtherLayer) != 0u;
  if (!has_other_layer) {
    if (rule.other_layer) {
      throw std::invalid_argument("implant spacing peer layer requires its presence flag");
    }
    return;
  }
  if (!rule.other_layer) {
    throw std::invalid_argument("implant spacing peer layer must not be null");
  }
  if (rule.other_layer == owner) {
    throw std::invalid_argument("implant spacing peer layer must differ from its owner");
  }
  if (!contains(rule.other_layer)) {
    throw std::invalid_argument("implant spacing peer must be an implant layer");
  }
}

}  // namespace eccdb
