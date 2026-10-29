aux_source_directory(. PY_IDB_SRC)

# py_clock_tree.cpp converts Python-owned CTS timing graphs into settled
# clock-tree snapshots. List it explicitly because aux_source_directory only
# refreshes when CMake is reconfigured.
list(APPEND PY_IDB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/py_clock_tree.cpp)

add_library(py_idb ${PY_IDB_SRC})

target_link_libraries(py_idb
PUBLIC
        idm
        pybind11::pybind11
        ecc_report
        ecc_logger
        ecc_feature
        geometry_builder
)
target_include_directories(py_idb
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
