#include "tech/overlap_layer/storage/OverlapLayerStorage.h"

#include <stdexcept>
#include <utility>

namespace eccdb {

TechOverlapLayerId TechOverlapLayerStorage::createLayer(TechLayerInfo info)
{
  validateInfo(info);
  const auto entity = _registry.create();
  try {
    _registry.emplace<TechLayerInfo>(entity, std::move(info));
    _registry.emplace<TechOverlapLayer>(entity);
    return TechOverlapLayerId{entity};
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
}

bool TechOverlapLayerStorage::contains(TechOverlapLayerId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechLayerInfo, TechOverlapLayer>(id.entity());
}

TechOverlapLayerId TechOverlapLayerStorage::findLayerById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechLayerInfo, TechOverlapLayer>(entity) ? TechOverlapLayerId{entity}
                                                                                              : TechOverlapLayerId{};
}

TechLayerInfo& TechOverlapLayerStorage::layerInfo(TechOverlapLayerId id)
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

const TechLayerInfo& TechOverlapLayerStorage::layerInfo(TechOverlapLayerId id) const
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

void TechOverlapLayerStorage::ensureLayer(TechOverlapLayerId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid tech overlap layer id");
  }
}

void TechOverlapLayerStorage::validateInfo(const TechLayerInfo& info) const
{
  if (info.name.empty()) {
    throw std::invalid_argument("overlap layer name is required");
  }
  if (info.mask_count == 0) {
    throw std::invalid_argument("overlap layer mask count must be positive");
  }
}

}  // namespace eccdb
