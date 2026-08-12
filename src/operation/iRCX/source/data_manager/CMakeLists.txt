if(DEBUG_IRCX_DATA_MANAGER)
    message(STATUS "iRCX: DEBUG_IRCX_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_data_manager
    ${IRCX_DATA_MANAGER}/DataManager.cpp
)

target_link_libraries(ircx_data_manager
    PUBLIC
    ircx_toolkit
)

target_include_directories(ircx_data_manager
    PUBLIC
    ${IRCX_DATA_MANAGER}/advance
    ${IRCX_DATA_MANAGER}/basic
    ${IRCX_DATA_MANAGER}
)
