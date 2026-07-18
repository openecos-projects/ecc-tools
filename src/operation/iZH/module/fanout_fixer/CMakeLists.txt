if(DEBUG_IZH_FANOUT_FIXER)
    message(STATUS "ZH: DEBUG_IZH_FANOUT_FIXER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "ZH: RELEASE_IZH_FANOUT_FIXER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(izh_fanout_fixer
    ## module fanout_fixer
    ${IZH_MODULE}/fanout_fixer/FanoutFixer.cpp
)

target_link_libraries(izh_fanout_fixer
    PUBLIC
    izh_module
    izh_toolkit
    idm
)

target_include_directories(izh_fanout_fixer
    PUBLIC
    ${IZH_MODULE}/fanout_fixer/ff_data_manager
    ${IZH_MODULE}/fanout_fixer
)
