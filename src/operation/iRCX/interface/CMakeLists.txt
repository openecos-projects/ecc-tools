if(DEBUG_IRCX_INTERFACE)
    message(STATUS "iRCX: DEBUG_IRCX_INTERFACE")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_INTERFACE")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_interface
    ${IRCX_INTERFACE}/RCXInterface.cpp
)

add_subdirectory(${IRCX_INTERFACE}/ics55)
add_subdirectory(${IRCX_INTERFACE}/aux)

target_link_libraries(ircx_interface
    PRIVATE
    ircx_aux
    ircx_source
    idm
)

target_include_directories(ircx_interface
    PUBLIC
    ${IRCX_INTERFACE}
    ${IRCX_SOURCE}/data_manager/advance
)
