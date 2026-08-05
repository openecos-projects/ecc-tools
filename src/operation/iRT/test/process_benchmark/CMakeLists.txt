add_executable(process_benchmark
    ${IRT_TEST}/process_benchmark/process_benchmark.cpp
)

target_link_libraries(process_benchmark PRIVATE ecc_logger)
