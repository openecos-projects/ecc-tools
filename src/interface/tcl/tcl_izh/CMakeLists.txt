add_library(tcl_izh
    ${HOME_INTERFACE}/tcl/tcl_izh/src/tcl_zh_fix_fanout.cpp
    ${HOME_INTERFACE}/tcl/tcl_izh/src/tcl_zh_insert_filler.cpp
)

target_link_libraries(tcl_izh
    PUBLIC
    tool_manager
    tcl_util
    PRIVATE
    izh_interface
)

target_include_directories(tcl_izh
    PUBLIC
    ${HOME_INTERFACE}/tcl/tcl_izh/include
)
