#pragma once

#include <cstdint>
#include <vector>

#include "common/GeometryTypes.h"
#include "tech/common/TechLayerIds.h"

namespace eccdb {

namespace DesignFillFlag {
constexpr uint32_t kOpc = 1u << 0;
constexpr uint32_t kHasMask = 1u << 1;
}  // namespace DesignFillFlag

// DEF 5.8 rectangular layer fill:
//   FILLS count ;
//     - LAYER layerName [ + MASK mask ] [ + OPC ]
//       RECT pt pt ... ;
//   END FILLS
//
// Polygon and VIA fill alternatives are intentionally outside the current
// model. Rectangles share the layer, mask and OPC options of one DEF entry.
struct DesignFill
{
  TechLayerId layer;
  uint32_t flags = 0;
  uint32_t mask = 0;
  std::vector<Rect> rectangles;
};

}  // namespace eccdb
