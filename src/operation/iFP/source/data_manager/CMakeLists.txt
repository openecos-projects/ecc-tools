if(DEBUG_IFP_DATA_MANAGER)
    message(STATUS "iFP: DEBUG_IFP_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ifp_data_manager
    ${IFP_DATA_MANAGER}/DataManager.cpp
)

target_link_libraries(ifp_data_manager
    PUBLIC
    ifp_toolkit
)

target_include_directories(ifp_data_manager
    PUBLIC
    ${IFP_DATA_MANAGER}/basic
    ${IFP_DATA_MANAGER}/advance
    ${IFP_DATA_MANAGER}
)
