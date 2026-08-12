if(DEBUG_IRCX_VAR_PROCESSOR)
    message(STATUS "iRCX: DEBUG_IRCX_VAR_PROCESSOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_VAR_PROCESSOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_var_processor
    ${IRCX_MODULE}/var_processor/VarProcessor.cpp
)

target_link_libraries(ircx_var_processor
    PUBLIC
    ircx_data_manager
    ircx_module
    ircx_toolkit
)

target_include_directories(ircx_var_processor
    PUBLIC
    ${IRCX_MODULE}/var_processor/vp_data_manager
    ${IRCX_MODULE}/var_processor
)
