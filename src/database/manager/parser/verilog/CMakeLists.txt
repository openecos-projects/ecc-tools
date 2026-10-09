cmake_minimum_required(VERSION 3.11)

set (CMAKE_CXX_STANDARD 20)

set(CMAKE_BUILD_TYPE "Release")

if(BUILD_STATIC_LIB)
  # This is a workaround on cmake 3.22.1, should ensure static libraries are used
  # Note: There is a better approach in newer versions of CMake:
  # set(ZLIB_USE_STATIC_LIBS ON CACHE BOOL "Use static zlib" FORCE)

  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

  find_package(ZLIB REQUIRED)

  set(CMAKE_FIND_LIBRARY_SUFFIXES ${ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})

  if(ZLIB_LIBRARIES MATCHES "\\.so(\\.|$)")
    message(FATAL_ERROR "Found dynamic zlib, but static required: ${ZLIB_LIBRARIES}")
  endif()

  message(STATUS "Linking static zlib: ${ZLIB_LIBRARIES}")
else()
  find_package(ZLIB REQUIRED)
endif()

add_library(verilog
  VerilogIdentifier.cc
  VerilogLexer.cc
  VerilogParser.cc
  VerilogConstEval.cc
  VerilogElaborator.cc
  VerilogValue.cc
)
target_link_libraries(verilog PRIVATE ${ZLIB_LIBRARIES})
target_include_directories(verilog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(BUILD_TESTING)
  foreach(test IN ITEMS EscapedIdentifier LargeModule SyntaxSemantics Value ConstEval Input)
    string(REGEX REPLACE "([a-z])([A-Z])" "\\1_\\2" test_name "${test}")
    string(TOLOWER "${test_name}" test_name)
    add_executable(verilog_${test_name}_test test/Verilog${test}Test.cpp)
    target_link_libraries(verilog_${test_name}_test PRIVATE verilog)
    add_test(NAME verilog_${test_name}_test COMMAND verilog_${test_name}_test)
  endforeach()
  target_link_libraries(verilog_input_test PRIVATE ZLIB::ZLIB)
  set_tests_properties(verilog_large_module_test PROPERTIES TIMEOUT 20)
endif()
