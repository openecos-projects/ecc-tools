include(CMakeParseArguments)

add_library(icts_test_base INTERFACE)

target_include_directories(
  icts_test_base
  INTERFACE
  ${ICTS_TEST}
  $<TARGET_PROPERTY:idm,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(
  icts_test_base
  INTERFACE
  icts_source_toolkit_logger
  icts_source
  icts_interface
  gtest
  pthread
  geometry_db
  idb
  IdbBuilder
  def_builder
  lef_builder
  def_service
  lef_service
)

target_include_directories(
  icts_test_base
  INTERFACE
  ${HOME_DATABASE}/db
  ${HOME_DATABASE}/builder/builder
  ${HOME_DATABASE}/builder/def_builder/def_service
  ${HOME_DATABASE}/builder/lef_builder/lef_service
)

add_library(icts_test_realtech_base INTERFACE)

target_link_libraries(
  icts_test_realtech_base
  INTERFACE
  icts_test_base
)

add_library(icts_test_main OBJECT ${ICTS_TEST}/main.cc)
target_link_libraries(icts_test_main PUBLIC icts_test_base)

option(ICTS_BUILD_REALTECH_TESTS "Build opt-in iCTS real-tech smoke tests" OFF)
option(ICTS_BUILD_SLOW_REALTECH_TESTS "Build opt-in slow iCTS real-tech regression and benchmark tests" OFF)

function(icts_add_test_executable target_name)
  set(options REALTECH)
  set(one_value_args)
  set(multi_value_args SOURCES LIBS)
  cmake_parse_arguments(ICTS_ADD_TEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  add_executable(${target_name} ${ICTS_ADD_TEST_SOURCES} $<TARGET_OBJECTS:icts_test_main>)
  target_link_libraries(${target_name} PRIVATE icts_test_base icts_test_toolkit_io ${ICTS_ADD_TEST_LIBS})

  if(ICTS_ADD_TEST_REALTECH)
    target_link_libraries(${target_name} PRIVATE icts_test_realtech_base)
  endif()

  add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_subdirectory(toolkit)
add_subdirectory(data_manager)
add_subdirectory(module)
