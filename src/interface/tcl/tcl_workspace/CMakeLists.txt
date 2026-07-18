AUX_SOURCE_DIRECTORY(./ TCL_WORKSPACE_SRC)

add_library(tcl_workspace
    ${TCL_WORKSPACE_SRC}
)

target_link_libraries(tcl_workspace
    PUBLIC
        workspace
        tcl
        str
)

target_include_directories(tcl_workspace
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_INTERFACE}/tcl
)
