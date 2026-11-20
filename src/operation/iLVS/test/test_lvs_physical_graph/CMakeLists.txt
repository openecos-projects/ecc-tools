add_executable(test_lvs_physical_graph
    test_lvs_physical_graph.cpp
)

target_link_libraries(test_lvs_physical_graph
    PRIVATE
    ilvs_data_manager
    ilvs_interface
    ilvs_routing_checker
    ilvs_toolkit
)

add_test(NAME test_lvs_physical_graph COMMAND test_lvs_physical_graph)
