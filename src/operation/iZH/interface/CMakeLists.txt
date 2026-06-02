if(DEBUG_IZH_INTERFACE)
    message(STATUS "ZH: DEBUG_IZH_INTERFACE")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "ZH: RELEASE_IZH_INTERFACE")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(izh_interface
    ${IZH_INTERFACE}/ZHInterface.cpp
)

target_link_libraries(izh_interface
    PRIVATE
    izh_toolkit
    idrc_interface
    ieda_feature
    idm
    notification
)

target_include_directories(izh_interface
    PUBLIC
    ${IZH_INTERFACE}
)
