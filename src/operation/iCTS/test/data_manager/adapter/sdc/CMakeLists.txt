icts_add_test_executable(
  icts_test_data_manager_adapter_sdc
  SOURCES
    ${ICTS_TEST}/data_manager/adapter/sdc/ClockTraceTest.cc
    ${ICTS_TEST}/data_manager/adapter/sdc/NativeStaConstraintsTest.cc
    ${ICTS_TEST}/data_manager/adapter/sdc/SDCGeneratedClockResolverTest.cc
  LIBS
    ista_interface
    icts_source_data_manager_adapter_sdc
    icts_source_data_manager_config
    ista_sdc_frontend
    ista_data_manager
)

target_include_directories(
  icts_test_data_manager_adapter_sdc
  PRIVATE
  # First, so the plain "Config.hh" this test includes is the CTS configuration
  # rather than the Liberty parser's identically named header.
  ${ICTS_SOURCE}/data_manager/config
  ${ICTS_SOURCE}/data_manager/adapter/sdc
  ${ICTS_SOURCE}/data_manager/adapter/sdc/clock_trace
)
