if(DEBUG_IMJ_INTERFACE)
    message(STATUS "MJ: DEBUG_IMJ_INTERFACE")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_INTERFACE")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_interface
    ${IMJ_INTERFACE}/MJInterface.cpp
)

target_link_libraries(imj_interface
    PRIVATE
    imj_source
    idrc_interface
    ecc_feature
    idm
)

target_include_directories(imj_interface
    PUBLIC
    ${IMJ_INTERFACE}
)
