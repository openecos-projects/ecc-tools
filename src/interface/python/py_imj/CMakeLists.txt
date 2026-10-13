aux_source_directory(. PY_IMJ_SRC)

add_library(py_imj ${PY_IMJ_SRC})

target_link_libraries(py_imj
    PUBLIC
        imj_interface
        tcl
        pybind11::pybind11
        ecc_logger

)
target_include_directories(py_imj
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HOME_UTILITY}/json
)
