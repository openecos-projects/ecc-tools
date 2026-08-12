if(DEBUG_IRCX_UTILITY)
    message(STATUS "iRCX: DEBUG_IRCX_UTILITY")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_UTILITY")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_utility
    ${IRCX_TOOLKIT}/utility/Utility.cpp
)

target_link_libraries(ircx_utility
    PUBLIC
    ircx_data_manager
)

target_include_directories(ircx_utility
    PUBLIC
    ${IRCX_TOOLKIT}/utility
)
