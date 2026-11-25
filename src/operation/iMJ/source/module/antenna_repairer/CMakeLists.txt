if(DEBUG_IMJ_ANTENNA_REPAIRER)
    message(STATUS "MJ: DEBUG_IMJ_ANTENNA_REPAIRER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_ANTENNA_REPAIRER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_antenna_repairer
    ${IMJ_MODULE}/antenna_repairer/ARReporter.cpp
    ${IMJ_MODULE}/antenna_repairer/AntennaRepairer.cpp
    ${IMJ_MODULE}/antenna_repairer/DiodeLibrary.cpp
    ${IMJ_MODULE}/antenna_repairer/LocalStubRouter.cpp
    ${IMJ_MODULE}/antenna_repairer/PolygonDrcRules.cpp
    ${IMJ_MODULE}/antenna_repairer/RepairTransaction.cpp
    ${IMJ_MODULE}/antenna_repairer/RowSiteGrid.cpp
    ${IMJ_MODULE}/antenna_repairer/StubMazeSearch.cpp
)

target_link_libraries(imj_antenna_repairer
    PUBLIC
    geometry_builder
    imj_antenna_checker
    imj_data_manager
    imj_toolkit
    idm
)

target_include_directories(imj_antenna_repairer
    PUBLIC
    ${IMJ_MODULE}/antenna_repairer
    ${IMJ_MODULE}/antenna_repairer/ar_data_manager
)
