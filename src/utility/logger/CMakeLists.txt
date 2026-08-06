add_library(ecc_logger
    Logger.cpp
)

target_include_directories(ecc_logger
    PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)
