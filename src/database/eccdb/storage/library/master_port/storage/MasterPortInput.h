#pragma once

#include <vector>

#include "geometry/GeometryInput.h"
#include "library/common/LibraryGeometry.h"
#include "library/master_port/model/MasterPortComponents.h"

namespace eccdb {

struct LibraryPortLayerGeometryInput
{
  TechLayerId layer;
  GeometryInput geometry;
};

struct LibraryMasterPortInput
{
  LibraryMasterPortClass port_class = LibraryMasterPortClass::kNone;
  std::vector<LibraryPortLayerGeometryInput> layer_clauses;
  std::vector<LibraryViaPlacement> vias;
};

}  // namespace eccdb
