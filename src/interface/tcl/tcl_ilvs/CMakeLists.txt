add_library(tcl_ilvs
    ${HOME_INTERFACE}/tcl/tcl_ilvs/src/tcl_init_lvs.cpp
    ${HOME_INTERFACE}/tcl/tcl_ilvs/src/tcl_run_lvs.cpp
    ${HOME_INTERFACE}/tcl/tcl_ilvs/src/tcl_destroy_lvs.cpp
)

target_link_libraries(tcl_ilvs
    PUBLIC
    tcl_util
    PRIVATE
    ilvs_interface
    ecc_logger
)

target_include_directories(tcl_ilvs
    PUBLIC
    ${HOME_INTERFACE}/tcl/tcl_ilvs/include
)
