add_library(ircx_tool_plot_spef
  internal/InternalPlotSpefWriter.cc
  PlotSpefTool.cc
  builder/PlotSpefModelBuilder.cc
  cap/PlotSpefCapToEdge.cc
  config/PlotSpefConfig.cc
  gds/PlotSpefGdsWriter.cc
  lyp/PlotSpefLypWriter.cc
  report/PlotSpefCgEdgeReport.cc
  select/PlotSpefSelect.cc
)

target_link_libraries(ircx_tool_plot_spef
  PUBLIC
    ircx_headers
  PRIVATE
    gdsii-parser
    ircx_database_design
    ircx_logger
    OpenMP::OpenMP_CXX
    spef
)

target_include_directories(ircx_tool_plot_spef
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
