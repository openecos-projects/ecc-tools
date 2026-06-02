aux_source_directory(. PY_IZH_SRC)

add_library(py_izh ${PY_IZH_SRC})

target_link_libraries(py_izh
    PUBLIC
        izh_interface
        pybind11::pybind11
)

target_include_directories(py_izh
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HOME_THIRDPARTY}/json
)
