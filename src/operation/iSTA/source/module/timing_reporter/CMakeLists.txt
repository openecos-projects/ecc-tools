add_library(ista_report_options INTERFACE)
target_link_libraries(ista_report_options INTERFACE ista_model)
target_include_directories(ista_report_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Selection and summary queries have no file-output side effects.
add_library(ista_report_paths path/ReportPathContext.cpp path/ReportPathSelector.cpp qor/QorSummaryBuilder.cpp)
target_link_libraries(ista_report_paths PUBLIC ista_report_options PRIVATE ista_network ista_toolkit ista_graph_query)
target_include_directories(ista_report_paths PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/path ${CMAKE_CURRENT_SOURCE_DIR}/qor)

add_library(ista_timing_reporter TimingReporter.cpp FanoutReportWriter.cpp ElectricalReportWriter.cpp ClockReportWriter.cpp TimingJsonWriter.cpp
    path/PathReportWriter.cpp qor/QorReportWriter.cpp)
target_link_libraries(ista_timing_reporter PUBLIC ista_report_paths PRIVATE ista_toolkit)
target_include_directories(ista_timing_reporter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
