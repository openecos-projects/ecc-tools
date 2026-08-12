add_library(
  icts_test_toolkit_io
  STATIC
  ${ICTS_TEST}/toolkit/io/TestArtifactIO.cc
)

target_link_libraries(
  icts_test_toolkit_io
  PRIVATE
  icts_source_toolkit_logger
  icts_test_base
)

icts_add_test_executable(
  icts_test_toolkit_io_artifact
  SOURCES
    ${ICTS_TEST}/toolkit/io/TestArtifactIOTest.cc
  LIBS
    icts_test_toolkit_io
)

target_link_libraries(icts_test_toolkit_io_artifact PUBLIC icts_test_toolkit_io)
