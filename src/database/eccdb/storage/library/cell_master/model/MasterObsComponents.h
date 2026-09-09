#pragma once

#include <cstdint>
#include <vector>

#include "geometry/GeometryHandle.h"
#include "library/common/LibraryGeometry.h"

namespace eccdb {

namespace LibraryObsLayerFlag {
constexpr uint32_t kExceptPgNet = 1u << 0;
constexpr uint32_t kHasSpacing = 1u << 1;
constexpr uint32_t kHasDesignRuleWidth = 1u << 2;
constexpr uint32_t kHasPathWidth = 1u << 3;
}  // namespace LibraryObsLayerFlag

// LEF 5.8 MACRO obstruction syntax represented by LibraryMasterObs:
//   OBS
//     {LAYER layerName [EXCEPTPGNET] [SPACING spacing |
//        DESIGNRULEWIDTH width] ; [WIDTH pathWidth ;] RECT ... ; ...
//      | VIA x y viaName ;} ...
//   END
// One value below is one LAYER clause. The same layer may occur more than once
// when clause attributes differ, so clauses remain in source order.
struct LibraryObsLayerClause
{
  TechLayerId layer;
  uint32_t flags = 0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
  int32_t path_width = 0;
  GeometryHandle geometry;
};

// Anonymous OBS sections of one MACRO are normalized into this component. VIA
// placements are siblings of LAYER clauses in the LEF grammar.
struct LibraryMasterObs
{
  std::vector<LibraryObsLayerClause> layer_clauses;
  std::vector<LibraryViaPlacement> vias;
};

}  // namespace eccdb
