icts_add_test_executable(
  icts_test_data_manager_lifecycle
  SOURCES
    ${ICTS_TEST}/data_manager/lifecycle/CTSAPILifecycleTest.cc
)

add_executable(
  icts_test_data_manager_lifecycle_exit
  ${ICTS_TEST}/data_manager/lifecycle/CTSProcessExitTest.cc
)

target_link_libraries(
  icts_test_data_manager_lifecycle_exit
  PRIVATE
  icts_test_base
)

add_test(
  NAME icts_test_data_manager_lifecycle_exit
  COMMAND icts_test_data_manager_lifecycle_exit
)
