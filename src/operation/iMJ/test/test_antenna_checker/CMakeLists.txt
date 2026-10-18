add_executable(test_imj_antenna_checker
    ${IMJ_TEST}/test_antenna_checker/test_antenna_checker.cpp
)

target_link_libraries(test_imj_antenna_checker
    PRIVATE
    imj_antenna_checker
)

add_test(NAME test_imj_antenna_checker COMMAND test_imj_antenna_checker)

add_executable(test_imj_antenna_reporting
    ${IMJ_TEST}/test_antenna_checker/test_antenna_reporting.cpp
)

target_link_libraries(test_imj_antenna_reporting
    PRIVATE
    imj_interface
    imj_antenna_checker
    idm
)

add_test(NAME test_imj_antenna_reporting COMMAND test_imj_antenna_reporting)

add_test(NAME test_imj_antenna_command
    COMMAND ecc_bin -script ${IMJ_TEST}/test_antenna_checker/data/test_check_antenna.tcl
)
