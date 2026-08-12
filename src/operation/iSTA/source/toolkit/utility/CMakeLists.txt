if(DEBUG_ISTA_UTILITY)
    message(STATUS "iSTA: DEBUG_ISTA_UTILITY")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_UTILITY")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ista_utility
    ${ISTA_TOOLKIT}/utility/Utility.cpp
)

target_link_libraries(ista_utility
    PUBLIC
    ista_data_manager
)

target_include_directories(ista_utility
    PUBLIC
    ${ISTA_TOOLKIT}/utility
)
