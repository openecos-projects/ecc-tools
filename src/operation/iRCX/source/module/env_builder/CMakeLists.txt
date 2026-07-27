if(DEBUG_IRCX_ENV_BUILDER)
    message(STATUS "iRCX: DEBUG_IRCX_ENV_BUILDER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iRCX: RELEASE_IRCX_ENV_BUILDER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ircx_env_builder
    ${IRCX_MODULE}/env_builder/EnvBuilder.cpp
)

target_link_libraries(ircx_env_builder
    PUBLIC
    ircx_data_manager
    ircx_module
    ircx_toolkit
)

target_include_directories(ircx_env_builder
    PUBLIC
    ${IRCX_MODULE}/env_builder/eb_data_manager
    ${IRCX_MODULE}/env_builder
)
