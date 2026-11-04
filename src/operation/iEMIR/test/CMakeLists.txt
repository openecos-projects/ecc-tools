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

add_test(NAME iemir_ir_em_test COMMAND iemir_ir_em_test)

add_executable(iemir_power_source_test ${IEMIR_TEST}/power_source_test.cpp)
target_link_libraries(iemir_power_source_test PRIVATE iemir_graph_builder)
add_test(NAME iemir_power_source_test COMMAND iemir_power_source_test)

find_program(IEMIR_TEST_PYTHON NAMES python3)
if(IEMIR_TEST_PYTHON)
    add_test(NAME iemir_ploc_command_test
        COMMAND ${IEMIR_TEST_PYTHON} ${IEMIR_TEST}/ploc_command_test.py
                $<TARGET_FILE:ecc_bin>)
    add_test(NAME iemir_def_source_command_test
        COMMAND ${IEMIR_TEST_PYTHON} ${IEMIR_TEST}/def_source_command_test.py
                $<TARGET_FILE:ecc_bin>)
    if(BUILD_PYTHON)
        add_test(NAME iemir_ploc_python_test
            COMMAND ${IEMIR_TEST_PYTHON} ${IEMIR_TEST}/ploc_command_test.py
                    $<TARGET_FILE:ecc_bin> $<TARGET_FILE_DIR:ecc_py>)
        add_test(NAME iemir_def_source_python_test
            COMMAND ${IEMIR_TEST_PYTHON} ${IEMIR_TEST}/def_source_command_test.py
                    $<TARGET_FILE:ecc_bin> $<TARGET_FILE_DIR:ecc_py>)
    endif()
endif()

add_executable(iemir_ir_solver_test ${IEMIR_TEST}/ir_linear_solver_test.cpp)
target_link_libraries(iemir_ir_solver_test PRIVATE iemir_ir_solver)
add_test(NAME iemir_ir_solver_test COMMAND iemir_ir_solver_test)

add_executable(iemir_ir_solver_benchmark ${IEMIR_TEST}/ir_solver_benchmark.cpp)
target_link_libraries(iemir_ir_solver_benchmark PRIVATE iemir_ir_solver)

if(IEMIR_TEST_PYTHON)
    add_test(NAME iemir_em_evaluation_command_test
        COMMAND ${IEMIR_TEST_PYTHON} ${IEMIR_TEST}/em_evaluation_command_test.py
                $<TARGET_FILE:ecc_bin>)
endif()
