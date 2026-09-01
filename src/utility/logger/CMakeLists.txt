add_library(ecc_logger
    Logger.cpp
)

target_include_directories(ecc_logger
    PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

if(BUILD_TESTING)
    add_executable(logger_throw_test
        logger_throw_test.cpp
    )

    target_link_libraries(logger_throw_test
        PRIVATE
        ecc_logger
    )

    set_target_properties(logger_throw_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    add_test(NAME logger_throw_test COMMAND logger_throw_test)
endif()
