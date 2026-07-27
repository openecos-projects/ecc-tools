add_executable(test_netlist_extractor
    test_netlist_extractor.cpp
)

target_link_libraries(test_netlist_extractor
    PRIVATE
    ilvs_netlist_extractor
    ilvs_toolkit
    idm
)

add_test(NAME test_netlist_extractor COMMAND test_netlist_extractor)
