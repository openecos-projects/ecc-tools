if(DEBUG_IFP_INTERFACE)
    message(STATUS "iFP: DEBUG_IFP_INTERFACE")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_INTERFACE")
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(_ifp_interface_sources
    ${IFP_INTERFACE}/FPInterface.cpp
)

if(TARGET eccdb_core)
    list(APPEND _ifp_interface_sources
        ${PROJECT_SOURCE_DIR}/src/database/eccdb/adapters/ifp/FPInterfaceEnTT.cpp
    )
endif()

add_library(ifp_interface ${_ifp_interface_sources})

target_link_libraries(ifp_interface
    PRIVATE
    ifp_source
    idm
)

if(TARGET eccdb_core)
    target_compile_definitions(ifp_interface PRIVATE IFP_HAS_ECCDB=1)
    target_link_libraries(ifp_interface PRIVATE eccdb_core)

    add_subdirectory(
        ${PROJECT_SOURCE_DIR}/src/database/eccdb/tests/adapters/ifp
        ${CMAKE_CURRENT_BINARY_DIR}/eccdb_ifp_adapter_tests
    )
endif()

target_include_directories(ifp_interface
    PUBLIC
    ${IFP_INTERFACE}
)
