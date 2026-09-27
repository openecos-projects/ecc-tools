add_library(ista_path_exception
    ${ISTA_TOOLKIT}/path_exception/TimingExceptionMatcher.cpp
)

target_link_libraries(ista_path_exception
    PUBLIC
    ista_data_manager
    PRIVATE
    ista_utility
)

target_include_directories(ista_path_exception
    PUBLIC
    ${ISTA_TOOLKIT}/path_exception
)
