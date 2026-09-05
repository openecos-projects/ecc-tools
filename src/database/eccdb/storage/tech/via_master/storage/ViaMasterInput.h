#pragma once

#include "geometry/GeometryInput.h"
#include "tech/common/TechLayerTypes.h"

namespace eccdb {

// Write-side geometry for one fixed or generated VIA. Storage validates the
// layer roles and converts each GeometryInput into a GeometryHandle.
struct TechViaMasterShapeInput
{
  TechConductorLayerRef bottom_layer;
  GeometryInput bottom_geometry;

  TechCutLayerId cut_layer;
  GeometryInput cut_geometry;

  TechConductorLayerRef top_layer;
  GeometryInput top_geometry;
};

}  // namespace eccdb
