add_library(icts_source_data_manager_qor INTERFACE)

target_include_directories(
  icts_source_data_manager_qor
  INTERFACE
  ${ICTS_DATA_MANAGER_QOR}
  ${ICTS_DATA_MANAGER}
)
