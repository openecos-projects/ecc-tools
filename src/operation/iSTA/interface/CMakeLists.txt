add_library(ista_interface
    ${ISTA_INTERFACE}/STAInterface.cpp
    ${CMAKE_SOURCE_DIR}/src/database/eccdb/adapters/ista/STAInterfaceEccdb.cpp
)

target_link_libraries(ista_interface
    PRIVATE
    ista_source
    ista_sdc_command
    idm
    liberty
    spef
    eccdb_core
)

target_include_directories(ista_interface
    PUBLIC
    ${ISTA_INTERFACE}
)

if(TARGET eccdb_core)
    add_subdirectory(
        ${PROJECT_SOURCE_DIR}/src/database/eccdb/tests/adapters/ista
        ${CMAKE_CURRENT_BINARY_DIR}/eccdb_ista_adapter_tests
    )
endif()
