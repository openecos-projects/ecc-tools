if(DEBUG_IRCX_SPEF_WRITER)
    message(STATUS "iRCX: DEBUG_IRCX_SPEF_WRITER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_SPEF_WRITER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_spef_writer
    ${IRCX_MODULE}/spef_writer/SPEFWriter.cpp
)

target_link_libraries(ircx_spef_writer
    PUBLIC
    ircx_data_manager
    ircx_module
    ircx_toolkit
)

target_include_directories(ircx_spef_writer
    PUBLIC
    ${IRCX_MODULE}/spef_writer/sw_data_manager
    ${IRCX_MODULE}/spef_writer
)
