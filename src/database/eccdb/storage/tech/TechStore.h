#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "geometry/GeometryPool.h"
#include "tech/TechRegistry.h"
#include "tech/cut_layer/storage/CutLayerStorage.h"
#include "tech/global/storage/TechGlobalStorage.h"
#include "tech/implant_layer/storage/ImplantLayerStorage.h"
#include "tech/masterslice_layer/storage/MastersliceLayerStorage.h"
#include "tech/non_default_rule/storage/NonDefaultRuleStorage.h"
#include "tech/overlap_layer/storage/OverlapLayerStorage.h"
#include "tech/routing_layer/storage/RoutingLayerStorage.h"
#include "tech/via_master/storage/ViaMasterStorage.h"
#include "tech/via_rule/storage/ViaRuleStorage.h"
#include "tech/via_rule_generate/storage/ViaRuleGenerateStorage.h"

namespace eccdb {

class BinaryDatabaseImporter;

struct TechStoreOptions
{
  GeometryPoolOptions geometry;
};

// The EnTT-first technology root. Every complete Tech object is allocated in
// this one registry. Layer type is expressed by components, not by a catalog
// enum or a second ID space.
class TechStore
{
 public:
  explicit TechStore(TechStoreOptions options = {});

  [[nodiscard]] TechRegistry& techRegistry() noexcept { return _registry; }
  [[nodiscard]] const TechRegistry& techRegistry() const noexcept { return _registry; }
  [[nodiscard]] GeometryPool& geometryPool() noexcept { return _geometry; }
  [[nodiscard]] const GeometryPool& geometryPool() const noexcept { return _geometry; }

  // TechRoot owns singleton LEF statements through TechGlobalStorage.
  [[nodiscard]] TechRootId techRootId() const noexcept { return _tech_root; }
  [[nodiscard]] bool contains(TechRootId id) const noexcept;

  // These constructors enforce one Tech-wide layer-name namespace. Physical
  // layers append to the canonical bottom-to-top process sequence; OVERLAP is
  // a placement abstraction and deliberately stays outside that sequence.
  [[nodiscard]] TechRoutingLayerId createRoutingLayer(TechLayerInfo info, TechRoutingLayer routing = {});
  [[nodiscard]] TechCutLayerId createCutLayer(TechLayerInfo info, TechCutLayer cut = {});
  [[nodiscard]] TechImplantLayerId createImplantLayer(TechLayerInfo info, TechImplantLayer implant = {});
  [[nodiscard]] TechMastersliceLayerId createMastersliceLayer(TechLayerInfo info, TechMastersliceLayer masterslice = {});
  [[nodiscard]] TechOverlapLayerId createOverlapLayer(TechLayerInfo info);

  [[nodiscard]] bool contains(TechLayerId id) const;
  [[nodiscard]] TechLayerId findLayer(std::string_view name) const;
  [[nodiscard]] TechLayerInfo& layerInfo(TechLayerId id);
  [[nodiscard]] const TechLayerInfo& layerInfo(TechLayerId id) const;
  [[nodiscard]] const std::vector<TechLayerId>& layerSequence() const noexcept;
  [[nodiscard]] std::optional<uint32_t> layerPosition(TechLayerId id) const noexcept;
  [[nodiscard]] bool isBelow(TechLayerId lower, TechLayerId upper) const noexcept;
  [[nodiscard]] std::optional<uint32_t> routingLevel(TechRoutingLayerId layer) const noexcept;

  void appendLayerProperty(TechLayerId owner, TechProperty property);
  [[nodiscard]] std::vector<TechProperty>& layerProperties(TechLayerId owner);
  [[nodiscard]] const std::vector<TechProperty>& layerProperties(TechLayerId owner) const;

  [[nodiscard]] TechRoutingLayerStorage& routingLayerStorage() noexcept { return _routing_layers; }
  [[nodiscard]] const TechRoutingLayerStorage& routingLayerStorage() const noexcept { return _routing_layers; }
  [[nodiscard]] TechCutLayerStorage& cutLayerStorage() noexcept { return _cut_layers; }
  [[nodiscard]] const TechCutLayerStorage& cutLayerStorage() const noexcept { return _cut_layers; }
  [[nodiscard]] TechImplantLayerStorage& implantLayerStorage() noexcept { return _implant_layers; }
  [[nodiscard]] const TechImplantLayerStorage& implantLayerStorage() const noexcept { return _implant_layers; }
  [[nodiscard]] TechMastersliceLayerStorage& mastersliceLayerStorage() noexcept { return _masterslice_layers; }
  [[nodiscard]] const TechMastersliceLayerStorage& mastersliceLayerStorage() const noexcept { return _masterslice_layers; }
  [[nodiscard]] TechOverlapLayerStorage& overlapLayerStorage() noexcept { return _overlap_layers; }
  [[nodiscard]] const TechOverlapLayerStorage& overlapLayerStorage() const noexcept { return _overlap_layers; }
  [[nodiscard]] TechGlobalStorage& globalStorage() noexcept { return _globals; }
  [[nodiscard]] const TechGlobalStorage& globalStorage() const noexcept { return _globals; }

  [[nodiscard]] ViaRuleGenerateStorage& viaRuleGenerateStorage() noexcept { return _via_rule_generates; }
  [[nodiscard]] const ViaRuleGenerateStorage& viaRuleGenerateStorage() const noexcept { return _via_rule_generates; }
  [[nodiscard]] ViaRuleStorage& viaRuleStorage() noexcept { return _via_rules; }
  [[nodiscard]] const ViaRuleStorage& viaRuleStorage() const noexcept { return _via_rules; }
  [[nodiscard]] TechNonDefaultRuleStorage& nonDefaultRuleStorage() noexcept { return _non_default_rules; }
  [[nodiscard]] const TechNonDefaultRuleStorage& nonDefaultRuleStorage() const noexcept { return _non_default_rules; }
  [[nodiscard]] TechViaMasterStorage& viaMasterStorage() noexcept { return _via_masters; }
  [[nodiscard]] const TechViaMasterStorage& viaMasterStorage() const noexcept { return _via_masters; }

 private:
  friend class BinaryDatabaseImporter;

  void resetForBinaryLoad(GeometryPoolOptions options);
  void bindBinaryLoadedRoot();
  void validateNewLayerInfo(const TechLayerInfo& info) const;
  void appendLayer(TechLayerId id);
  void ensureLayer(TechLayerId id) const;

  TechRegistry _registry;
  GeometryPool _geometry;
  TechRootId _tech_root;
  TechGlobalStorage _globals;
  TechRoutingLayerStorage _routing_layers;
  TechCutLayerStorage _cut_layers;
  TechImplantLayerStorage _implant_layers;
  TechMastersliceLayerStorage _masterslice_layers;
  TechOverlapLayerStorage _overlap_layers;
  ViaRuleGenerateStorage _via_rule_generates;
  ViaRuleStorage _via_rules;
  TechNonDefaultRuleStorage _non_default_rules;
  TechViaMasterStorage _via_masters;
};

}  // namespace eccdb
