add_library(ista_sdc_frontend
    SdcReader.cpp
    SdcCommand.cpp
    SdcCommandRegistry.cpp
    SdcTclCmd.cpp
    SetUnits.cpp
    WriteSdc.cpp
    query/ObjectQueryOptions.cpp
    query/RemoveFromCollection.cpp
    query/AllPorts.cpp
    query/CurrentDesign.cpp
    query/AllDelayCorners.cpp
    query/GetCells.cpp
    query/GetFan.cpp
    query/GetClocks.cpp
    query/LibertyQueries.cpp
    query/GetNets.cpp
    query/GetObjectNames.cpp
    query/GetPins.cpp
    query/GetPorts.cpp
    clock/CreateClock.cpp
    clock/CreateGeneratedClock.cpp
    clock/SetClockGroups.cpp
    clock/ClockLatency.cpp
    clock/SetClockTransition.cpp
    clock/ClockUncertainty.cpp
    clock/PropagatedClock.cpp
    environment/SetDrivingCell.cpp
    environment/SetInputDelay.cpp
    environment/SetInputTransition.cpp
    environment/SetLoad.cpp
    environment/SetOutputDelay.cpp
    environment/SetMaxFanout.cpp
    environment/SetElectricalLimit.cpp
    environment/SetTimingDerate.cpp
    environment/SetCaseAnalysis.cpp
    exception/PathExceptionCommand.cpp
    exception/ResetPath.cpp
    exception/SetFalsePath.cpp
    exception/SetMulticyclePath.cpp
    exception/SetPathDelay.cpp
)
target_link_libraries(ista_sdc_frontend
    PUBLIC tcl ista_model ista_sdc_query
    PRIVATE ista_data_manager ista_sdc_writer ${CMAKE_DL_LIBS}
)
target_include_directories(ista_sdc_frontend
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
    ${ISTA_TOOLKIT}/utility
)

add_library(ista_sdc_catalog query/ObjectCatalog.cpp)
target_link_libraries(ista_sdc_catalog PUBLIC ista_model ista_network)
target_include_directories(ista_sdc_catalog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_library(ista_sdc_query query/ObjectQuery.cpp query/ObjectFilter.cpp)
target_link_libraries(ista_sdc_query PUBLIC ista_sdc_catalog PRIVATE tcl)
target_include_directories(ista_sdc_query PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
foreach(target ista_sdc_catalog ista_sdc_query)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
