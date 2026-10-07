#pragma once

#include <vector>

#include "common/GeometryTypes.h"
#include "tech/common/TechLayerIds.h"

namespace eccdb {

// Legacy comparison/input helpers retained for differential fixtures. Canonical
// editable design geometry lives in GeometryRegistry-owned shape entities.
struct DesignShapeSet
{
  std::vector<Rect> rectangles;
  std::vector<std::vector<Point>> polygons;
};

struct DesignLayerGeometry
{
  TechLayerId layer;
  DesignShapeSet shapes;
};

}  // namespace eccdb
