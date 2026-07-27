if(DEBUG_IEMIR_LOGGER)
    message(STATUS "iEMIR: DEBUG_IEMIR_LOGGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iEMIR: RELEASE_IEMIR_LOGGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(iemir_logger
    ${IEMIR_TOOLKIT}/logger/Logger.cpp
)

target_link_libraries(iemir_logger
    PUBLIC
    iemir_data_manager
)

target_include_directories(iemir_logger
    PUBLIC
    ${IEMIR_TOOLKIT}/logger
)
