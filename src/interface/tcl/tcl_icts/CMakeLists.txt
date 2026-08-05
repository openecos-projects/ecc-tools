AUX_SOURCE_DIRECTORY(./ TCL_ICTS_SRC)

add_library(tcl_icts
    ${TCL_ICTS_SRC}
)

target_link_libraries(tcl_icts
    PUBLIC
        tcl_util
        tcl
        icts_interface
    PRIVATE
        ecc_logger
)

target_include_directories(tcl_icts
    PUBLIC
        ${HOME_INTERFACE}/tcl
        ${CMAKE_CURRENT_SOURCE_DIR}
)
