if(DEBUG_ILVS_UTILITY)
    message(STATUS "LVS: DEBUG_ILVS_UTILITY")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_UTILITY")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_utility
    ${ILVS_TOOLKIT}/utility/Utility.cpp
)

target_link_libraries(ilvs_utility
    PUBLIC
    ilvs_data_manager
)

target_include_directories(ilvs_utility
    PUBLIC
    ${ILVS_TOOLKIT}/utility
)
