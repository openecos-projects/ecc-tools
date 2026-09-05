#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "library/common/LibraryTypes.h"
#include "tech/common/TechLayerIds.h"

namespace eccdb {

// Optional operational Core boundary. DEF has no COREAREA statement; when
// absent, DesignFloorplanStorage derives the Core from non-PAD Rows.
struct DesignCoreArea
{
  std::vector<Point> boundary;
};

namespace DesignRowFlag {
constexpr uint32_t kHasDo = 1u << 0;
constexpr uint32_t kHasStep = 1u << 1;
}  // namespace DesignRowFlag

// DEF ROW rowName siteName origin orientation DO/BY/STEP.
struct DesignRow
{
  std::string name;
  LibrarySiteId site;
  Point origin;
  DesignOrientation orientation = DesignOrientation::kN;
  uint32_t repeat_count_x = 1;
  uint32_t repeat_count_y = 1;
  int32_t step_x = 0;
  int32_t step_y = 0;
  uint32_t flags = 0;
  std::vector<DesignProperty> properties;
};

namespace DesignTrackGridFlag {
constexpr uint32_t kHasMask = 1u << 0;
constexpr uint32_t kSameMask = 1u << 1;
}  // namespace DesignTrackGridFlag

// DEF TRACKS. One entity represents one TRACKS statement and may reference
// multiple routing layers.
struct DesignTrackGrid
{
  DesignAxis axis = DesignAxis::kX;
  int32_t start = 0;
  uint32_t track_count = 0;
  int32_t step = 0;
  uint32_t flags = 0;
  uint32_t mask = 0;
  std::vector<TechRoutingLayerId> layers;
};

// DEF GCELLGRID. X and Y clauses remain separate entities.
struct DesignGCellGrid
{
  DesignAxis axis = DesignAxis::kX;
  int32_t start = 0;
  uint32_t line_count = 0;
  int32_t step = 0;
};

}  // namespace eccdb
