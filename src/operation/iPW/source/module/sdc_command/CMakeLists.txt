add_library(ipw_sdc_command
    # Only implemented commands are compiled and registered.  The command
    # inventory also contains source placeholders for future implementations;
    # those files stay out of this list until their behavior is complete.
    ${IPW_MODULE}/sdc_command/SdcCommand.cpp
    ${IPW_MODULE}/sdc_command/SdcCommandRegistry.cpp
    ${IPW_MODULE}/sdc_command/frontend/SdcTclCmd.cpp
    ${IPW_MODULE}/sdc_command/object/ObjectFilter.cpp
    ${IPW_MODULE}/sdc_command/object/ObjectQuery.cpp
    ${IPW_MODULE}/sdc_command/collection/RemoveFromCollection.cpp
    ${IPW_MODULE}/sdc_command/object/AllPorts.cpp
    ${IPW_MODULE}/sdc_command/object/CurrentDesign.cpp
    ${IPW_MODULE}/sdc_command/object/GetCells.cpp
    ${IPW_MODULE}/sdc_command/object/GetClocks.cpp
    ${IPW_MODULE}/sdc_command/object/GetGeneratedClocks.cpp
    ${IPW_MODULE}/sdc_command/object/GetLibCells.cpp
    ${IPW_MODULE}/sdc_command/object/GetLibPins.cpp
    ${IPW_MODULE}/sdc_command/object/GetLibs.cpp
    ${IPW_MODULE}/sdc_command/object/GetNets.cpp
    ${IPW_MODULE}/sdc_command/object/GetObjectNames.cpp
    ${IPW_MODULE}/sdc_command/object/GetPins.cpp
    ${IPW_MODULE}/sdc_command/object/GetPorts.cpp
    ${IPW_MODULE}/sdc_command/clock/CreateClock.cpp
    ${IPW_MODULE}/sdc_command/clock/CreateGeneratedClock.cpp
    ${IPW_MODULE}/sdc_command/clock/SetClockGroups.cpp
    ${IPW_MODULE}/sdc_command/clock/SetClockTransition.cpp
    ${IPW_MODULE}/sdc_command/clock/SetClockUncertainty.cpp
    ${IPW_MODULE}/sdc_command/clock/SetPropagatedClock.cpp
    ${IPW_MODULE}/sdc_command/delay/SetDrivingCell.cpp
    ${IPW_MODULE}/sdc_command/delay/SetInputDelay.cpp
    ${IPW_MODULE}/sdc_command/delay/SetInputTransition.cpp
    ${IPW_MODULE}/sdc_command/delay/SetLoad.cpp
    ${IPW_MODULE}/sdc_command/delay/SetOutputDelay.cpp
    ${IPW_MODULE}/sdc_command/design_rule/SetMaxFanout.cpp
    ${IPW_MODULE}/sdc_command/exception/PathExceptionCommand.cpp
    ${IPW_MODULE}/sdc_command/exception/ResetPath.cpp
    ${IPW_MODULE}/sdc_command/exception/SetFalsePath.cpp
    ${IPW_MODULE}/sdc_command/exception/SetMulticyclePath.cpp
    ${IPW_MODULE}/sdc_command/exception/SetPathDelay.cpp
    ${IPW_MODULE}/sdc_command/logic/SetCaseAnalysis.cpp
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
