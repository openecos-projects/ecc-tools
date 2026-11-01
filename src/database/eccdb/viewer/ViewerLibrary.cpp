// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

// Native business services stay in eccdb_service. GUI hierarchy records are
// projected by the viewer adapter at this separate shared-library boundary.
namespace eccdb {
void eccdb_viewer_link_anchor() {}
}  // namespace eccdb
