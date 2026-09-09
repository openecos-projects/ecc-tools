if(DEBUG_IDRC_INTERFACE)
    message(STATUS "DRC: DEBUG_IDRC_INTERFACE")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "DRC: RELEASE_IDRC_INTERFACE")
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(_idrc_interface_sources
    ${IDRC_INTERFACE}/DRCInterface.cpp
)

if(TARGET eccdb_design)
    list(APPEND _idrc_interface_sources
        ${PROJECT_SOURCE_DIR}/src/database/eccdb/adapters/idrc/DRCInterfaceEnTT.cpp
    )
endif()

add_library(idrc_interface ${_idrc_interface_sources})

target_link_libraries(idrc_interface
    PRIVATE
    idrc_source
    idm
    file_manager_drc
    ecc_logger
)

if(TARGET eccdb_design)
    target_link_libraries(idrc_interface
        PRIVATE
        eccdb_design
        eccdb_tech_database
        eccdb_library
    )
endif()

target_include_directories(idrc_interface
    PUBLIC
    ${IDRC_INTERFACE}
)

if(TARGET eccdb_design)
    add_subdirectory(
        ${PROJECT_SOURCE_DIR}/src/database/eccdb/tests/adapters/idrc
        ${CMAKE_CURRENT_BINARY_DIR}/eccdb_idrc_adapter_tests
    )
endif()
