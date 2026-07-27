if(DEBUG_IEMIR_DATA_MANAGER)
    message(STATUS "iEMIR: DEBUG_IEMIR_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iEMIR: RELEASE_IEMIR_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(iemir_data_manager
    ${IEMIR_DATA_MANAGER}/DataManager.cpp
)

target_link_libraries(iemir_data_manager
    PUBLIC
    iemir_toolkit
)

target_include_directories(iemir_data_manager
    PUBLIC
    ${IEMIR_DATA_MANAGER}/advance
    ${IEMIR_DATA_MANAGER}
)
