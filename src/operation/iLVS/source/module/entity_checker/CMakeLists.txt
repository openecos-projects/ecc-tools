if(DEBUG_ILVS_ENTITY_CHECKER)
    message(STATUS "LVS: DEBUG_ILVS_ENTITY_CHECKER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_ENTITY_CHECKER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_entity_checker
    ## module entity_checker
    ${ILVS_MODULE}/entity_checker/EntityChecker.cpp
)

target_link_libraries(ilvs_entity_checker
    PUBLIC
    ilvs_data_manager
    ilvs_module
    ilvs_toolkit
)

target_include_directories(ilvs_entity_checker
    PUBLIC
    ${ILVS_MODULE}/entity_checker/ec_data_manager
    ${ILVS_MODULE}/entity_checker
)
