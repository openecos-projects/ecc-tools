if(DEBUG_IFP_DIE_BUILDER)
    message(STATUS "iFP: DEBUG_IFP_DIE_BUILDER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_DIE_BUILDER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ifp_die_builder
    ${IFP_MODULE}/die_builder/DieBuilder.cpp
)

target_link_libraries(ifp_die_builder
    PUBLIC
    ifp_data_manager
    ifp_module
    ifp_toolkit
)

target_include_directories(ifp_die_builder
    PUBLIC
    ${IFP_MODULE}/die_builder/db_data_manager
    ${IFP_MODULE}/die_builder
)
