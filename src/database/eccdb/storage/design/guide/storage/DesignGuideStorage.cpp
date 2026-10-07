#include "design/guide/storage/DesignGuideStorage.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "design/netlist/model/NetlistComponents.h"
#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {

void DesignGuideStorage::replaceGuides(DesignNetId net, std::vector<DesignRouteGuide> values)
{
  ensureNet(net);
  if (values.empty()) {
    clearGuides(net);
    return;
  }
  validateGuides(values);

  std::vector<GeometryShapeId> ids;
  ids.reserve(values.size());
  const bool had_relation = _registry.all_of<DesignNetRouteGuides>(net.entity());
  try {
    for (auto& value : values) {
      ids.push_back(_geometry_registry.create(
          designGuideOwner(net), GeometryShape{.layer = value.layer, .flags = value.flags}, std::move(value)));
    }

    auto& relation = _registry.get_or_emplace<DesignNetRouteGuides>(net.entity());
    auto replaced = relation.values;
    relation.values = std::move(ids);
    for (const auto id : replaced) {
      static_cast<void>(_geometry_registry.destroy(id));
    }
    _design_registry.markGeometryChanged();
  } catch (...) {
    for (const auto id : ids) {
      static_cast<void>(_geometry_registry.destroy(id));
    }
    if (!had_relation && _registry.all_of<DesignNetRouteGuides>(net.entity())) {
      const auto& relation = _registry.get<const DesignNetRouteGuides>(net.entity());
      if (relation.values.empty()) {
        _registry.remove<DesignNetRouteGuides>(net.entity());
      }
    }
    throw;
  }
}

void DesignGuideStorage::clearGuides(DesignNetId net)
{
  ensureNet(net);
  auto* relation = _registry.try_get<DesignNetRouteGuides>(net.entity());
  if (relation == nullptr) {
    return;
  }
  auto ids = std::move(relation->values);
  _registry.remove<DesignNetRouteGuides>(net.entity());
  for (const auto id : ids) {
    static_cast<void>(_geometry_registry.destroy(id));
  }
  _design_registry.markGeometryChanged();
}

bool DesignGuideStorage::hasGuides(DesignNetId net) const
{
  ensureNet(net);
  return _registry.all_of<DesignNetRouteGuides>(net.entity());
}

std::span<const GeometryShapeId> DesignGuideStorage::guides(DesignNetId net) const
{
  ensureNet(net);
  const auto* component = _registry.try_get<const DesignNetRouteGuides>(net.entity());
  return component == nullptr ? std::span<const GeometryShapeId>{} : std::span<const GeometryShapeId>{component->values};
}

const DesignRouteGuide& DesignGuideStorage::guide(GeometryShapeId id) const
{
  return _geometry_registry.get<DesignRouteGuide>(id);
}

void DesignGuideStorage::replaceTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> values)
{
  ensureNet(net);
  if (values.empty()) {
    clearTrackAssignments(net);
    return;
  }
  validateTrackAssignments(values);

  std::vector<GeometryShapeId> ids;
  ids.reserve(values.size());
  const bool had_relation = _registry.all_of<DesignNetTrackAssignments>(net.entity());
  try {
    for (auto& value : values) {
      ids.push_back(_geometry_registry.create(designTrackAssignmentOwner(net),
                                              GeometryShape{.layer = value.layer}, std::move(value)));
    }

    auto& relation = _registry.get_or_emplace<DesignNetTrackAssignments>(net.entity());
    auto replaced = relation.values;
    relation.values = std::move(ids);
    for (const auto id : replaced) {
      static_cast<void>(_geometry_registry.destroy(id));
    }
    _design_registry.markGeometryChanged();
  } catch (...) {
    for (const auto id : ids) {
      static_cast<void>(_geometry_registry.destroy(id));
    }
    if (!had_relation && _registry.all_of<DesignNetTrackAssignments>(net.entity())) {
      const auto& relation = _registry.get<const DesignNetTrackAssignments>(net.entity());
      if (relation.values.empty()) {
        _registry.remove<DesignNetTrackAssignments>(net.entity());
      }
    }
    throw;
  }
}

void DesignGuideStorage::appendTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> values)
{
  ensureNet(net);
  if (values.empty()) {
    return;
  }
  validateTrackAssignments(values);

  std::vector<GeometryShapeId> ids;
  ids.reserve(values.size());
  const bool had_relation = _registry.all_of<DesignNetTrackAssignments>(net.entity());
  try {
    for (auto& value : values) {
      ids.push_back(_geometry_registry.create(designTrackAssignmentOwner(net),
                                              GeometryShape{.layer = value.layer}, std::move(value)));
    }

    auto& relation = _registry.get_or_emplace<DesignNetTrackAssignments>(net.entity());
    relation.values.reserve(relation.values.size() + ids.size());
    relation.values.insert(relation.values.end(), ids.begin(), ids.end());
    _design_registry.markGeometryChanged();
  } catch (...) {
    if (auto* relation = _registry.try_get<DesignNetTrackAssignments>(net.entity()); relation != nullptr) {
      for (const auto id : ids) {
        std::erase(relation->values, id);
      }
    }
    for (const auto id : ids) {
      static_cast<void>(_geometry_registry.destroy(id));
    }
    if (!had_relation && _registry.all_of<DesignNetTrackAssignments>(net.entity())) {
      const auto& relation = _registry.get<const DesignNetTrackAssignments>(net.entity());
      if (relation.values.empty()) {
        _registry.remove<DesignNetTrackAssignments>(net.entity());
      }
    }
    throw;
  }
}

void DesignGuideStorage::clearTrackAssignments(DesignNetId net)
{
  ensureNet(net);
  auto* relation = _registry.try_get<DesignNetTrackAssignments>(net.entity());
  if (relation == nullptr) {
    return;
  }
  auto ids = std::move(relation->values);
  _registry.remove<DesignNetTrackAssignments>(net.entity());
  for (const auto id : ids) {
    static_cast<void>(_geometry_registry.destroy(id));
  }
  _design_registry.markGeometryChanged();
}

bool DesignGuideStorage::hasTrackAssignments(DesignNetId net) const
{
  ensureNet(net);
  return _registry.all_of<DesignNetTrackAssignments>(net.entity());
}

std::span<const GeometryShapeId> DesignGuideStorage::trackAssignments(DesignNetId net) const
{
  ensureNet(net);
  const auto* component = _registry.try_get<const DesignNetTrackAssignments>(net.entity());
  return component == nullptr ? std::span<const GeometryShapeId>{} : std::span<const GeometryShapeId>{component->values};
}

const DesignTrackAssignmentShape& DesignGuideStorage::trackAssignment(GeometryShapeId id) const
{
  return _geometry_registry.get<DesignTrackAssignmentShape>(id);
}

void DesignGuideStorage::validateRestoredState() const
{
  const auto guide_view = _registry.view<const DesignNetRouteGuides>();
  for (const auto entity : guide_view) {
    try {
      const auto net = DesignNetId{entity};
      ensureNet(net);
      validateGuideIds(net, guide_view.get<const DesignNetRouteGuides>(entity).values);
    } catch (const std::exception& error) {
      throw std::runtime_error(std::string{"binary design has invalid net route guides: "} + error.what());
    }
  }

  const auto track_view = _registry.view<const DesignNetTrackAssignments>();
  for (const auto entity : track_view) {
    try {
      const auto net = DesignNetId{entity};
      ensureNet(net);
      validateTrackAssignmentIds(net, track_view.get<const DesignNetTrackAssignments>(entity).values);
    } catch (const std::exception& error) {
      throw std::runtime_error(std::string{"binary design has invalid net track assignments: "} + error.what());
    }
  }
}

void DesignGuideStorage::validateGuides(std::span<const DesignRouteGuide> values) const
{
  constexpr uint32_t kKnownFlags = DesignRouteGuideFlag::kCongested | DesignRouteGuideFlag::kJumper
                                   | DesignRouteGuideFlag::kConnectedToTerm;
  if (values.empty()) {
    throw std::invalid_argument("net route guide component is empty");
  }
  for (const auto& value : values) {
    if (!value.box.isValid() || !value.box.hasArea() || (value.flags & ~kKnownFlags) != 0u) {
      throw std::invalid_argument("net route guide geometry or flags are invalid");
    }
    validateRoutingLayer(value.layer);
    if (value.via_layer) {
      validateRoutingLayer(value.via_layer);
    }
  }
}

void DesignGuideStorage::validateTrackAssignments(std::span<const DesignTrackAssignmentShape> values) const
{
  if (values.empty()) {
    throw std::invalid_argument("net track-assignment component is empty");
  }
  for (const auto& value : values) {
    if (!value.box.isValid()) {
      throw std::invalid_argument("net track-assignment geometry is invalid");
    }
    validateRoutingLayer(value.layer);
  }
}

void DesignGuideStorage::validateGuideIds(DesignNetId net, std::span<const GeometryShapeId> ids) const
{
  if (ids.empty()) {
    throw std::invalid_argument("net route guide component is empty");
  }
  const auto owner = designGuideOwner(net);
  for (const auto id : ids) {
    if (!_geometry_registry.contains(id)) {
      throw std::invalid_argument("net route guide references missing geometry");
    }
    const auto& metadata = _geometry_registry.shape(id);
    if (metadata.owner.domain != owner.domain || metadata.owner.value != owner.value || metadata.kind != GeometryShapeKind::kGuide) {
      throw std::invalid_argument("net route guide geometry owner is inconsistent");
    }
    const auto* value = _geometry_registry.tryGet<DesignRouteGuide>(id);
    if (value == nullptr || metadata.layer != value->layer || metadata.flags != value->flags) {
      throw std::invalid_argument("net route guide geometry payload is inconsistent");
    }
    validateGuides(std::span<const DesignRouteGuide>{value, 1u});
  }
}

void DesignGuideStorage::validateTrackAssignmentIds(DesignNetId net, std::span<const GeometryShapeId> ids) const
{
  if (ids.empty()) {
    throw std::invalid_argument("net track-assignment component is empty");
  }
  const auto owner = designTrackAssignmentOwner(net);
  for (const auto id : ids) {
    if (!_geometry_registry.contains(id)) {
      throw std::invalid_argument("net track-assignment references missing geometry");
    }
    const auto& metadata = _geometry_registry.shape(id);
    if (metadata.owner.domain != owner.domain || metadata.owner.value != owner.value
        || metadata.kind != GeometryShapeKind::kTrackAssignment) {
      throw std::invalid_argument("net track-assignment geometry owner is inconsistent");
    }
    const auto* value = _geometry_registry.tryGet<DesignTrackAssignmentShape>(id);
    if (value == nullptr || metadata.layer != value->layer || metadata.flags != 0u) {
      throw std::invalid_argument("net track-assignment geometry payload is inconsistent");
    }
    validateTrackAssignments(std::span<const DesignTrackAssignmentShape>{value, 1u});
  }
}

void DesignGuideStorage::validateRoutingLayer(TechRoutingLayerId layer) const
{
  const auto& technology = _tech_registry.registry();
  if (!layer || !technology.valid(layer.entity()) || !technology.all_of<TechLayerInfo, TechRoutingLayer>(layer.entity())) {
    throw std::invalid_argument("net guide references an invalid routing layer");
  }
}

void DesignGuideStorage::ensureNet(DesignNetId net) const
{
  if (!net || !_registry.valid(net.entity()) || !_registry.all_of<DesignNet>(net.entity())) {
    throw std::out_of_range("invalid design net id");
  }
}

}  // namespace eccdb
