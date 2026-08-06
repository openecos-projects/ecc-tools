add_executable(test_pa
    ${IRT_TEST}/test_pa/test_pa.cpp
)

target_link_libraries(test_pa PRIVATE ecc_logger)

