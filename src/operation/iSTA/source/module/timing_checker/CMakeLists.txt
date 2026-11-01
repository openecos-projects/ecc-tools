add_library(ista_electrical_checker ElectricalChecker.cpp)
target_link_libraries(ista_electrical_checker PUBLIC ista_model PRIVATE ista_delay_calculator ista_network ista_sdc ista_graph_query)
target_include_directories(ista_electrical_checker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(ista_fanout_checker
    FanoutChecker.cpp
)
target_link_libraries(ista_fanout_checker
    PUBLIC ista_sdc
    PRIVATE ista_graph_query
)
target_include_directories(ista_fanout_checker
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)


add_library(ista_clock_reconvergence_calculator
    ClockReconvergenceCalculator.cpp
)
target_link_libraries(ista_clock_reconvergence_calculator
    PUBLIC ista_model
    PRIVATE ista_graph_query
)
target_include_directories(ista_clock_reconvergence_calculator
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(ista_path_checker
    PathChecker.cpp
)
target_link_libraries(ista_path_checker
    PUBLIC ista_graph_query
    PRIVATE ista_delay_calculator ista_clock_reconvergence_calculator ista_sdc ista_utility
)
target_include_directories(ista_path_checker
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(ista_clock_checker ClockChecker.cpp)
target_include_directories(ista_clock_checker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ista_clock_checker PUBLIC ista_model PRIVATE ista_clock_reconvergence_calculator ista_graph_query)
