add_library(ircx_compare_spef
  compare/Comparator.cc
  compare/CouplingCapComparator.cc
  compare/CompareMath.cc
  config/CompareSpefConfig.cc
  compare/NetSelector.cc
  compare/PathPairGenerator.cc
  report/ReportWriter.cc
  compare/ResistanceSolver.cc
  compare/ResultSorter.cc
  reader/SpefReader.cc
)

target_link_libraries(ircx_compare_spef
  PUBLIC
    ircx_headers
  PRIVATE
    log
    spef
    fort
    OpenMP::OpenMP_CXX
)

target_include_directories(ircx_compare_spef
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
