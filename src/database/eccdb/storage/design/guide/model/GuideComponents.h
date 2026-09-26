#pragma once

#include <cstdint>
#include <vector>

#include "common/GeometryTypes.h"
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

// Optional component owned by one DesignNet entity. Guide entries have no
// independent entity identity because TritonRoute replaces a net's complete
// guide set as one result.
struct DesignNetRouteGuides
{
  std::vector<DesignRouteGuide> values;
};

// One ODB dbNetTrack-like result from track assignment. Unlike guides, a
// track can be represented by a horizontal or vertical degenerate box.
struct DesignTrackAssignmentShape
{
  TechRoutingLayerId layer;
  Rect box;
};

// Optional component owned by one DesignNet entity.
struct DesignNetTrackAssignments
{
  std::vector<DesignTrackAssignmentShape> values;
};

}  // namespace eccdb
