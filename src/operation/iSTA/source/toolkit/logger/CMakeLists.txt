if(DEBUG_ISTA_LOGGER)
    message(STATUS "iSTA: DEBUG_ISTA_LOGGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_LOGGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ista_logger
    ${ISTA_TOOLKIT}/logger/Logger.cpp
)

target_link_libraries(ista_logger
    PUBLIC
    ista_data_manager
)

target_include_directories(ista_logger
    PUBLIC
    ${ISTA_TOOLKIT}/logger
)
