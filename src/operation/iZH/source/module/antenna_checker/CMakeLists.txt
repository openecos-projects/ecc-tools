if(DEBUG_IZH_ANTENNA_CHECKER OR DEBUG_IZH_ANTENNA_FIXER)
    message(STATUS "ZH: DEBUG_IZH_ANTENNA_CHECKER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "ZH: RELEASE_IZH_ANTENNA_CHECKER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(izh_antenna_checker
    ## module antenna_checker
    ${IZH_MODULE}/antenna_checker/AntennaChecker.cpp
    ${IZH_MODULE}/antenna_checker/AntennaRuleEvaluator.cpp
    ${IZH_MODULE}/antenna_checker/AntennaPathAnalyzer.cpp
    ${IZH_MODULE}/antenna_checker/AntennaGeometry.cpp
    ${IZH_MODULE}/antenna_checker/AntennaEngine.cpp
    ${IZH_MODULE}/antenna_checker/AntennaFixer.cpp
    ${IZH_MODULE}/antenna_checker/RoutingContext.cpp
    ${IZH_MODULE}/antenna_checker/WireEnvIndex.cpp
)

target_link_libraries(izh_antenna_checker
    PUBLIC
    geometry_db
    izh_module
    izh_toolkit
)

target_include_directories(izh_antenna_checker
    PUBLIC
    ${IZH_MODULE}/antenna_checker/ac_data_manager
    ${IZH_MODULE}/antenna_checker
)
