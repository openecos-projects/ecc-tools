if(DEBUG_IMJ_ANTENNA_CHECKER)
    message(STATUS "MJ: DEBUG_IMJ_ANTENNA_CHECKER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_ANTENNA_CHECKER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_antenna_checker
    ## module antenna_checker
    ${IMJ_MODULE}/antenna_checker/AntennaChecker.cpp
)

target_link_libraries(imj_antenna_checker
    PUBLIC
    geometry_db
    imj_data_manager
    imj_module
    imj_toolkit
)

target_include_directories(imj_antenna_checker
    PUBLIC
    ${IMJ_MODULE}/antenna_checker/ac_data_manager
    ${IMJ_MODULE}/antenna_checker
)
