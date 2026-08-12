aux_source_directory(. PY_IFP_SRC)

add_library(py_ifp ${PY_IFP_SRC})

target_link_libraries(py_ifp
    PUBLIC
        ifp_interface
        pybind11::pybind11
)

target_include_directories(py_ifp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
