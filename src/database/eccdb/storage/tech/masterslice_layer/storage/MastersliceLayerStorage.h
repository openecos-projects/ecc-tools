#pragma once

#include <cstdint>

#include "tech/TechRegistry.h"
#include "tech/masterslice_layer/model/MastersliceLayerComponents.h"

namespace eccdb {

class TechMastersliceLayerStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  explicit TechMastersliceLayerStorage(TechRegistry& registry) : _registry(registry.registry()) {}

  [[nodiscard]] TechMastersliceLayerId createLayer(TechLayerInfo info, TechMastersliceLayer masterslice = {});
  [[nodiscard]] bool contains(TechMastersliceLayerId id) const;
  [[nodiscard]] TechMastersliceLayerId findLayerById(uint32_t id) const;

  [[nodiscard]] TechLayerInfo& layerInfo(TechMastersliceLayerId id);
  [[nodiscard]] const TechLayerInfo& layerInfo(TechMastersliceLayerId id) const;
  [[nodiscard]] TechMastersliceLayer& mastersliceLayer(TechMastersliceLayerId id);
  [[nodiscard]] const TechMastersliceLayer& mastersliceLayer(TechMastersliceLayerId id) const;

  void setTrimmedMetalRule(TechMastersliceLayerId owner, TechTrimmedMetalRule rule);
  void clearTrimmedMetalRule(TechMastersliceLayerId owner);
  [[nodiscard]] bool hasTrimmedMetalRule(TechMastersliceLayerId owner) const;
  [[nodiscard]] const TechTrimmedMetalRule& trimmedMetalRule(TechMastersliceLayerId owner) const;

 private:
  void ensureLayer(TechMastersliceLayerId id) const;
  void validateInfo(const TechLayerInfo& info) const;
  void validateTrimmedMetalRule(TechMastersliceLayerId owner, const TechTrimmedMetalRule& rule) const;

  registry_type& _registry;
};

}  // namespace eccdb
