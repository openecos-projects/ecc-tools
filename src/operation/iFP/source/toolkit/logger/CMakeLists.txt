if(DEBUG_IFP_LOGGER)
    message(STATUS "iFP: DEBUG_IFP_LOGGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_LOGGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ifp_logger
    ${IFP_TOOLKIT}/logger/Logger.cpp
)

target_link_libraries(ifp_logger
    PUBLIC
    ifp_data_manager
)

target_include_directories(ifp_logger
    PUBLIC
    ${IFP_TOOLKIT}/logger
)
