add_executable(test_boost
    ${IRT_TEST}/test_boost/test_boost.cpp
)

target_link_libraries(test_boost PRIVATE ecc_logger)

