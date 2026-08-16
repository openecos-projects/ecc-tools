add_library(tcl_ifp
    ${HOME_INTERFACE}/tcl/tcl_ifp/src/tcl_init_fp.cpp
    ${HOME_INTERFACE}/tcl/tcl_ifp/src/tcl_run_fp.cpp
    ${HOME_INTERFACE}/tcl/tcl_ifp/src/tcl_destroy_fp.cpp
    ${HOME_INTERFACE}/tcl/tcl_ifp/src/tcl_debug_input_macro.cpp
)

target_link_libraries(tcl_ifp
    PUBLIC
    tcl_util
    PRIVATE
    ifp_interface
)

target_include_directories(tcl_ifp
    PUBLIC
    ${HOME_INTERFACE}/tcl/tcl_ifp/include
)
