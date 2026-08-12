if(DEBUG_IFP_INTERFACE)
    message(STATUS "iFP: DEBUG_IFP_INTERFACE")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_INTERFACE")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ifp_interface
    ${IFP_INTERFACE}/FPInterface.cpp
)

target_link_libraries(ifp_interface
    PRIVATE
    ifp_source
    idm
)

target_include_directories(ifp_interface
    PUBLIC
    ${IFP_INTERFACE}
)
