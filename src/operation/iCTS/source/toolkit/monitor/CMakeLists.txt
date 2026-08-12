add_library(
  icts_source_toolkit_monitor
  STATIC
  ${ICTS_TOOLKIT_MONITOR}/Monitor.cc
)

target_include_directories(
  icts_source_toolkit_monitor
  PUBLIC
  ${ICTS_TOOLKIT_MONITOR}
)

target_link_libraries(
  icts_source_toolkit_monitor
  PRIVATE
  icts_source_toolkit_logger
)
