add_library(data_process
    header.cpp
    layout_write.cpp
    layout_read.cpp
    design_write.cpp
    design_read.cpp
)

target_include_directories(data_process 
PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_DATABASE}/data/design
        ${HOME_DATABASE}/data/design/db_design
        ${HOME_DATABASE}/data/design/db_layout
        ${HOME_DATABASE}/basic/geometry
        ${HOME_DATABASE}/manager/service/def_service
)

if(BUILD_TESTING)
    add_executable(idb_data_binary_roundtrip_test test/data_binary_roundtrip_test.cpp)
    target_link_libraries(idb_data_binary_roundtrip_test PRIVATE data_process idb geometry_db str)
    add_test(NAME idb_data_binary_roundtrip_test COMMAND idb_data_binary_roundtrip_test)
endif()
