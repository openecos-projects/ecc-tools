if(DEBUG_ILVS_PDN_CHECKER)
    message(STATUS "LVS: DEBUG_ILVS_PDN_CHECKER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_PDN_CHECKER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_pdn_checker
    ## module pdn_checker
    ${ILVS_MODULE}/pdn_checker/PDNChecker.cpp
)

target_link_libraries(ilvs_pdn_checker
    PUBLIC
    ilvs_data_manager
    ilvs_module
    ilvs_toolkit
)

target_include_directories(ilvs_pdn_checker
    PUBLIC
    ${ILVS_MODULE}/pdn_checker/pc_data_manager
    ${ILVS_MODULE}/pdn_checker
)
