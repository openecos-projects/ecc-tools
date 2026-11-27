#pragma once

#include <map>
#include <vector>

#include "LayoutData.hpp"
#include "MetalDensity.hpp"

namespace ircx {
using LayerMetalShapes = std::map<int32_t, std::vector<DensityRect>>;

// Preserve individual physical rectangles; a topology node's bounding box is
// not a metal shape. Coordinates are converted from database units once here.
ProcessResult<LayerMetalShapes> collectLayoutMetal(LayoutData& layout, double scale);
}  // namespace ircx
