add_library(tcl_ipw
    ${HOME_INTERFACE}/tcl/tcl_ipw/src/tcl_init_pw.cpp
    ${HOME_INTERFACE}/tcl/tcl_ipw/src/tcl_run_pw.cpp
    ${HOME_INTERFACE}/tcl/tcl_ipw/src/tcl_destroy_pw.cpp
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
