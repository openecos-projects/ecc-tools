#pragma once

#include <vector>

#include "common/GeometryTypes.h"

namespace eccdb {

// Write-side polygon value. GeometryPool converts it into an internal polygon
// record and a contiguous point range.
struct GeometryPolygonInput
{
  std::vector<Point> points;
};

// Shared write-side geometry accepted by database storage facades. It is
// never attached to an EnTT entity or included in the binary schema.
struct GeometryInput
{
  std::vector<Rect> rects;
  std::vector<GeometryPolygonInput> polygons;

  [[nodiscard]] bool empty() const noexcept { return rects.empty() && polygons.empty(); }
};

}  // namespace eccdb
