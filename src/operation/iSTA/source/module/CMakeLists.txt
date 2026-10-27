# The coordinator gives the execution order; subdirectories own business functions.
add_subdirectory(reader)
add_subdirectory(writer)
add_subdirectory(sdc)
add_subdirectory(graph_builder)
add_subdirectory(delay_calculator)
add_subdirectory(timing_checker)
add_subdirectory(timing_propagator)
add_subdirectory(path_analyzer)
add_subdirectory(timing_reporter)
add_subdirectory(model_extractor)

add_library(ista_timing_analyzer TimingAnalyzer.cpp)
target_include_directories(ista_timing_analyzer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ista_timing_analyzer PRIVATE
    ista_graph_builder ista_clock_propagator ista_data_propagator ista_required_propagator
    ista_path_analyzer ista_fanout_checker ista_electrical_checker ista_clock_checker ista_delay_calculator ista_toolkit
)
