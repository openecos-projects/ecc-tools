add_executable(test_snapshot_io
    test_snapshot_io.cpp
)

target_link_libraries(test_snapshot_io
    PRIVATE
    ilvs_snapshot_io
    ilvs_toolkit
)

add_test(NAME test_snapshot_io COMMAND test_snapshot_io)
