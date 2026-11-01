// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "geometry/GeometryEntity.h"
#include "tech/common/TechLayerIds.h"

namespace eccdb {

namespace DesignRouteGuideFlag {
constexpr uint32_t kCongested = 1u << 0;
constexpr uint32_t kJumper = 1u << 1;
constexpr uint32_t kConnectedToTerm = 1u << 2;
}  // namespace DesignRouteGuideFlag

// One ODB dbGuide-like global-routing region. A null via_layer means the
// guide does not carry an explicit layer transition target.
struct DesignRouteGuide
{
  Rect box;
  TechRoutingLayerId layer;
  TechRoutingLayerId via_layer;
  uint32_t flags = 0;
};

template <>
struct GeometryPayloadTraits<DesignRouteGuide>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kGuide;
};

[[nodiscard]] inline GeometryOwner designGuideOwner(DesignNetId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignGuide, id);
}

// Optional ordered owner relation. Each guide has an independent geometry
// entity; TritonRoute still replaces the complete net-local relation at once.
struct DesignNetRouteGuides
{
  std::vector<GeometryShapeId> values;
};

// One ODB dbNetTrack-like result from track assignment. Unlike guides, a
// track can be represented by a horizontal or vertical degenerate box.
struct DesignTrackAssignmentShape
{
  TechRoutingLayerId layer;
  Rect box;
};

template <>
struct GeometryPayloadTraits<DesignTrackAssignmentShape>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kTrackAssignment;
};

[[nodiscard]] inline GeometryOwner designTrackAssignmentOwner(DesignNetId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignTrackAssignment, id);
}

// Optional component owned by one DesignNet entity.
struct DesignNetTrackAssignments
{
  std::vector<GeometryShapeId> values;
};

}  // namespace eccdb
