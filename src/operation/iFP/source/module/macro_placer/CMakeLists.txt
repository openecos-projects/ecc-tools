if(DEBUG_IFP_MACRO_PLACER)
    message(STATUS "iFP: DEBUG_IFP_MACRO_PLACER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_MACRO_PLACER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ifp_macro_placer
    ${IFP_MODULE}/macro_placer/MacroPlacer.cpp
)

target_link_libraries(ifp_macro_placer
    PUBLIC
    ifp_data_manager
    ifp_module
    ifp_toolkit
)

target_include_directories(ifp_macro_placer
    PUBLIC
    ${IFP_MODULE}/macro_placer/mp_data_manager
    ${IFP_MODULE}/macro_placer
)
