add_executable(iemir_ir_em_test
    ${IEMIR_TEST}/iemir_ir_em_test.cpp
    ${IEMIR_SOURCE}/data_manager/RedHawkResNetworkReader.cpp
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

add_test(NAME iemir_ir_em_test COMMAND iemir_ir_em_test)

add_executable(iemir_ir_solver_test ${IEMIR_TEST}/ir_linear_solver_test.cpp)
target_link_libraries(iemir_ir_solver_test PRIVATE iemir_ir_solver)
add_test(NAME iemir_ir_solver_test COMMAND iemir_ir_solver_test)

add_executable(iemir_ir_solver_benchmark ${IEMIR_TEST}/ir_solver_benchmark.cpp)
target_link_libraries(iemir_ir_solver_benchmark PRIVATE iemir_ir_solver)
