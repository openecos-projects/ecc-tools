add_library(ista_logger
    ${ISTA_TOOLKIT}/logger/Logger.cpp
)

target_link_libraries(ista_logger
    PUBLIC
    ista_data_manager
)

target_include_directories(ista_logger
    PUBLIC
    ${ISTA_TOOLKIT}/logger
)
