add_subdirectory(${IEMIR_MODULE}/graph_builder)
add_subdirectory(${IEMIR_MODULE}/ir_analyzer)
add_subdirectory(${IEMIR_MODULE}/em_analyzer)
add_subdirectory(${IEMIR_MODULE}/emir_reporter)

add_library(iemir_module INTERFACE)

target_link_libraries(iemir_module
    INTERFACE
    iemir_graph_builder
    iemir_ir_analyzer
    iemir_em_analyzer
    iemir_emir_reporter
)
