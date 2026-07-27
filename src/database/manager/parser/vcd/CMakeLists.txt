cmake_minimum_required(VERSION 3.11)

add_library(vcd
  VcdParser.cc
)

target_include_directories(vcd
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(test_vcd
  test/VcdParserTest.cpp
)
target_compile_definitions(test_vcd
  PRIVATE
    VCD_TEST_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/test1.vcd"
)
target_link_libraries(test_vcd vcd)
