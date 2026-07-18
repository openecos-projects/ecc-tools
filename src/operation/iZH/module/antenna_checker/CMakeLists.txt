if(DEBUG_IZH_ANTENNA_CHECKER)
    message(STATUS "ZH: DEBUG_IZH_ANTENNA_CHECKER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "ZH: RELEASE_IZH_ANTENNA_CHECKER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(izh_antenna_checker
    ## module antenna_checker
    ${IZH_MODULE}/antenna_checker/AntennaChecker.cpp
)

target_link_libraries(izh_antenna_checker
    PUBLIC
    izh_module
    izh_toolkit
)

target_include_directories(izh_antenna_checker
    PUBLIC
    ${IZH_MODULE}/antenna_checker/ac_data_manager
    ${IZH_MODULE}/antenna_checker
)
