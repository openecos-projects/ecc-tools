if(DEBUG_ILVS_LVS_CHECKER)
    message(STATUS "LVS: DEBUG_ILVS_LVS_CHECKER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_LVS_CHECKER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_lvs_checker
    ## module lvs_checker
    ${ILVS_MODULE}/lvs_checker/LVSChecker.cpp
)

target_link_libraries(ilvs_lvs_checker
    PUBLIC
    ilvs_data_manager
    ilvs_module
    ilvs_toolkit
)

target_include_directories(ilvs_lvs_checker
    PUBLIC
    ${ILVS_MODULE}/lvs_checker
    ${ILVS_DATA_MANAGER}/advance
)
