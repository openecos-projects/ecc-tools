add_library(view_builder
    view_write.cpp
    view_geometry_transform.cpp
    view_json_edit_apply.cpp
    view_json_writer.cpp
)

target_include_directories(view_builder
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_DATABASE}/basic/geometry
        ${HOME_DATABASE}/data/design
        ${HOME_DATABASE}/data/design/db_design
        ${HOME_DATABASE}/data/design/db_layout
        ${HOME_DATABASE}/manager/service/def_service
        ${HOME_DATABASE}/manager/service/lef_service
        ${HOME_THIRDPARTY}/json
)

target_link_libraries(view_builder PRIVATE idb)
