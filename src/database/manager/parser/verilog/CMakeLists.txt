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

set(VERILOG_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${VERILOG_GENERATED_DIR})

find_package(FLEX REQUIRED)
flex_target(verilogScanner
  ${CMAKE_CURRENT_SOURCE_DIR}/VerilogLexer.l
  ${VERILOG_GENERATED_DIR}/VerilogLexer.cc
  DEFINES_FILE ${VERILOG_GENERATED_DIR}/VerilogLexer.hh
)

find_package(BISON REQUIRED)
bison_target(verilogParser
  ${CMAKE_CURRENT_SOURCE_DIR}/VerilogParser.yy
  ${VERILOG_GENERATED_DIR}/VerilogParser.cc
  DEFINES_FILE ${VERILOG_GENERATED_DIR}/VerilogParser.hh
)
add_flex_bison_dependency(verilogScanner verilogParser)

add_library(verilog
  VerilogReader.cc
  VerilogParserContext.cc
  ${FLEX_verilogScanner_OUTPUTS}
  ${BISON_verilogParser_OUTPUTS}
)

target_link_libraries(verilog ecc_logger ${ZLIB_LIBRARIES})

target_include_directories(verilog
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${VERILOG_GENERATED_DIR}
)

if(BUILD_TESTING)
  add_executable(verilog_escaped_identifier_test
      VerilogEscapedIdentifierTest.cpp
  )
  target_link_libraries(verilog_escaped_identifier_test PRIVATE verilog ecc_logger)
  add_test(NAME verilog_escaped_identifier_test COMMAND verilog_escaped_identifier_test)
endif()
