add_library(tcl_imj
    ${HOME_INTERFACE}/tcl/tcl_imj/src/tcl_check_antenna.cpp
    ${HOME_INTERFACE}/tcl/tcl_imj/src/tcl_flatten_def.cpp
    ${HOME_INTERFACE}/tcl/tcl_imj/src/tcl_destroy_mj.cpp
    ${HOME_INTERFACE}/tcl/tcl_imj/src/tcl_init_mj.cpp
    ${HOME_INTERFACE}/tcl/tcl_imj/src/tcl_insert_filler.cpp
    ${HOME_INTERFACE}/tcl/tcl_imj/src/tcl_insert_metal.cpp
)

target_link_libraries(tcl_imj
    PUBLIC
    tcl
    PRIVATE
    imj_interface
)

target_include_directories(tcl_imj
    PUBLIC
    ${HOME_INTERFACE}/tcl/tcl_imj/include
)
