# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

add_library(eccdb_geometry
  geometry/PolygonRectDecomposer.cpp
)
eccdb_storage_target(eccdb_geometry)
set_target_properties(eccdb_geometry PROPERTIES EXPORT_NAME geometry)

add_subdirectory(tech)
add_subdirectory(library)
add_subdirectory(design)
