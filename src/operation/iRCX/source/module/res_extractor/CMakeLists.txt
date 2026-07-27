if(DEBUG_IRCX_RES_EXTRACTOR)
    message(STATUS "iRCX: DEBUG_IRCX_RES_EXTRACTOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_RES_EXTRACTOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_res_extractor
    ${IRCX_MODULE}/res_extractor/ResExtractor.cpp
)

target_link_libraries(ircx_res_extractor
    PUBLIC
    ircx_data_manager
    ircx_module
    ircx_toolkit
)

target_include_directories(ircx_res_extractor
    PUBLIC
    ${IRCX_MODULE}/res_extractor/re_data_manager
    ${IRCX_MODULE}/res_extractor
)
