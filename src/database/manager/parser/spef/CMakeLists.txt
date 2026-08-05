cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 20)

set(SPEF_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${SPEF_GENERATED_DIR})

find_package(FLEX REQUIRED)
flex_target(spefScanner
  ${CMAKE_CURRENT_SOURCE_DIR}/spef_lex.l
  ${SPEF_GENERATED_DIR}/spef_lex.cpp
  DEFINES_FILE ${SPEF_GENERATED_DIR}/spef_lex.hpp
)

find_package(BISON REQUIRED)
bison_target(spefParser
  ${CMAKE_CURRENT_SOURCE_DIR}/spef_parser.yy
  ${SPEF_GENERATED_DIR}/spef_parser.cpp
  DEFINES_FILE ${SPEF_GENERATED_DIR}/spef_parser.hpp
)

add_library(spef
  SpefAnnotationScanner.cc
  SpefParserContext.cc
  SpefReader.cc
  SpefText.cc
  ${FLEX_spefScanner_OUTPUTS}
  ${BISON_spefParser_OUTPUTS}
)

target_link_libraries(spef ecc_logger)
target_include_directories(spef
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE
    ${SPEF_GENERATED_DIR}
)
