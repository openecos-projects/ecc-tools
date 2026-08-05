if(DEBUG_IZH_FILLER_INSERTER)
    message(STATUS "ZH: DEBUG_IZH_FILLER_INSERTER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "ZH: RELEASE_IZH_FILLER_INSERTER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(izh_filler_inserter
    ## module filler_inserter
    ${IZH_MODULE}/filler_inserter/FillerInserter.cpp
)

target_link_libraries(izh_filler_inserter
    PUBLIC
    izh_module
    izh_toolkit
    idm
)

target_include_directories(izh_filler_inserter
    PUBLIC
    ${IZH_MODULE}/filler_inserter/fi_data_manager
    ${IZH_MODULE}/filler_inserter
)
