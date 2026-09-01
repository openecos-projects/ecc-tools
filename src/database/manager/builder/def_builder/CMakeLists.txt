add_library(def_builder
    def_read.cpp
    def_write.cpp
)

target_include_directories(def_builder
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_DATABASE}/data/design
        ${HOME_DATABASE}/data/design/db_design
        ${HOME_DATABASE}/data/design/db_layout
        ${HOME_DATABASE}/manager/service/def_service
        ${HOME_DATABASE}/manager/service/lef_service
        ${HOME_DATABASE}/basic/geometry
)

target_include_directories(def_builder
    SYSTEM PUBLIC
        ${HOME_THIRDPARTY}/lefdef/def
        ${HOME_THIRDPARTY}/lefdef/def/def
        ${HOME_THIRDPARTY}/lefdef/def/defzlib
)

target_link_libraries(def_builder PRIVATE def defzlib geometry_db ecc_logger)

if(BUILD_TESTING)
  add_executable(def_writer_escaped_name_test
      validation/def_writer_escaped_name_test.cpp
  )
  target_link_libraries(def_writer_escaped_name_test PRIVATE def_builder def_service idb ecc_logger)
  add_test(NAME def_writer_escaped_name_test COMMAND def_writer_escaped_name_test)

  add_executable(def_reader_error_test
      validation/def_reader_error_test.cpp
  )
  target_link_libraries(def_reader_error_test PRIVATE def_builder def_service idb ecc_logger)
  add_test(NAME def_reader_error_test COMMAND def_reader_error_test)
endif()
