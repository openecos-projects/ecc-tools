if(DEBUG_ISTA_TIMING_CHARACTERIZER)
    message(STATUS "iSTA: DEBUG_ISTA_TIMING_CHARACTERIZER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iSTA: RELEASE_ISTA_TIMING_CHARACTERIZER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ista_timing_characterizer
    ${ISTA_MODULE}/timing_characterizer/TimingCharacterizer.cpp
)

target_link_libraries(ista_timing_characterizer
    PUBLIC
    ista_data_manager
    ista_toolkit
)

target_include_directories(ista_timing_characterizer
    PUBLIC
    ${ISTA_MODULE}/timing_characterizer/tc_data_manager
    ${ISTA_MODULE}/timing_characterizer
)
