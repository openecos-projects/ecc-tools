if(DEBUG_IFP_PHY_PLACER)
    message(STATUS "iFP: DEBUG_IFP_PHY_PLACER")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iFP: RELEASE_IFP_PHY_PLACER")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(ifp_phy_placer
    ${IFP_MODULE}/phy_placer/PhyPlacer.cpp
)

target_link_libraries(ifp_phy_placer
    PUBLIC
    ifp_data_manager
    ifp_module
    ifp_toolkit
)

target_include_directories(ifp_phy_placer
    PUBLIC
    ${IFP_MODULE}/phy_placer/pp_data_manager
    ${IFP_MODULE}/phy_placer
)
