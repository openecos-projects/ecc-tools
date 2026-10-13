add_executable(iemir_ir_em_test
    ${IEMIR_TEST}/iemir_ir_em_test.cpp
    ${IEMIR_SOURCE}/data_manager/ResistanceNetworkReader.cpp
)

target_link_libraries(iemir_ir_em_test
    PRIVATE
    iemir_data_manager
    iemir_graph_builder
    iemir_ir_analyzer
    iemir_em_analyzer
    iemir_emir_reporter
    iemir_toolkit
)

add_executable(iemir_via_provider_test iemir_via_provider_test.cpp)
target_link_libraries(iemir_via_provider_test PRIVATE iemir_interface iemir_data_manager idm)
add_test(NAME iemir_via_provider_test COMMAND iemir_via_provider_test
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cut-resistance.lef)

add_executable(iemir_port_topology_test iemir_port_topology_test.cpp)
target_link_libraries(iemir_port_topology_test PRIVATE
    iemir_graph_builder iemir_ir_analyzer iemir_data_manager iemir_toolkit)
add_test(NAME iemir_port_topology_test COMMAND iemir_port_topology_test)

add_executable(iemir_via_enclosure_topology_test iemir_via_enclosure_topology_test.cpp)
target_link_libraries(iemir_via_enclosure_topology_test PRIVATE
    iemir_graph_builder iemir_ir_analyzer iemir_data_manager iemir_toolkit)
add_test(NAME iemir_via_enclosure_topology_test COMMAND iemir_via_enclosure_topology_test)

add_test(NAME iemir_ir_em_test COMMAND iemir_ir_em_test)

add_executable(iemir_ir_solver_test ${IEMIR_TEST}/ir_linear_solver_test.cpp)
target_link_libraries(iemir_ir_solver_test PRIVATE iemir_ir_solver)
add_test(NAME iemir_ir_solver_test COMMAND iemir_ir_solver_test)

add_executable(iemir_ir_solver_benchmark ${IEMIR_TEST}/ir_solver_benchmark.cpp)
target_link_libraries(iemir_ir_solver_benchmark PRIVATE iemir_ir_solver)
