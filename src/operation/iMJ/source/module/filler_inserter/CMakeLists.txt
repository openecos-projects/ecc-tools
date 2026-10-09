if(DEBUG_IMJ_FILLER_INSERTER)
    message(STATUS "MJ: DEBUG_IMJ_FILLER_INSERTER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "MJ: RELEASE_IMJ_FILLER_INSERTER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(imj_filler_inserter
    ## module filler_inserter
    ${IMJ_MODULE}/filler_inserter/FillerInserter.cpp
)

target_link_libraries(imj_filler_inserter
    PUBLIC
    imj_module
    imj_toolkit
    idm
)

target_include_directories(imj_filler_inserter
    PUBLIC
    ${IMJ_MODULE}/filler_inserter/fi_data_manager
    ${IMJ_MODULE}/filler_inserter
)
