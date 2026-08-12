if(DEBUG_IZH_LOGGER)
    message(STATUS "ZH: DEBUG_IZH_LOGGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "ZH: RELEASE_IZH_LOGGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(izh_logger
    ${IZH_TOOLKIT}/logger/Logger.cpp
)

target_link_libraries(izh_logger
    PUBLIC
    izh_toolkit
)

target_include_directories(izh_logger
    PUBLIC
    ${IZH_TOOLKIT}/logger
)
