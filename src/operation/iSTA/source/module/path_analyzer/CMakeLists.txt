# Enumeration, tracing and grouping implement one path-analysis operation.
# Keep their algorithm files separate without exposing one library per helper.
add_library(ista_path_analyzer
    PathAnalyzer.cpp
    PathSearcher.cpp
    PathTracer.cpp
    PathGroupBuilder.cpp
)
target_link_libraries(ista_path_analyzer
    PUBLIC ista_path_checker
    PRIVATE ista_sdc ista_network
)
target_include_directories(ista_path_analyzer
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
