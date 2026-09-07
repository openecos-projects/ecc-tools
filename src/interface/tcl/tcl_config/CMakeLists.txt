AUX_SOURCE_DIRECTORY(./ TCL_CONFIG_SRC)

add_library(tcl_config
    ${TCL_CONFIG_SRC}
)

target_link_libraries(tcl_config
    PUBLIC
        tcl
        tool_manager
        ecc_logger
)

target_include_directories(tcl_config
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_INTERFACE}/tcl
)
