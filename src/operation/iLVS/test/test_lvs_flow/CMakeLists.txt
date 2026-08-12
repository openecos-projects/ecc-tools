add_executable(test_lvs_flow
    test_lvs_flow.cpp
)

target_link_libraries(test_lvs_flow
    PRIVATE
    ilvs_data_manager
    ilvs_interface
    ilvs_entity_checker
    ilvs_routing_checker
    ilvs_pdn_checker
    ilvs_lvs_reporter
    ilvs_toolkit
)

add_test(NAME test_lvs_flow COMMAND test_lvs_flow)
