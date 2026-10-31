add_library(ipw_sdc_command
    ${IPW_MODULE}/sdc_command/SdcCommand.cpp
    ${IPW_MODULE}/sdc_command/object/ClockTargets.cpp
    ${IPW_MODULE}/sdc_command/clock/ClockUncertaintyStore.cpp
    ${IPW_MODULE}/sdc_command/SetUnits.cpp
    ${IPW_MODULE}/sdc_command/WriteSdc.cpp
    ${IPW_MODULE}/sdc_command/environment/SetElectricalLimit.cpp
    ${IPW_MODULE}/sdc_command/environment/SetTimingDerate.cpp
    ${IPW_MODULE}/sdc_command/writer/SdcObjectWriter.cpp
    ${IPW_MODULE}/sdc_command/writer/SdcWriter.cpp

    ${IPW_MODULE}/sdc_command/clock/SetClockLatency.cpp
    ${IPW_MODULE}/sdc_command/clock/SetClockUncertainty.cpp
    ${IPW_MODULE}/sdc_command/clock/SetClockGroups.cpp
    ${IPW_MODULE}/sdc_command/clock/RemovePropagatedClock.cpp
    ${IPW_MODULE}/sdc_command/delay/SetInputDelay.cpp
    ${IPW_MODULE}/sdc_command/delay/SetOutputDelay.cpp
    ${IPW_MODULE}/sdc_command/design_rule/SetMaxFanout.cpp
    ${IPW_MODULE}/sdc_command/exception/PathExceptionCommand.cpp
    ${IPW_MODULE}/sdc_command/exception/SetFalsePath.cpp
    ${IPW_MODULE}/sdc_command/exception/SetPathDelay.cpp
    ${IPW_MODULE}/sdc_command/exception/SetMulticyclePath.cpp
    ${IPW_MODULE}/sdc_command/exception/ResetPath.cpp
    ${IPW_MODULE}/sdc_command/object/GetLibs.cpp
    ${IPW_MODULE}/sdc_command/object/GetLibCells.cpp
    ${IPW_MODULE}/sdc_command/object/GetLibPins.cpp

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
