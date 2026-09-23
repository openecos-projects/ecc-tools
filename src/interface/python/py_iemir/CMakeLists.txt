aux_source_directory(. PY_IEMIR_SRC)

add_library(py_iemir ${PY_IEMIR_SRC})

target_link_libraries(py_iemir
    PUBLIC
        iemir_interface
        pybind11::pybind11
)

target_include_directories(py_iemir
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
