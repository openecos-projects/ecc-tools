add_executable(test_topo_builder
    test_topo_builder.cpp
)

target_link_libraries(test_topo_builder
    PRIVATE
    irt_topo_builder
    irt_toolkit
    ieda_logger
)

add_test(NAME test_topo_builder COMMAND test_topo_builder)
