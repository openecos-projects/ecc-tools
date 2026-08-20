add_library(tcl_ista
    ${HOME_INTERFACE}/tcl/tcl_ista/src/tcl_init_sta.cpp
    ${HOME_INTERFACE}/tcl/tcl_ista/src/tcl_run_sta.cpp
    ${HOME_INTERFACE}/tcl/tcl_ista/src/tcl_extract_lib.cpp
    ${HOME_INTERFACE}/tcl/tcl_ista/src/tcl_destroy_sta.cpp
)

target_link_libraries(tcl_ista
    PUBLIC
    tool_manager
    tcl_util
    PRIVATE
    ista_interface
)

target_include_directories(tcl_ista
    PUBLIC
    ${HOME_INTERFACE}/tcl/tcl_ista/include
)
