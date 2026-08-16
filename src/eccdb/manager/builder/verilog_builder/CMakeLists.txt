add_library(verilog_builder
    verilog_read.cpp
    verilog_write.cpp
)

find_package(ZLIB REQUIRED)

target_include_directories(verilog_builder
     PUBLIC
             ${CMAKE_CURRENT_SOURCE_DIR}
             ${HOME_DATABASE}/basic
             ${HOME_DATABASE}/data/design
             ${HOME_DATABASE}/data/design/db_design
             ${HOME_DATABASE}/data/design/db_layout
             ${HOME_DATABASE}/manager/service/def_service
             ${HOME_DATABASE}/manager/service/lef_service
             ${HOME_DATABASE}/manager/parser
             ${HOME_UTILITY}
)

target_link_libraries(verilog_builder
    PRIVATE
        verilog
        def_service
        idb
        ecc_logger
    PUBLIC
        ZLIB::ZLIB
)

if(BUILD_TESTING)
  add_executable(verilog_reader_escaped_identifier_test
      validation/verilog_reader_escaped_identifier_test.cpp
  )
  target_link_libraries(verilog_reader_escaped_identifier_test PRIVATE verilog_builder def_service idb ecc_logger)
  add_test(NAME verilog_reader_escaped_identifier_test COMMAND verilog_reader_escaped_identifier_test)
endif()
