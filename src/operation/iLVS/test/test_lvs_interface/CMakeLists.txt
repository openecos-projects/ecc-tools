add_executable(test_lvs_interface
    test_lvs_interface.cpp
)

target_link_libraries(test_lvs_interface
    PRIVATE
    ilvs_interface
    ilvs_data_manager
    ilvs_toolkit
    idm
)

add_test(NAME test_lvs_interface COMMAND test_lvs_interface)
