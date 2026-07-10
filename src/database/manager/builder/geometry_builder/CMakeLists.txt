add_library(geometry_builder
    BlockageEditAdapter.cpp
    GeometryBuilder.cpp
    GeometryEditApplier.cpp
    GeometryEditJson.cpp
    GeometryNameQuery.cpp
    GeometrySnapshotReader.cpp
    GeometrySnapshotWorkflow.cpp
    GeometrySnapshotWriter.cpp
    InstanceEditAdapter.cpp
    NetWireEditAdapter.cpp
    StoreGeometrySink.cpp
)

target_include_directories(geometry_builder
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_DATABASE}/data/geometry
        ${HOME_DATABASE}/data/design
        ${HOME_DATABASE}/data/design/db_design
        ${HOME_DATABASE}/data/design/db_layout
)

target_link_libraries(geometry_builder
    PUBLIC
        geometry_data
        idb
        geometry_db
)

add_executable(geometry_builder_tests
    test/GeometryBuilderSinkTest.cpp
)

target_link_libraries(geometry_builder_tests
    PRIVATE
        geometry_builder
)

add_test(NAME geometry_builder_tests COMMAND geometry_builder_tests)
