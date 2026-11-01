# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2026 Peng Cheng Laboratory

add_library(py_eccdb
    py_eccdb.cpp
)

target_link_libraries(py_eccdb
    PUBLIC
        eccdb_scripting
        pybind11::pybind11
)

target_include_directories(py_eccdb
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
