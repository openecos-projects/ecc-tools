add_executable(test_izh_antenna_checker
    ${IZH_TEST}/test_antenna_checker/test_antenna_checker.cpp
)

target_link_libraries(test_izh_antenna_checker
    PRIVATE
    izh_antenna_checker
)

add_test(NAME test_izh_antenna_checker COMMAND test_izh_antenna_checker)
