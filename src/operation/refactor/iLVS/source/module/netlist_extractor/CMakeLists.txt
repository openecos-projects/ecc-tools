if(DEBUG_ILVS_NETLIST_EXTRACTOR)
    message(STATUS "LVS: DEBUG_ILVS_NETLIST_EXTRACTOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_NETLIST_EXTRACTOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_netlist_extractor
    ## module netlist_extractor
    ${ILVS_MODULE}/netlist_extractor/NetlistExtractor.cpp
)

target_link_libraries(ilvs_netlist_extractor
    PUBLIC
    ilvs_data_manager
    ilvs_module
    ilvs_toolkit
    PRIVATE
    idm
)

target_include_directories(ilvs_netlist_extractor
    PUBLIC
    ${ILVS_MODULE}/netlist_extractor
    ${ILVS_DATA_MANAGER}/advance
)
