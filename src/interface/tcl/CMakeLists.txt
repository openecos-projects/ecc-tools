add_subdirectory(tcl_engine)
add_subdirectory(tcl_util)
add_subdirectory(tcl_config)
add_subdirectory(tcl_idb)
add_subdirectory(tcl_flow)
add_subdirectory(tcl_icts)
add_subdirectory(tcl_idrc)
add_subdirectory(tcl_irt)
add_subdirectory(tcl_ilvs)
add_subdirectory(tcl_ista)
add_subdirectory(tcl_ifp)
add_subdirectory(tcl_iemir)
add_subdirectory(tcl_izh)
add_subdirectory(tcl_ircx)
add_subdirectory(tcl_report)
add_subdirectory(tcl_feature)
add_subdirectory(tcl_geometry)

AUX_SOURCE_DIRECTORY(./ SRC)

add_library(ecc_tcl ${SRC})

target_link_libraries(ecc_tcl
    PUBLIC
        tool_manager
        tcl
        tcl_config
        tcl_idb
        tcl_flow
        tcl_icts
        tcl_idrc
        tcl_irt
        tcl_ilvs
        tcl_ista
        tcl_ifp
        tcl_iemir
        tcl_izh
        tcl_ircx
        tcl_report
        tcl_feature
        tcl_geometry
)

target_include_directories(ecc_tcl
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tcl_geometry

    ${CMAKE_CURRENT_SOURCE_DIR}/tcl_engine

    ${HOME_PLATFORM}/data_manager
    ${HOME_PLATFORM}/tool_manager
)
