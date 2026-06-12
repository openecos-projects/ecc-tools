aux_source_directory(. PY_IRCX_SRC)

add_library(py_ircx ${PY_IRCX_SRC})

target_link_libraries(py_ircx
    PUBLIC
        ircx_api
        ircx_ics55
        pybind11::pybind11
        idm
)
target_include_directories(py_ircx
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
