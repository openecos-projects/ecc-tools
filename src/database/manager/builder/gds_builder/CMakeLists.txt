add_library(gds_builder
    gds_write.cpp
)

target_include_directories(gds_builder 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_DATABASE}/data/design
        ${HOME_DATABASE}/data/design/db_design
        ${HOME_DATABASE}/data/design/db_layout
        ${HOME_DATABASE}/manager/service/def_service
        ${HOME_DATABASE}/manager/service/lef_service
)

target_link_libraries(gds_builder PRIVATE idb solver_geometry_boost gdstk::gdstk)
