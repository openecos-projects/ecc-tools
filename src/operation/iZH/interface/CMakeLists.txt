if(DEBUG_IZH_INTERFACE)
    message(STATUS "ZH: DEBUG_IZH_INTERFACE")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "ZH: RELEASE_IZH_INTERFACE")
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(_izh_interface_sources
    ${IZH_INTERFACE}/ZHInterface.cpp
)

if(TARGET eccdb_core)
    list(APPEND _izh_interface_sources
        ${PROJECT_SOURCE_DIR}/src/database/eccdb/adapters/izh/ZHInterfaceEccdb.cpp
    )
endif()

add_library(izh_interface ${_izh_interface_sources})

target_link_libraries(izh_interface
    PRIVATE
    izh_source
    idrc_interface
    ecc_feature
    idm
)

if(TARGET eccdb_core)
    target_compile_definitions(izh_interface PRIVATE IZH_HAS_ECCDB=1)
    target_link_libraries(izh_interface PRIVATE eccdb_core)

    add_subdirectory(
        ${PROJECT_SOURCE_DIR}/src/database/eccdb/tests/adapters/izh
        ${CMAKE_CURRENT_BINARY_DIR}/eccdb_izh_adapter_tests
    )
endif()

target_include_directories(izh_interface
    PUBLIC
    ${IZH_INTERFACE}
)
