add_executable(test_gtl
    ${IRT_TEST}/test_gtl/test_gtl.cpp
)

target_link_libraries(test_gtl PRIVATE ecc_logger)

