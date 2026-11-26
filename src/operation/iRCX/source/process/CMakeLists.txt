# This module also builds on its own, without the design database or extractor.
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ircx_process LANGUAGES CXX)
  include(CTest)
endif()

find_package(FLEX 2.6 REQUIRED)
find_package(BISON 3.7 REQUIRED)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
BISON_TARGET(IRCXItfGrammar parser/itf/syntax/itf_parser.y "${CMAKE_CURRENT_BINARY_DIR}/generated/itf_parser.cpp"
  DEFINES_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/itf_parser.hpp"
  COMPILE_FLAGS "-Wall -Werror=conflicts-sr -Werror=conflicts-rr")
FLEX_TARGET(IRCXItfScanner parser/itf/syntax/itf_lexer.l "${CMAKE_CURRENT_BINARY_DIR}/generated/itf_scanner.cpp")
ADD_FLEX_BISON_DEPENDENCY(IRCXItfScanner IRCXItfGrammar)

BISON_TARGET(IRCXTechGrammar parser/tech/syntax/tech_parser.y "${CMAKE_CURRENT_BINARY_DIR}/generated/tech_parser.cpp"
  DEFINES_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/tech_parser.hpp"
  COMPILE_FLAGS "-Wall -Werror=conflicts-sr -Werror=conflicts-rr")
FLEX_TARGET(IRCXTechScanner parser/tech/syntax/tech_lexer.l "${CMAKE_CURRENT_BINARY_DIR}/generated/tech_scanner.cpp")
ADD_FLEX_BISON_DEPENDENCY(IRCXTechScanner IRCXTechGrammar)

add_library(ircx_process_model INTERFACE)
target_compile_features(ircx_process_model INTERFACE cxx_std_20)
target_include_directories(ircx_process_model INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/model")

add_library(ircx_process_parser
  parser/ProcessReader.cpp parser/ProcessDetector.cpp parser/ProcessUnits.cpp
  parser/ProcessBuilder.cpp parser/ConductorBuilder.cpp parser/ViaBuilder.cpp parser/DeferredModelValidation.cpp parser/ThicknessModelBuilder.cpp
  parser/itf/ItfParser.cpp parser/tech/TechParser.cpp
  parser/itf/syntax/ItfSyntax.cpp
  parser/tech/syntax/TechSyntax.cpp parser/tech/syntax/TechValidation.cpp
  ${BISON_IRCXItfGrammar_OUTPUTS} ${FLEX_IRCXItfScanner_OUTPUTS}
  ${BISON_IRCXTechGrammar_OUTPUTS} ${FLEX_IRCXTechScanner_OUTPUTS})
target_link_libraries(ircx_process_parser PUBLIC ircx_process_model)
target_include_directories(ircx_process_parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/parser")
target_include_directories(ircx_process_parser PRIVATE parser/itf/syntax parser/tech/syntax "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(ircx_process_parser PRIVATE -Wall -Wextra -Wpedantic)

add_library(ircx_process_evaluator evaluator/ProcessEvaluator.cpp evaluator/ProcessThicknessEvaluator.cpp evaluator/ProcessSpatialEvaluator.cpp evaluator/ProcessGeometryValidation.cpp)
target_link_libraries(ircx_process_evaluator PUBLIC ircx_process_model)
target_include_directories(ircx_process_evaluator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/evaluator")

if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  add_executable(ircx_process_parser_test ../../test/RCXProcessParserTest.cpp)
  target_link_libraries(ircx_process_parser_test PRIVATE ircx_process_parser Threads::Threads)
  add_test(NAME ircx_process_parser_test COMMAND ircx_process_parser_test)
  add_executable(ircx_process_evaluator_test ../../test/RCXProcessEvaluatorTest.cpp)
  target_link_libraries(ircx_process_evaluator_test PRIVATE ircx_process_parser ircx_process_evaluator)
  add_test(NAME ircx_process_evaluator_test COMMAND ircx_process_evaluator_test)
  add_executable(ircx_spatial_process_test ../../test/RCXSpatialProcessTest.cpp)
  target_link_libraries(ircx_spatial_process_test PRIVATE ircx_process_evaluator)
  add_test(NAME ircx_spatial_process_test COMMAND ircx_spatial_process_test)
endif()
