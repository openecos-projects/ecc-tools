add_executable(test_lvs_data_manager
    test_lvs_data_manager.cpp
)

target_link_libraries(test_lvs_data_manager
    PRIVATE
    ilvs_data_manager
    ilvs_toolkit
)

add_test(NAME test_lvs_data_manager COMMAND test_lvs_data_manager)
