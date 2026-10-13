aux_source_directory(. PY_IPW_SRC)

add_library(py_ipw ${PY_IPW_SRC})

target_link_libraries(py_ipw
    PUBLIC
        ipw_interface
        tcl
        pybind11::pybind11
        ecc_logger
)

target_include_directories(py_ipw
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HOME_UTILITY}/json
)
