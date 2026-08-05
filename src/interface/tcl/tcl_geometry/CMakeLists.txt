AUX_SOURCE_DIRECTORY(./ TCL_GEOMETRY_SRC)

add_library(tcl_geometry
    ${TCL_GEOMETRY_SRC}
)

target_link_libraries(tcl_geometry
    PUBLIC
        tcl
        idm
        geometry_builder
)

target_include_directories(tcl_geometry
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_DATABASE}/data/geometry
        ${HOME_DATABASE}/manager/builder/geometry_builder
        ${HOME_PLATFORM}/data_manager
)
