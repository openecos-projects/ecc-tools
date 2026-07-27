add_executable(test_lvs_checker
    test_lvs_checker.cpp
)

target_link_libraries(test_lvs_checker
    PRIVATE
    ilvs_lvs_checker
    ilvs_toolkit
)

add_test(NAME test_lvs_checker COMMAND test_lvs_checker)
