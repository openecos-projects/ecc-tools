add_subdirectory(${IZH_MODULE}/antenna_checker)
add_subdirectory(${IZH_MODULE}/fanout_fixer)
add_subdirectory(${IZH_MODULE}/filler_inserter)

add_library(izh_module INTERFACE)

target_link_libraries(izh_module
    INTERFACE
    izh_antenna_checker
    izh_fanout_fixer
    izh_filler_inserter
)
