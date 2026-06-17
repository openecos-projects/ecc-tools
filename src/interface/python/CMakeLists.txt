# add_subdirectory(${HOME_THIRDPARTY}/pybind11 binary_dir)

SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# DEFINE PYTHON MODULE & CMAKE TARGET
pybind11_add_module(ecc_py python_moodule.cc)

add_subdirectory(py_idb)
add_subdirectory(py_config)
add_subdirectory(py_flow)
add_subdirectory(py_icts)
add_subdirectory(py_idrc)
add_subdirectory(py_ifp)
add_subdirectory(py_instance)
add_subdirectory(py_ipdn)
add_subdirectory(py_irt)
add_subdirectory(py_ircx)
add_subdirectory(py_ista)
add_subdirectory(py_izh)
add_subdirectory(py_report)
add_subdirectory(py_feature)
add_subdirectory(py_eval)
add_subdirectory(py_vec)
add_subdirectory(py_imp)

# ADD SOURCE FILES
# file(GLOB_RECURSE PYTHON_INTERFACE_SRC CONFIGURE_DEPENDS ./*.cpp ./*.cc)
file(GLOB PYTHON_INTERFACE_SRC CONFIGURE_DEPENDS *.cpp .h)

# SET SOURCE FILES
target_sources(ecc_py PUBLIC ${PYTHON_INTERFACE_SRC})

target_link_libraries(ecc_py
    PRIVATE
        py_idb
        py_config
        py_flow
        py_icts
        py_idrc
        py_ifp
        py_inst
        py_ipdn
        py_irt
        py_ircx
        py_ista
        py_izh
        py_report
        py_feature
        py_eval
        log
        py_vec
        py_imp
)

set_target_properties(ecc_py PROPERTIES
    INSTALL_RPATH "$ORIGIN/lib"
)

install(TARGETS ecc_py
    DESTINATION ${CMAKE_INSTALL_PREFIX}/ecc_tools_bin
)

get_target_property(DEPENDENCIES ecc_py INTERFACE_LINK_LIBRARIES)
message(STATUS "ecc_py install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "ecc_py dependencies: ${DEPENDENCIES}")
