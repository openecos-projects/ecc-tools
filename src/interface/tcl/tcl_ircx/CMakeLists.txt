add_library(tcl_ircx
    ${HOME_INTERFACE}/tcl/tcl_ircx/src/tcl_init_rcx.cpp
    ${HOME_INTERFACE}/tcl/tcl_ircx/src/tcl_run_rcx.cpp
    ${HOME_INTERFACE}/tcl/tcl_ircx/src/tcl_compare_spef.cpp
    ${HOME_INTERFACE}/tcl/tcl_ircx/src/tcl_dump_net_shape.cpp
    ${HOME_INTERFACE}/tcl/tcl_ircx/src/tcl_run_rcx_from_topology.cpp
    ${HOME_INTERFACE}/tcl/tcl_ircx/src/tcl_plot_spef.cpp
    ${HOME_INTERFACE}/tcl/tcl_ircx/src/tcl_destroy_rcx.cpp
)

target_link_libraries(tcl_ircx
    PUBLIC
    tcl_util
    PRIVATE
    ircx_interface
)

target_include_directories(tcl_ircx
    PUBLIC
    ${HOME_INTERFACE}/tcl/tcl_ircx/include
)
