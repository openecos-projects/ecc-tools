if(DEBUG_IZH_UTILITY)
    message(STATUS "ZH: DEBUG_IZH_UTILITY")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "ZH: RELEASE_IZH_UTILITY")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(izh_utility
    ${IZH_TOOLKIT}/utility/Utility.cpp
)

target_link_libraries(izh_utility
    PUBLIC
    izh_toolkit
)

target_include_directories(izh_utility
    PUBLIC
    ${IZH_TOOLKIT}/utility
)
