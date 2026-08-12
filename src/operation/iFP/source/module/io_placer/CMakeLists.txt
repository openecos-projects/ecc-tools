if(DEBUG_IFP_IO_PLACER)
    message(STATUS "iFP: DEBUG_IFP_IO_PLACER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_IO_PLACER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ifp_io_placer
    ${IFP_MODULE}/io_placer/IOPlacer.cpp
)

target_link_libraries(ifp_io_placer
    PUBLIC
    ifp_data_manager
    ifp_module
    ifp_toolkit
)

target_include_directories(ifp_io_placer
    PUBLIC
    ${IFP_MODULE}/io_placer/ip_data_manager
    ${IFP_MODULE}/io_placer
)
