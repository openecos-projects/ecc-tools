if(DEBUG_ILVS_SNAPSHOT_IO)
    message(STATUS "LVS: DEBUG_ILVS_SNAPSHOT_IO")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_SNAPSHOT_IO")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_snapshot_io
    ## module snapshot_io
    ${ILVS_MODULE}/snapshot_io/LVSSnapshotIO.cpp
)

target_link_libraries(ilvs_snapshot_io
    PUBLIC
    ilvs_data_manager
    ilvs_module
    ilvs_toolkit
)

target_include_directories(ilvs_snapshot_io
    PUBLIC
    ${ILVS_MODULE}/snapshot_io
    ${ILVS_DATA_MANAGER}/advance
)
