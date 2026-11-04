# Clock marking, generated sources and arc traversal cooperate in one propagation phase.
add_library(ista_clock_propagator ClockPropagator.cpp ClockNetworkBuilder.cpp ClockArcPropagator.cpp GeneratedClockSeeder.cpp)
target_link_libraries(ista_clock_propagator PUBLIC ista_model ista_delay_calculator PRIVATE ista_toolkit ista_graph_query)
target_include_directories(ista_clock_propagator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
