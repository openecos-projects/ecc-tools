#pragma once

#include <cstdint>

#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/overlap_layer/model/OverlapLayerComponents.h"

namespace eccdb {

class TechOverlapLayerStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  explicit TechOverlapLayerStorage(TechRegistry& registry) : _registry(registry.registry()) {}

  [[nodiscard]] TechOverlapLayerId createLayer(TechLayerInfo info);
  [[nodiscard]] bool contains(TechOverlapLayerId id) const;
  [[nodiscard]] TechOverlapLayerId findLayerById(uint32_t id) const;
  [[nodiscard]] TechLayerInfo& layerInfo(TechOverlapLayerId id);
  [[nodiscard]] const TechLayerInfo& layerInfo(TechOverlapLayerId id) const;

 private:
  void ensureLayer(TechOverlapLayerId id) const;
  void validateInfo(const TechLayerInfo& info) const;

  registry_type& _registry;
};

}  // namespace eccdb
