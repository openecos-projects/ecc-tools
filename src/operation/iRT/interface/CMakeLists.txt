if(DEBUG_IRT_INTERFACE)
    message(STATUS "RT: DEBUG_IRT_INTERFACE")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "RT: RELEASE_IRT_INTERFACE")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(irt_interface
    ${IRT_INTERFACE}/RTInterface.cpp
    ${PROJECT_SOURCE_DIR}/src/database/eccdb/adapters/irt/RTInterfaceEnTT.cpp
)

target_link_libraries(irt_interface
    PRIVATE
    irt_source
    idrc_interface
    ecc_feature
    idm
)

if(TARGET eccdb_design)
    target_link_libraries(irt_interface
        PRIVATE
        eccdb_design
        eccdb_tech_database
        eccdb_library
    )
endif()

target_include_directories(irt_interface
    PUBLIC
    ${IRT_INTERFACE}
)

if(TARGET eccdb_design)
    add_subdirectory(
        ${PROJECT_SOURCE_DIR}/src/database/eccdb/tests/adapters/irt
        ${CMAKE_CURRENT_BINARY_DIR}/eccdb_irt_adapter_tests
    )
endif()

if(ECCDB_BUILD_BENCHMARKS AND TARGET eccdb_design)
    add_executable(irt_input_benchmark
        ${PROJECT_SOURCE_DIR}/src/database/eccdb/benchmarks/IrtInputBenchmark.cpp
    )

    target_compile_features(irt_input_benchmark PUBLIC cxx_std_20)
    target_link_libraries(irt_input_benchmark
        PRIVATE
        irt_interface
        irt_data_manager
        irt_source
        idrc_interface
        idm
        ecc_feature
        eccdb_design
        eccdb_tech_database
        eccdb_library
        eccdb_lef_import
        eccdb_def_import
    )
endif()
