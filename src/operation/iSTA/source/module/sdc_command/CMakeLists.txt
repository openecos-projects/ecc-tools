add_library(ista_sdc_command
    # Only implemented commands are compiled and registered.  The command
    # inventory also contains source placeholders for future implementations;
    # those files stay out of this list until their behavior is complete.
    ${ISTA_MODULE}/sdc_command/SdcCommand.cpp
    ${ISTA_MODULE}/sdc_command/SdcCommandRegistry.cpp
    ${ISTA_MODULE}/sdc_command/frontend/SdcTclCmd.cpp
    ${ISTA_MODULE}/sdc_command/object/ObjectFilter.cpp
    ${ISTA_MODULE}/sdc_command/object/ObjectQuery.cpp
    ${ISTA_MODULE}/sdc_command/collection/RemoveFromCollection.cpp
    ${ISTA_MODULE}/sdc_command/object/AllPorts.cpp
    ${ISTA_MODULE}/sdc_command/object/CurrentDesign.cpp
    ${ISTA_MODULE}/sdc_command/object/GetCells.cpp
    ${ISTA_MODULE}/sdc_command/object/GetClocks.cpp
    ${ISTA_MODULE}/sdc_command/object/GetGeneratedClocks.cpp
    ${ISTA_MODULE}/sdc_command/object/GetLibCells.cpp
    ${ISTA_MODULE}/sdc_command/object/GetLibPins.cpp
    ${ISTA_MODULE}/sdc_command/object/GetLibs.cpp
    ${ISTA_MODULE}/sdc_command/object/GetNets.cpp
    ${ISTA_MODULE}/sdc_command/object/GetObjectNames.cpp
    ${ISTA_MODULE}/sdc_command/object/GetPins.cpp
    ${ISTA_MODULE}/sdc_command/object/GetPorts.cpp
    ${ISTA_MODULE}/sdc_command/clock/CreateClock.cpp
    ${ISTA_MODULE}/sdc_command/clock/CreateGeneratedClock.cpp
    ${ISTA_MODULE}/sdc_command/clock/SetClockGroups.cpp
    ${ISTA_MODULE}/sdc_command/clock/SetClockTransition.cpp
    ${ISTA_MODULE}/sdc_command/clock/SetClockUncertainty.cpp
    ${ISTA_MODULE}/sdc_command/clock/SetPropagatedClock.cpp
    ${ISTA_MODULE}/sdc_command/delay/SetDrivingCell.cpp
    ${ISTA_MODULE}/sdc_command/delay/SetInputDelay.cpp
    ${ISTA_MODULE}/sdc_command/delay/SetInputTransition.cpp
    ${ISTA_MODULE}/sdc_command/delay/SetLoad.cpp
    ${ISTA_MODULE}/sdc_command/delay/SetOutputDelay.cpp
    ${ISTA_MODULE}/sdc_command/design_rule/SetMaxFanout.cpp
    ${ISTA_MODULE}/sdc_command/exception/PathExceptionCommand.cpp
    ${ISTA_MODULE}/sdc_command/exception/ResetPath.cpp
    ${ISTA_MODULE}/sdc_command/exception/SetFalsePath.cpp
    ${ISTA_MODULE}/sdc_command/exception/SetMulticyclePath.cpp
    ${ISTA_MODULE}/sdc_command/exception/SetPathDelay.cpp
    ${ISTA_MODULE}/sdc_command/logic/SetCaseAnalysis.cpp
)

target_link_libraries(ista_sdc_command
    PUBLIC
        tcl
    PRIVATE
        ista_data_manager
        ${CMAKE_DL_LIBS}
)

target_include_directories(ista_sdc_command
    PUBLIC
        ${ISTA_MODULE}/sdc_command
        ${ISTA_TOOLKIT}/utility
)
