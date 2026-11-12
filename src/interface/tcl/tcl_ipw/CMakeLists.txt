add_library(tcl_ipw
    ${HOME_INTERFACE}/tcl/tcl_ipw/src/tcl_init_pw.cpp
    ${HOME_INTERFACE}/tcl/tcl_ipw/src/tcl_run_pw.cpp
    ${HOME_INTERFACE}/tcl/tcl_ipw/src/tcl_destroy_pw.cpp
    ${HOME_INTERFACE}/tcl/tcl_ipw/src/tcl_report_cell_power.cpp
    ${HOME_INTERFACE}/tcl/tcl_ipw/src/tcl_report_net_power.cpp
    ${HOME_INTERFACE}/tcl/tcl_ipw/src/tcl_report_peak_power_waveform.cpp
)

target_link_libraries(tcl_ipw
    PUBLIC
    tcl
    PRIVATE
    ipw_interface
)

target_include_directories(tcl_ipw
    PUBLIC
    ${HOME_INTERFACE}/tcl/tcl_ipw/include
)
