if(DEBUG_ILVS_INTERFACE)
    message(STATUS "LVS: DEBUG_ILVS_INTERFACE")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "LVS: RELEASE_ILVS_INTERFACE")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ilvs_interface
    ${ILVS_INTERFACE}/LVSInterface.cpp
)

target_link_libraries(ilvs_interface
    PRIVATE
    ilvs_source
    idm
)

target_include_directories(ilvs_interface
    PUBLIC
    ${ILVS_INTERFACE}
)
