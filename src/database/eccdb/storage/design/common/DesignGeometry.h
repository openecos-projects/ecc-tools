#pragma once

#include <vector>

#include "common/GeometryTypes.h"
#include "tech/common/TechLayerIds.h"

namespace eccdb {

// Mutable Design geometry deliberately remains an ordinary nested value in
// V1. It has no GeometryHandle or per-shape entity identity.
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
