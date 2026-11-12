if(DEBUG_IMJ_DRIVER_ANALYZER)
    message(STATUS "MJ: DEBUG_IMJ_DRIVER_ANALYZER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_DRIVER_ANALYZER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_driver_analyzer
    ${IMJ_MODULE}/driver_analyzer/DriverAnalyzer.cpp
)

target_link_libraries(imj_driver_analyzer
    PUBLIC
    imj_data_manager
    imj_module
    imj_toolkit
)

target_include_directories(imj_driver_analyzer
    PUBLIC
    ${IMJ_MODULE}/driver_analyzer/da_data_manager
    ${IMJ_MODULE}/driver_analyzer
)
