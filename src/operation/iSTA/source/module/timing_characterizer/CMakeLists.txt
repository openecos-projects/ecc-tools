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
