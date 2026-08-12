icts_add_test_executable(
  icts_test_toolkit_logger
  SOURCES
    ${ICTS_TEST}/toolkit/logger/LogTableTest.cc
    ${ICTS_TEST}/toolkit/logger/LoggerTest.cc
  LIBS
    icts_source_toolkit_logger
)
