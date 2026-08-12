if(DEBUG_IRCX_CAP_EXTRACTOR)
    message(STATUS "iRCX: DEBUG_IRCX_CAP_EXTRACTOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_CAP_EXTRACTOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_cap_extractor
    ${IRCX_MODULE}/cap_extractor/CapExtractor.cpp
)

target_link_libraries(ircx_cap_extractor
    PUBLIC
    ircx_data_manager
    ircx_module
    ircx_toolkit
)

target_include_directories(ircx_cap_extractor
    PUBLIC
    ${IRCX_MODULE}/cap_extractor/ce_data_manager
    ${IRCX_MODULE}/cap_extractor
)
