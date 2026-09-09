#pragma once

namespace eccdb {

// LEF 5.8 OVERLAP layer:
//   LAYER name
//     TYPE OVERLAP ;
//   END name
// It has no type-specific technology-rule payload. Macro-specific OVERLAP
// geometry is stored by LibraryMasterObs on each CellMaster.
struct TechOverlapLayer
{
};

}  // namespace eccdb
