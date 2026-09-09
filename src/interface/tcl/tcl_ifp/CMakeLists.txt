add_library(tcl_ifp
    ${HOME_INTERFACE}/tcl/tcl_ifp/src/tcl_init_fp.cpp
    ${HOME_INTERFACE}/tcl/tcl_ifp/src/tcl_run_simple_fp.cpp
    ${HOME_INTERFACE}/tcl/tcl_ifp/src/tcl_run_fp.cpp
    ${HOME_INTERFACE}/tcl/tcl_ifp/src/tcl_destroy_fp.cpp
)

target_link_libraries(tcl_ifp
    PUBLIC
    tcl
    PRIVATE
    ifp_interface
)

target_include_directories(tcl_ifp
    PUBLIC
    ${HOME_INTERFACE}/tcl/tcl_ifp/include
)
