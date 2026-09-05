add_executable(test_pa_state EXCLUDE_FROM_ALL test_pa_state.cpp)
target_link_libraries(test_pa_state PRIVATE irt_pin_accessor)

add_executable(test_pa_drc EXCLUDE_FROM_ALL test_pa_drc.cpp)
target_link_libraries(test_pa_drc PRIVATE irt_pin_accessor)
# Test private input builders without changing the production interface or running the DRC engine.
target_compile_options(test_pa_drc PRIVATE -fno-access-control)
target_link_options(test_pa_drc PRIVATE -Wl,--wrap=_ZN3irt9DRCEngine16getViolationListERNS_6DETaskE)
