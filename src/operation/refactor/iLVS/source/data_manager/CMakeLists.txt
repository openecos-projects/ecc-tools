if(DEBUG_ILVS_DATA_MANAGER)
    message(STATUS "LVS: DEBUG_ILVS_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_data_manager
    ${ILVS_DATA_MANAGER}/DataManager.cpp
)

target_link_libraries(ilvs_data_manager
    PUBLIC
    ilvs_toolkit
)

target_include_directories(ilvs_data_manager
    PUBLIC
    ${ILVS_DATA_MANAGER}/advance
    ${ILVS_DATA_MANAGER}
)
