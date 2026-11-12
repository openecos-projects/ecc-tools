find_package(Eigen3 REQUIRED NO_MODULE)

add_library(ipw_delay_calculator
    ${IPW_MODULE}/delay_calculator/DelayCalculator.cpp
    ${IPW_MODULE}/delay_calculator/dc_data_manager/cache/DelayCache.cpp
    ${IPW_MODULE}/delay_calculator/dc_data_manager/lookup/ArcTimingLookup.cpp
    ${IPW_MODULE}/delay_calculator/dc_data_manager/load/OutputLoadEstimator.cpp
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic/ParasiticNodeName.cpp
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic/rc_tree/RcTreeApprox.cpp
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic/rc_tree/RcTree.cpp
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic/dmp/ParasiticDmpSolver.cpp
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic/arnoldi/ParasiticArnoldiSolver.cpp
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic/threshold/ParasiticThresholds.cpp
)

target_link_libraries(ipw_delay_calculator
    PRIVATE
    Eigen3::Eigen
    PUBLIC
    ipw_data_manager
    ipw_toolkit
)

target_include_directories(ipw_delay_calculator
    PUBLIC
    ${IPW_MODULE}/delay_calculator
    ${IPW_MODULE}/delay_calculator/dc_data_manager
    ${IPW_MODULE}/delay_calculator/dc_data_manager/task
    ${IPW_MODULE}/delay_calculator/dc_data_manager/cache
    ${IPW_MODULE}/delay_calculator/dc_data_manager/lookup
    ${IPW_MODULE}/delay_calculator/dc_data_manager/load
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic/rc_tree
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic/dmp
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic/arnoldi
    ${IPW_MODULE}/delay_calculator/dc_data_manager/parasitic/threshold
)
