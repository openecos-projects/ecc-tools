if(DEBUG_IMJ_DEF_CUTTER)
    message(STATUS "MJ: DEBUG_IMJ_DEF_CUTTER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_DEF_CUTTER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_def_cutter
    ${IMJ_MODULE}/def_cutter/DEFCutter.cpp
)

target_link_libraries(imj_def_cutter
    PUBLIC
    imj_data_manager
    imj_module
    imj_toolkit
    idm
)

target_include_directories(imj_def_cutter
    PUBLIC
    ${IMJ_MODULE}/def_cutter/dc_data_manager
    ${IMJ_MODULE}/def_cutter
)
