if(DEBUG_ILVS_LOGGER)
    message(STATUS "LVS: DEBUG_ILVS_LOGGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_LOGGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_logger
    ${ILVS_TOOLKIT}/logger/Logger.cpp
)

target_link_libraries(ilvs_logger
    PUBLIC
    ilvs_data_manager
)

target_include_directories(ilvs_logger
    PUBLIC
    ${ILVS_TOOLKIT}/logger
)
