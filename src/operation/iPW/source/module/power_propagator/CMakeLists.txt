add_library(ipw_power_propagator
    ${IPW_MODULE}/power_propagator/PowerPropagator.cpp
)

# 向量less 仿真器大量使用 std::popcount 做位并行的活动统计；
# 只开 POPCNT 这一条整数指令（不改 -march，避免 FMA 收缩改变浮点结果、破坏逐字节回归）。
target_compile_options(ipw_power_propagator PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-mpopcnt>)

target_link_libraries(ipw_power_propagator
    PUBLIC
    ipw_data_manager
    ipw_toolkit
    vcd
)

target_include_directories(ipw_power_propagator
    PUBLIC
    ${IPW_MODULE}/power_propagator/pp_data_manager
    ${IPW_MODULE}/power_propagator
)
