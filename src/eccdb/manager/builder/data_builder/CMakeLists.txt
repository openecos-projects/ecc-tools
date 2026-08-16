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

target_link_libraries(data_process PRIVATE ecc_logger)

if(BUILD_TESTING)
  add_executable(idb_die_geometry_test
      test/idb_die_geometry_test.cpp
  )
  target_link_libraries(idb_die_geometry_test PRIVATE data_process idb geometry_db ecc_logger)
  add_test(NAME idb_die_geometry_test COMMAND idb_die_geometry_test)
endif()
