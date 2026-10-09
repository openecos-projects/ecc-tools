add_executable(test_imj_antenna_checker
    ${IMJ_TEST}/test_antenna_checker/test_antenna_checker.cpp
)

target_link_libraries(test_imj_antenna_checker
    PRIVATE
    imj_antenna_checker
)

add_test(NAME test_imj_antenna_checker COMMAND test_imj_antenna_checker)
