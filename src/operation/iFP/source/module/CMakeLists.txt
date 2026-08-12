add_subdirectory(${IFP_MODULE}/die_builder)
add_subdirectory(${IFP_MODULE}/io_placer)
add_subdirectory(${IFP_MODULE}/macro_placer)
add_subdirectory(${IFP_MODULE}/pdn_generator)
add_subdirectory(${IFP_MODULE}/phy_placer)

add_library(ifp_module INTERFACE)

target_link_libraries(ifp_module
    INTERFACE
    ifp_die_builder
    ifp_io_placer
    ifp_macro_placer
    ifp_pdn_generator
    ifp_phy_placer
)
