add_library(imp-operator BlkClustering.cc HierPlacer.cc SAPlacer.cc MacroAligner.cc NetWeightPre.cc ClusterTimingEvaluator.cc Refinement.cc)
target_link_libraries(imp-operator
    PRIVATE
    imp-partition
    imp-basic
    imp-logger
    imp-parser
    power
)

target_include_directories(imp-operator
    PUBLIC
    ${IMP_OPERATOR}
)