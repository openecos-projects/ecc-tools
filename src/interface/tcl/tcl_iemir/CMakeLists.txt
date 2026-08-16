add_library(tcl_iemir
    ${HOME_INTERFACE}/tcl/tcl_iemir/src/tcl_init_emir.cpp
    ${HOME_INTERFACE}/tcl/tcl_iemir/src/tcl_run_emir.cpp
    ${HOME_INTERFACE}/tcl/tcl_iemir/src/tcl_destroy_emir.cpp
)

target_link_libraries(tcl_iemir
    PUBLIC
    tcl_util
    PRIVATE
    iemir_interface
)

target_include_directories(tcl_iemir
    PUBLIC
    ${HOME_INTERFACE}/tcl/tcl_iemir/include
)
