add_executable(iemir_ir_em_test
    ${IEMIR_TEST}/iemir_ir_em_test.cpp
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
