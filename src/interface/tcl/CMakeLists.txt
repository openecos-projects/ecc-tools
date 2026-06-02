add_subdirectory(tcl_util)
add_subdirectory(tcl_workspace)
add_subdirectory(tcl_config)
add_subdirectory(tcl_idb)
add_subdirectory(tcl_flow)
add_subdirectory(tcl_icts)
add_subdirectory(tcl_idrc)
add_subdirectory(tcl_instance)
add_subdirectory(tcl_irt)
add_subdirectory(tcl_izh)
add_subdirectory(tcl_ircx)
add_subdirectory(tcl_ifp)
add_subdirectory(tcl_ipdn)
add_subdirectory(tcl_ista)
add_subdirectory(tcl_report)
add_subdirectory(tcl_feature)
add_subdirectory(tcl_eval)
add_subdirectory(tcl_vec)
add_subdirectory(tcl_notification)

if(CONTEST)
add_subdirectory(tcl_contest)
endif()

AUX_SOURCE_DIRECTORY(./ SRC)

add_library(ieda_tcl ${SRC})

target_link_libraries(ieda_tcl 
    PUBLIC 
        flow 
        tool_manager
        tcl 
        shell-cmd 
        tcl_workspace
        tcl_config
        tcl_idb
        tcl_flow
        tcl_icts
        tcl_idrc
        tcl_irt
        tcl_izh
        tcl_ircx 
        tcl_ifp 
        tcl_ipdn
        tcl_inst 
        tcl_ista
        tcl_report
        tcl_feature
        tcl_eval
        tcl_vec
        tcl_notification
)

target_include_directories(ieda_tcl 
    PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tcl_workspace

    ${HOME_UTILITY}/tcl

    ${HOME_PLATFORM}/flow
    ${HOME_PLATFORM}/data_manager
    ${HOME_PLATFORM}/tool_manager
    ${HOME_PLATFORM}/tool_manager/tool_api
)

if(CONTEST)
  target_link_libraries(ieda_tcl PUBLIC tcl_contest)

  target_include_directories(ieda_tcl 
    PUBLIC 
    ${HOME_INTERFACE}/tcl/tcl_contest
  )
endif()
