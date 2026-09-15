add_executable(process_guide
    ${IRT_TEST}/process_guide/process_guide.cpp
)

target_link_libraries(process_guide PRIVATE ecc_logger)
