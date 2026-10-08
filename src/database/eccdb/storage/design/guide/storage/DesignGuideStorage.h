// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#pragma once

#include <concepts>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "design/DesignRegistry.h"
#include "design/guide/model/GuideComponents.h"
#include "geometry/GeometryRegistry.h"

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
      : _design_registry(design_registry),
        _registry(design_registry.registry()),
        _geometry_registry(design_registry.geometryRegistry()),
        _tech_registry(tech_registry)
  {
  }

  void replaceGuides(DesignNetId net, std::vector<DesignRouteGuide> guides);
  // Stages both collections before publishing either one. Absent guides keep
  // the existing guide collection; assignments are the complete stage result.
  void replaceGuideStage(DesignNetId net,
                         std::optional<std::vector<DesignRouteGuide>> guides,
                         std::vector<DesignTrackAssignmentShape> assignments);
  void clearGuides(DesignNetId net);
  [[nodiscard]] bool hasGuides(DesignNetId net) const;
  // The returned view is invalidated by a guide replacement or clear on this
  // net. It is empty when the optional component is absent.
  [[nodiscard]] std::span<const GeometryShapeId> guides(DesignNetId net) const;
  [[nodiscard]] const DesignRouteGuide& guide(GeometryShapeId id) const;

  template <typename Function>
    requires std::invocable<Function&, const DesignRouteGuide&>
  void forEachGuide(DesignNetId net, Function&& function) const
  {
    for (const auto id : guides(net)) {
      std::invoke(function, guide(id));
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
  [[nodiscard]] std::span<const GeometryShapeId> trackAssignments(DesignNetId net) const;
  [[nodiscard]] const DesignTrackAssignmentShape& trackAssignment(GeometryShapeId id) const;

  template <typename Function>
    requires std::invocable<Function&, const DesignTrackAssignmentShape&>
  void forEachTrackAssignment(DesignNetId net, Function&& function) const
  {
    for (const auto id : trackAssignments(net)) {
      std::invoke(function, trackAssignment(id));
    }
  }

  // Validates optional components after a direct binary registry restore.
  void validateRestoredState() const;

 private:
  void validateGuides(std::span<const DesignRouteGuide> guides) const;
  void validateTrackAssignments(std::span<const DesignTrackAssignmentShape> assignments) const;
  void validateGuideIds(DesignNetId net, std::span<const GeometryShapeId> guides) const;
  void validateTrackAssignmentIds(DesignNetId net, std::span<const GeometryShapeId> assignments) const;
  void validateRoutingLayer(TechRoutingLayerId layer) const;
  void ensureNet(DesignNetId net) const;

  DesignRegistry& _design_registry;
  registry_type& _registry;
  GeometryRegistry& _geometry_registry;
  const TechRegistry& _tech_registry;
};

}  // namespace eccdb
