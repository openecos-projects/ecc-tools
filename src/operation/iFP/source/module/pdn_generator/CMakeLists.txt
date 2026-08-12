if(DEBUG_IFP_PDN_GENERATOR)
    message(STATUS "iFP: DEBUG_IFP_PDN_GENERATOR")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_PDN_GENERATOR")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ifp_pdn_generator
    ${IFP_MODULE}/pdn_generator/PDNGenerator.cpp
)

target_link_libraries(ifp_pdn_generator
    PUBLIC
    ifp_data_manager
    ifp_module
    ifp_toolkit
)

target_include_directories(ifp_pdn_generator
    PUBLIC
    ${IFP_MODULE}/pdn_generator/pg_data_manager
    ${IFP_MODULE}/pdn_generator
)
