add_library(imj_def_flattener
    ${IMJ_MODULE}/def_flattener/DefFlattener.cpp
)

target_link_libraries(imj_def_flattener
    PUBLIC
    imj_module
    imj_toolkit
    idm
)

target_include_directories(imj_def_flattener
    PUBLIC
    ${IMJ_MODULE}/def_flattener/df_data_manager
    ${IMJ_MODULE}/def_flattener
)
