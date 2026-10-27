# Public value types have no dependency on the database, parsers or logger.
add_library(ista_timing_data INTERFACE)
target_include_directories(ista_timing_data INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ista_timing_data INTERFACE cxx_std_20)

add_library(ista_interface ${ISTA_INTERFACE}/STAInterface.cpp)
target_link_libraries(ista_interface
    PRIVATE ista_timing_analyzer ista_delay_calculator ista_data_manager ista_toolkit ista_sdc_frontend ista_sdc_writer
        ista_reader ista_data_exporter ista_timing_reporter ista_sdf_writer ista_model_extractor ista_liberty_writer idm
    PUBLIC ista_timing_data
)

# Convert internal results to the public value contract without running analysis.
add_library(ista_data_exporter EndpointQualifier.cpp LibertyDataExporter.cpp TimingDataExporter.cpp)
target_link_libraries(ista_data_exporter PUBLIC ista_timing_data PRIVATE ista_model ista_sdc)
target_include_directories(ista_data_exporter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
