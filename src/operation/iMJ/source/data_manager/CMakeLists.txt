if(DEBUG_IMJ_DATA_MANAGER)
    message(STATUS "MJ: DEBUG_IMJ_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_data_manager
    ${IMJ_DATA_MANAGER}/DataManager.cpp
)

target_link_libraries(imj_data_manager
    PUBLIC
    imj_toolkit
)

target_include_directories(imj_data_manager
    PUBLIC
    ${IMJ_DATA_MANAGER}/advance
    ${IMJ_DATA_MANAGER}
)
