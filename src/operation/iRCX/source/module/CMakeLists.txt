add_subdirectory(${IRCX_MODULE}/topo_builder)
add_subdirectory(${IRCX_MODULE}/env_builder)
add_subdirectory(${IRCX_MODULE}/var_processor)
add_subdirectory(${IRCX_MODULE}/res_extractor)
add_subdirectory(${IRCX_MODULE}/cap_extractor)
add_subdirectory(${IRCX_MODULE}/spef_writer)

add_library(ircx_module INTERFACE)

target_link_libraries(ircx_module
    INTERFACE
    ircx_topo_builder
    ircx_env_builder
    ircx_var_processor
    ircx_res_extractor
    ircx_cap_extractor
    ircx_spef_writer
)
