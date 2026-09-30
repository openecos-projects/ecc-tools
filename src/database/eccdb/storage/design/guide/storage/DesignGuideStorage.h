#pragma once

#include <concepts>
#include <functional>
#include <span>
#include <vector>

#include "design/DesignRegistry.h"
#include "design/guide/model/GuideComponents.h"

namespace eccdb {

class TechRegistry;

// Net-owned routing guides and track-assignment results. Both are optional
// components so ordinary nets pay no storage cost when a router has not
// produced the corresponding data.
class DesignGuideStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;

  DesignGuideStorage(DesignRegistry& design_registry, const TechRegistry& tech_registry)
      : _registry(design_registry.registry()), _tech_registry(tech_registry)
  {
  }

  void replaceGuides(DesignNetId net, std::vector<DesignRouteGuide> guides);
  void clearGuides(DesignNetId net);
  [[nodiscard]] bool hasGuides(DesignNetId net) const;
  // The returned view is invalidated by a guide replacement or clear on this
  // net. It is empty when the optional component is absent.
  [[nodiscard]] std::span<const DesignRouteGuide> guides(DesignNetId net) const;

  template <typename Function>
    requires std::invocable<Function&, const DesignRouteGuide&>
  void forEachGuide(DesignNetId net, Function&& function) const
  {
    for (const auto& guide : guides(net)) {
      std::invoke(function, guide);
    }
  }

  void replaceTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> assignments);
  // Mirrors dbNetTrack::create(): values are appended in call order and are
  // intentionally not deduplicated.
  void appendTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> assignments);
  void clearTrackAssignments(DesignNetId net);
  [[nodiscard]] bool hasTrackAssignments(DesignNetId net) const;
  // The returned view is invalidated by a track-assignment replacement or
  // clear on this net. It is empty when the optional component is absent.
  [[nodiscard]] std::span<const DesignTrackAssignmentShape> trackAssignments(DesignNetId net) const;

  template <typename Function>
    requires std::invocable<Function&, const DesignTrackAssignmentShape&>
  void forEachTrackAssignment(DesignNetId net, Function&& function) const
  {
    for (const auto& assignment : trackAssignments(net)) {
      std::invoke(function, assignment);
    }
  }

  // Validates optional components after a direct binary registry restore.
  void validateRestoredState() const;

 private:
  void validateGuides(std::span<const DesignRouteGuide> guides) const;
  void validateTrackAssignments(std::span<const DesignTrackAssignmentShape> assignments) const;
  void validateRoutingLayer(TechRoutingLayerId layer) const;
  void ensureNet(DesignNetId net) const;

  registry_type& _registry;
  const TechRegistry& _tech_registry;
};

}  // namespace eccdb
