#pragma once

#include <cstdint>
#include <vector>

#include "geometry/GeometryHandle.h"
#include "library/common/LibraryGeometry.h"
#include "tech/common/TechLayerTypes.h"

namespace eccdb {

// One LEF PORT is a physical connection group below a logical master term:
//   PORT
//     [CLASS {NONE | CORE | BUMP} ;]
//     {LAYER layerName ; [WIDTH width ;] RECT ... ; ... | VIA ... ;} ...
//   END
enum class LibraryMasterPortClass : uint8_t
{
  kNone = 0,
  kCore,
  kBump
};

// Persistent payload for one LEF PORT LAYER clause. The handle selects one
// group in LibraryStore's GeometryPool, so every RECT does not repeat its
// Tech layer ID and does not allocate a separate vector.
struct LibraryPortLayerGeometry
{
  TechLayerId layer;
  GeometryHandle geometry;
};

// One complete LEF PORT and the only payload component on its entity. Geometry
// values do not receive independent EnTT IDs. The owner term is maintained by
// LibraryMasterPortStorage.
struct LibraryMasterPort
{
  LibraryMasterPortClass port_class = LibraryMasterPortClass::kNone;
  std::vector<LibraryPortLayerGeometry> layer_clauses;
  std::vector<LibraryViaPlacement> vias;

  // Storage-managed reverse link to the logical PIN that owns this PORT.
  LibraryMasterTermId term;
};

}  // namespace eccdb
