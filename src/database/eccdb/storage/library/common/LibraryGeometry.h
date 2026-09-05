#pragma once

#include <vector>

#include "common/GeometryTypes.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

class TechRegistry;

// LEF VIA placement inside a PIN/PORT or OBS:
//   VIA [MASK ...] x y viaName ;
// The master VIA remains owned by Tech; MASK is not represented yet.
struct LibraryViaPlacement
{
  TechViaMasterId via;
  Point origin;
};

void validateLibraryViaPlacements(const TechRegistry& tech_registry, const std::vector<LibraryViaPlacement>& placements);

}  // namespace eccdb
