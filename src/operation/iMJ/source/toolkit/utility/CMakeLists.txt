if(DEBUG_IMJ_UTILITY)
    message(STATUS "MJ: DEBUG_IMJ_UTILITY")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_UTILITY")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_utility
    ${IMJ_TOOLKIT}/utility/Utility.cpp
)

target_link_libraries(imj_utility
    PUBLIC
    imj_toolkit
)

target_include_directories(imj_utility
    PUBLIC
    ${IMJ_TOOLKIT}/utility
)
