aux_source_directory(. PY_ILVS_SRC)

add_library(py_ilvs ${PY_ILVS_SRC})

target_link_libraries(py_ilvs
    PUBLIC
        ilvs_interface
        pybind11::pybind11
)

target_include_directories(py_ilvs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
