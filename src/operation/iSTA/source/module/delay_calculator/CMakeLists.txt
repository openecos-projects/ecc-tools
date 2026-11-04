find_package(Eigen3 REQUIRED NO_MODULE)

# Value contract, usable by propagation without linking any delay implementation.
add_library(ista_delay_data INTERFACE)
target_link_libraries(ista_delay_data INTERFACE ista_model)
target_include_directories(ista_delay_data INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Numerical solvers take explicit library/reduced-model inputs, with no session dependency.
add_library(ista_dmp_ceff_calculator dmp/DmpCeffCalculator.cpp)
target_link_libraries(ista_dmp_ceff_calculator PUBLIC ista_model)
target_include_directories(ista_dmp_ceff_calculator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dmp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(ista_dmp_delay_calculator dmp/DmpDelayCalculator.cpp)
target_link_libraries(ista_dmp_delay_calculator PUBLIC ista_delay_data PRIVATE ista_dmp_ceff_calculator)
target_include_directories(ista_dmp_delay_calculator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dmp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(ista_arnoldi_delay_calculator arnoldi/ArnoldiDelayCalculator.cpp arnoldi/ArnoldiWaveformSolver.cpp)
target_link_libraries(ista_arnoldi_delay_calculator PUBLIC ista_model PRIVATE Eigen3::Eigen)
target_include_directories(ista_arnoldi_delay_calculator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/arnoldi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# One interconnect component: loads -> rooted RC models -> driver/receiver responses.
add_library(ista_parasitic_delay
    interconnect/PinCapacitanceCalculator.cpp interconnect/LoadCalculator.cpp
    interconnect/RcTreeAnalyzer.cpp interconnect/RcModelBuilder.cpp
    interconnect/ReceiverThresholdCalculator.cpp interconnect/InputPortResponseCalculator.cpp
    interconnect/WireDelayCalculator.cpp interconnect/DmpResponseCalculator.cpp interconnect/ArnoldiResponseCalculator.cpp
)
target_link_libraries(ista_parasitic_delay PUBLIC ista_delay_data
    PRIVATE ista_network ista_dmp_delay_calculator ista_arnoldi_delay_calculator ista_toolkit Eigen3::Eigen)
target_include_directories(ista_parasitic_delay PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/interconnect ${CMAKE_CURRENT_SOURCE_DIR}/dmp ${CMAKE_CURRENT_SOURCE_DIR}/arnoldi)

# Arc selection and cell/check evaluation share the calculator's request boundary.
add_library(ista_delay_calculator DelayCalculator.cpp NetThresholdAdjustment.cpp
    cell/LibertyArcSelector.cpp cell/CellDelayCalculator.cpp cell/CheckDelayCalculator.cpp)
target_link_libraries(ista_delay_calculator PUBLIC ista_delay_data ista_parasitic_delay PRIVATE ista_data_manager)
target_include_directories(ista_delay_calculator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cell)
foreach(target ista_delay_calculator ista_parasitic_delay ista_dmp_ceff_calculator ista_dmp_delay_calculator ista_arnoldi_delay_calculator)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
