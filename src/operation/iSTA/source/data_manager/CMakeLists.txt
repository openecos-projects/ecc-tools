if(DEBUG_ISTA_DATA_MANAGER)
    message(STATUS "iSTA: DEBUG_ISTA_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_DATA_MANAGER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ista_data_manager
    ${ISTA_DATA_MANAGER}/DataManager.cpp
)

target_link_libraries(ista_data_manager
    PUBLIC
    ista_toolkit
    idm
    liberty
    spef
)

target_include_directories(ista_data_manager
    PUBLIC
    ${ISTA_DATA_MANAGER}/advance
    ${ISTA_DATA_MANAGER}/basic
    ${ISTA_DATA_MANAGER}
)
