icts_add_test_executable(
  icts_test_data_manager_adapter_sdc
  SOURCES
    ${ICTS_TEST}/data_manager/adapter/sdc/ClockTraceTest.cc
  LIBS
    icts_source_data_manager_adapter_sdc
)

target_include_directories(
  icts_test_data_manager_adapter_sdc
  PRIVATE
  ${ICTS_SOURCE}/data_manager/adapter/sdc/clock_trace
)
