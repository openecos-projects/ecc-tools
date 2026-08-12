add_library(
  icts_source_toolkit_logger
  STATIC
  ${ICTS_TOOLKIT_LOGGER}/LogTable.cc
  ${ICTS_TOOLKIT_LOGGER}/Logger.cc
)

target_include_directories(
  icts_source_toolkit_logger
  PUBLIC
  ${ICTS_TOOLKIT_LOGGER}
)
