// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

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
