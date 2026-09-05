#include "tech/TechStore.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace eccdb {

TechStore::TechStore(TechStoreOptions options)
    : _geometry(options.geometry),
      _tech_root(_registry.registry().create()),
      _globals(_registry, _tech_root),
      _routing_layers(_registry),
      _cut_layers(_registry),
      _implant_layers(_registry),
      _masterslice_layers(_registry),
      _overlap_layers(_registry),
      _via_rule_generates(_registry),
      _via_rules(_registry, _tech_root),
      _non_default_rules(_registry, _geometry),
      _via_masters(_registry, _geometry)
{
  _registry.registry().emplace<TechRoot>(_tech_root.entity());
  _registry.registry().emplace<TechLayerSequence>(_tech_root.entity());
}

void TechStore::resetForBinaryLoad(GeometryPoolOptions options)
{
  _registry.registry() = TechRegistry::registry_type{};
  _geometry = GeometryPool(options);
  _tech_root = {};
  _globals.rebindRoot(_tech_root);
  _via_rules.rebindRoot(_tech_root);
}

void TechStore::bindBinaryLoadedRoot()
{
  auto view = _registry.registry().view<TechRoot>();
  auto current = view.begin();
  if (current == view.end()) {
    throw std::runtime_error("binary technology has no TechRoot");
  }
  const auto root = *current;
  if (++current != view.end()) {
    throw std::runtime_error("binary technology has multiple TechRoot entities");
  }
  _tech_root = TechRootId{root};
  _globals.rebindRoot(_tech_root);
  _via_rules.rebindRoot(_tech_root);
}

bool TechStore::contains(TechRootId id) const noexcept
{
  const auto& registry = _registry.registry();
  return registry.valid(id.entity()) && registry.all_of<TechRoot>(id.entity());
}

TechRoutingLayerId TechStore::createRoutingLayer(TechLayerInfo info, TechRoutingLayer routing)
{
  validateNewLayerInfo(info);
  const auto id = _routing_layers.createLayer(std::move(info), std::move(routing));
  try {
    appendLayer(TechLayerId{id.entity()});
  } catch (...) {
    _registry.registry().destroy(id.entity());
    throw;
  }
  return id;
}

TechCutLayerId TechStore::createCutLayer(TechLayerInfo info, TechCutLayer cut)
{
  validateNewLayerInfo(info);
  const auto id = _cut_layers.createLayer(std::move(info), std::move(cut));
  try {
    appendLayer(TechLayerId{id.entity()});
  } catch (...) {
    _registry.registry().destroy(id.entity());
    throw;
  }
  return id;
}

TechImplantLayerId TechStore::createImplantLayer(TechLayerInfo info, TechImplantLayer implant)
{
  validateNewLayerInfo(info);
  const auto id = _implant_layers.createLayer(std::move(info), std::move(implant));
  try {
    appendLayer(TechLayerId{id.entity()});
  } catch (...) {
    _registry.registry().destroy(id.entity());
    throw;
  }
  return id;
}

TechMastersliceLayerId TechStore::createMastersliceLayer(TechLayerInfo info, TechMastersliceLayer masterslice)
{
  validateNewLayerInfo(info);
  const auto id = _masterslice_layers.createLayer(std::move(info), std::move(masterslice));
  try {
    appendLayer(TechLayerId{id.entity()});
  } catch (...) {
    _registry.registry().destroy(id.entity());
    throw;
  }
  return id;
}

TechOverlapLayerId TechStore::createOverlapLayer(TechLayerInfo info)
{
  validateNewLayerInfo(info);
  return _overlap_layers.createLayer(std::move(info));
}

bool TechStore::contains(TechLayerId id) const
{
  return _registry.registry().valid(id.entity()) && _registry.registry().all_of<TechLayerInfo>(id.entity());
}

TechLayerId TechStore::findLayer(std::string_view name) const
{
  const auto view = _registry.registry().view<const TechLayerInfo>();
  for (const auto entity : view) {
    if (view.get<const TechLayerInfo>(entity).name == name) {
      return TechLayerId{entity};
    }
  }
  return {};
}

TechLayerInfo& TechStore::layerInfo(TechLayerId id)
{
  ensureLayer(id);
  return _registry.registry().get<TechLayerInfo>(id.entity());
}

const TechLayerInfo& TechStore::layerInfo(TechLayerId id) const
{
  ensureLayer(id);
  return _registry.registry().get<TechLayerInfo>(id.entity());
}

const std::vector<TechLayerId>& TechStore::layerSequence() const noexcept
{
  return _registry.registry().get<TechLayerSequence>(_tech_root.entity()).layers;
}

std::optional<uint32_t> TechStore::layerPosition(TechLayerId id) const noexcept
{
  if (!contains(id)) {
    return std::nullopt;
  }
  return techLayerPosition(_registry.registry().get<TechLayerSequence>(_tech_root.entity()), id.entity());
}

bool TechStore::isBelow(TechLayerId lower, TechLayerId upper) const noexcept
{
  return contains(lower) && contains(upper)
         && techLayerIsBelow(_registry.registry().get<TechLayerSequence>(_tech_root.entity()), lower.entity(), upper.entity());
}

std::optional<uint32_t> TechStore::routingLevel(TechRoutingLayerId layer) const noexcept
{
  const auto& registry = _registry.registry();
  if (!registry.valid(layer.entity()) || !registry.all_of<TechLayerInfo, TechRoutingLayer>(layer.entity())) {
    return std::nullopt;
  }

  uint32_t level = 0;
  for (const auto id : layerSequence()) {
    if (registry.all_of<TechRoutingLayer>(id.entity())) {
      ++level;
    }
    if (id.entity() == layer.entity()) {
      return level;
    }
  }
  return std::nullopt;
}

void TechStore::appendLayerProperty(TechLayerId owner, TechProperty property)
{
  ensureLayer(owner);
  if (property.name.empty()) {
    throw std::invalid_argument("tech layer property name is required");
  }
  _registry.registry().get_or_emplace<TechLayerProperties>(owner.entity()).values.push_back(std::move(property));
}

std::vector<TechProperty>& TechStore::layerProperties(TechLayerId owner)
{
  ensureLayer(owner);
  return _registry.registry().get_or_emplace<TechLayerProperties>(owner.entity()).values;
}

const std::vector<TechProperty>& TechStore::layerProperties(TechLayerId owner) const
{
  ensureLayer(owner);
  const auto* properties = _registry.registry().try_get<TechLayerProperties>(owner.entity());
  static const std::vector<TechProperty> empty;
  return properties == nullptr ? empty : properties->values;
}

void TechStore::validateNewLayerInfo(const TechLayerInfo& info) const
{
  if (info.name.empty()) {
    throw std::invalid_argument("tech layer name is required");
  }
  if (info.mask_count == 0) {
    throw std::invalid_argument("tech layer mask count must be positive");
  }
  if (findLayer(info.name)) {
    throw std::invalid_argument("duplicate tech layer name: " + info.name);
  }
}

void TechStore::appendLayer(TechLayerId id)
{
  ensureLayer(id);
  auto& sequence = _registry.registry().get<TechLayerSequence>(_tech_root.entity());
  if (!id || techLayerSequenceContains(sequence, id.entity())) {
    throw std::invalid_argument("tech layer sequence requires a new layer entity");
  }
  sequence.layers.push_back(id);
}

void TechStore::ensureLayer(TechLayerId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid tech layer id");
  }
}

}  // namespace eccdb
