add_library(geometry_data
    GeometrySpatialIndex.cpp
    GeometryStore.cpp
    GeometryTilePyramid.cpp
    ShapeTable.cpp
)

target_include_directories(geometry_data
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(geometry_data_tests
    test/GeometryDataTests.cpp
)

target_link_libraries(geometry_data_tests
    PRIVATE
        geometry_data
)

add_test(NAME geometry_data_tests COMMAND geometry_data_tests)
