#pragma once

#include <cstdint>
#include <vector>

#include "geometry/GeometryInput.h"
#include "library/common/LibraryGeometry.h"
#include "tech/common/TechLayerIds.h"

namespace eccdb {

struct LibraryObsLayerClauseInput
{
  TechLayerId layer;
  uint32_t flags = 0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
  int32_t path_width = 0;
  GeometryInput geometry;
};

struct LibraryMasterObsInput
{
  std::vector<LibraryObsLayerClauseInput> layer_clauses;
  std::vector<LibraryViaPlacement> vias;
};

}  // namespace eccdb
