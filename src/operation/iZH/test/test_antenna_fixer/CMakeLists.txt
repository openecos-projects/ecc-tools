add_executable(test_izh_antenna_fixer
    ${IZH_TEST}/test_antenna_fixer/test_antenna_fixer.cpp
)

target_link_libraries(test_izh_antenna_fixer
    PRIVATE
    izh_antenna_checker
)

add_test(NAME test_izh_antenna_fixer COMMAND test_izh_antenna_fixer)
