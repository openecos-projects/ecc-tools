add_executable(test_lvs_reporter
    test_lvs_reporter.cpp
)

target_link_libraries(test_lvs_reporter
    PRIVATE
    ilvs_lvs_reporter
    ilvs_toolkit
)

add_test(NAME test_lvs_reporter COMMAND test_lvs_reporter)
