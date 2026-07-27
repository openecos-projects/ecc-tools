if(DEBUG_IRCX_LOGGER)
    message(STATUS "iRCX: DEBUG_IRCX_LOGGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_LOGGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_logger
    ${IRCX_TOOLKIT}/logger/Logger.cpp
)

target_link_libraries(ircx_logger
    PUBLIC
    ircx_data_manager
)

target_include_directories(ircx_logger
    PUBLIC
    ${IRCX_TOOLKIT}/logger
)
