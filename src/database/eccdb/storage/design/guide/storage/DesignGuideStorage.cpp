#include "design/guide/storage/DesignGuideStorage.h"

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
  _registry.emplace_or_replace<DesignNetRouteGuides>(net.entity(), DesignNetRouteGuides{.values = std::move(values)});
}

void DesignGuideStorage::clearGuides(DesignNetId net)
{
  ensureNet(net);
  _registry.remove<DesignNetRouteGuides>(net.entity());
}

bool DesignGuideStorage::hasGuides(DesignNetId net) const
{
  ensureNet(net);
  return _registry.all_of<DesignNetRouteGuides>(net.entity());
}

std::span<const DesignRouteGuide> DesignGuideStorage::guides(DesignNetId net) const
{
  ensureNet(net);
  const auto* component = _registry.try_get<const DesignNetRouteGuides>(net.entity());
  return component == nullptr ? std::span<const DesignRouteGuide>{} : std::span<const DesignRouteGuide>{component->values};
}

void DesignGuideStorage::replaceTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> values)
{
  ensureNet(net);
  if (values.empty()) {
    clearTrackAssignments(net);
    return;
  }
  validateTrackAssignments(values);
  _registry.emplace_or_replace<DesignNetTrackAssignments>(net.entity(),
                                                           DesignNetTrackAssignments{.values = std::move(values)});
}

void DesignGuideStorage::appendTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> values)
{
  ensureNet(net);
  if (values.empty()) {
    return;
  }
  validateTrackAssignments(values);

  // Build and validate the complete replacement before touching the optional
  // component. This preserves the existing assignments if allocation or
  // validation fails while appending a Writer batch.
  DesignNetTrackAssignments replacement;
  if (const auto* existing = _registry.try_get<const DesignNetTrackAssignments>(net.entity()); existing != nullptr) {
    replacement = *existing;
  }
  replacement.values.reserve(replacement.values.size() + values.size());
  for (auto& value : values) {
    replacement.values.push_back(std::move(value));
  }
  validateTrackAssignments(replacement.values);
  _registry.emplace_or_replace<DesignNetTrackAssignments>(net.entity(), std::move(replacement));
}

void DesignGuideStorage::clearTrackAssignments(DesignNetId net)
{
  ensureNet(net);
  _registry.remove<DesignNetTrackAssignments>(net.entity());
}

bool DesignGuideStorage::hasTrackAssignments(DesignNetId net) const
{
  ensureNet(net);
  return _registry.all_of<DesignNetTrackAssignments>(net.entity());
}

std::span<const DesignTrackAssignmentShape> DesignGuideStorage::trackAssignments(DesignNetId net) const
{
  ensureNet(net);
  const auto* component = _registry.try_get<const DesignNetTrackAssignments>(net.entity());
  return component == nullptr ? std::span<const DesignTrackAssignmentShape>{}
                              : std::span<const DesignTrackAssignmentShape>{component->values};
}

void DesignGuideStorage::validateRestoredState() const
{
  const auto guide_view = _registry.view<const DesignNetRouteGuides>();
  for (const auto entity : guide_view) {
    try {
      ensureNet(DesignNetId{entity});
      validateGuides(guide_view.get<const DesignNetRouteGuides>(entity).values);
    } catch (const std::exception& error) {
      throw std::runtime_error(std::string{"binary design has invalid net route guides: "} + error.what());
    }
  }

  const auto track_view = _registry.view<const DesignNetTrackAssignments>();
  for (const auto entity : track_view) {
    try {
      ensureNet(DesignNetId{entity});
      validateTrackAssignments(track_view.get<const DesignNetTrackAssignments>(entity).values);
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
  for (const auto& guide : values) {
    if (!guide.box.isValid() || !guide.box.hasArea() || (guide.flags & ~kKnownFlags) != 0u) {
      throw std::invalid_argument("net route guide geometry or flags are invalid");
    }
    validateRoutingLayer(guide.layer);
    if (guide.via_layer) {
      validateRoutingLayer(guide.via_layer);
    }
  }
}

void DesignGuideStorage::validateTrackAssignments(std::span<const DesignTrackAssignmentShape> values) const
{
  if (values.empty()) {
    throw std::invalid_argument("net track-assignment component is empty");
  }
  for (const auto& assignment : values) {
    if (!assignment.box.isValid()) {
      throw std::invalid_argument("net track-assignment geometry is invalid");
    }
    validateRoutingLayer(assignment.layer);
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
