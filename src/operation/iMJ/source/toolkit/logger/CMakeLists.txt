if(DEBUG_IMJ_LOGGER)
    message(STATUS "MJ: DEBUG_IMJ_LOGGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_LOGGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_logger
    ${IMJ_TOOLKIT}/logger/Logger.cpp
)

target_link_libraries(imj_logger
    PUBLIC
    imj_toolkit
)

target_include_directories(imj_logger
    PUBLIC
    ${IMJ_TOOLKIT}/logger
)
