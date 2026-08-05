if(DEBUG_IFP_UTILITY)
    message(STATUS "iFP: DEBUG_IFP_UTILITY")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_UTILITY")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ifp_utility
    ${IFP_TOOLKIT}/utility/Utility.cpp
)

target_link_libraries(ifp_utility
    PUBLIC
    ifp_data_manager
    ifp_logger
)

target_include_directories(ifp_utility
    PUBLIC
    ${IFP_TOOLKIT}/utility
)
