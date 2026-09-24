find_package(Eigen3 REQUIRED NO_MODULE)

add_library(ipw_delay_calculator
    ${IPW_MODULE}/delay_calculator/DelayCalculator.cpp
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
    ${IPW_MODULE}/delay_calculator/dc_data_manager
    ${IPW_MODULE}/delay_calculator
)
