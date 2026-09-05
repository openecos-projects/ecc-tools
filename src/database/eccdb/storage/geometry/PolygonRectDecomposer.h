#pragma once

#include <span>
#include <vector>

#include "common/GeometryTypes.h"

namespace eccdb {

// Converts one LEF POLYGON with the same Boost.Polygon polygon_90 algorithm
// used by legacy iDB. This is a rectangle-only storage compatibility path;
// LEF-permitted 45-degree edges become Boost's deterministic staircase cover.
// Use native Polygon storage when the original boundary must be preserved.
[[nodiscard]] std::vector<Rect> decomposePolygonToRectangles(std::span<const Point> points);

}  // namespace eccdb
