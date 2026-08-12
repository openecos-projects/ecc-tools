add_library(ieda_logger
    Logger.cpp
)

target_include_directories(ieda_logger
    PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)
