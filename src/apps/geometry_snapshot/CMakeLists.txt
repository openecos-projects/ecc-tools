add_executable(ecc_geometry_snapshot
    main.cpp
)

set_target_properties(ecc_geometry_snapshot
    PROPERTIES
        OUTPUT_NAME ecc-geometry-snapshot
)

target_link_libraries(ecc_geometry_snapshot
    PRIVATE
        IdbBuilder
        geometry_builder
)

add_test(NAME ecc_geometry_snapshot_help COMMAND ecc_geometry_snapshot --help)
add_test(NAME ecc_geometry_snapshot_synthetic
    COMMAND ecc_geometry_snapshot
        --mode synthetic
        --out ${CMAKE_CURRENT_BINARY_DIR}/synthetic_smoke
        --synthetic-shapes 1024
)
