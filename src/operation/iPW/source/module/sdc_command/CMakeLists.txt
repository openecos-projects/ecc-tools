add_library(ipw_sdc_command
    ${IPW_MODULE}/sdc_command/SdcCommand.cpp
)

target_link_libraries(ipw_sdc_command
    PUBLIC
        tcl
    PRIVATE
        ipw_data_manager
        ${CMAKE_DL_LIBS}
)

target_include_directories(ipw_sdc_command
    PUBLIC
        ${IPW_MODULE}/sdc_command
        ${IPW_TOOLKIT}/utility
)
