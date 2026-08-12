set(ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_STATE ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}/clock_state)
set(ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_TREE ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}/clock_tree)
set(ICTS_DATA_MANAGER_ADAPTER_FAST_STA_LIBERTY ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}/liberty)
set(ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_NET_PARASITIC ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}/clock_net_parasitic)
set(ICTS_DATA_MANAGER_ADAPTER_FAST_STA_TIMING ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}/timing)
set(ICTS_DATA_MANAGER_ADAPTER_FAST_STA_POWER ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}/power)
set(ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_SIZING ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}/clock_sizing)
set(ICTS_DATA_MANAGER_ADAPTER_FAST_STA_SEGMENT_CHAR ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}/segment_char)

set(
  ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_STATE}
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_TREE}
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_LIBERTY}
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_NET_PARASITIC}
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_TIMING}
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_POWER}
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_SIZING}
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_SEGMENT_CHAR}
)

add_library(
  icts_source_data_manager_adapter_fast_sta_liberty
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_LIBERTY}/FastSTALiberty.cc
)

add_library(
  icts_source_data_manager_adapter_fast_sta_liberty_model
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_LIBERTY}/FastSTALibertyModel.cc
)

target_link_libraries(
  icts_source_data_manager_adapter_fast_sta_liberty
  PRIVATE
  icts_source_data_manager_io
  icts_source_toolkit_logger
  absl::inlined_vector
  liberty
)

target_include_directories(
  icts_source_data_manager_adapter_fast_sta_liberty
  PRIVATE
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS}
  ${ICTS_DATA_MANAGER}
  ${HOME_DATABASE}/manager/parser
)

target_include_directories(
  icts_source_data_manager_adapter_fast_sta_liberty_model
  PRIVATE
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS}
)

add_library(
  icts_source_data_manager_adapter_fast_sta_clock_net_parasitic
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_NET_PARASITIC}/FastSTAParasitics.cc
)

target_link_libraries(
  icts_source_data_manager_adapter_fast_sta_clock_net_parasitic
  PRIVATE
  icts_source_data_manager_config
  icts_source_data_manager_design
  icts_source_data_manager_io
  icts_source_data_manager_routing
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_data_manager_adapter_fast_sta_clock_net_parasitic
  PRIVATE
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS}
  ${ICTS_DATA_MANAGER}
)

add_library(
  icts_source_data_manager_adapter_fast_sta_clock_tree
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_TREE}/FastSTAClockTree.cc
)

target_link_libraries(
  icts_source_data_manager_adapter_fast_sta_clock_tree
  PRIVATE
  icts_source_data_manager_adapter_fast_sta_clock_net_parasitic
  icts_source_data_manager_design
  icts_source_data_manager_routing
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_data_manager_adapter_fast_sta_clock_tree
  PRIVATE
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS}
  ${ICTS_DATA_MANAGER}
)

add_library(
  icts_source_data_manager_adapter_fast_sta_timing
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_TIMING}/FastSTADmpCeff.cc
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_TIMING}/FastSTADmpCeffEquations.cc
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_TIMING}/FastSTADmpCeffShared.cc
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_TIMING}/FastSTADmpCeffSolver.cc
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_TIMING}/FastSTATiming.cc
)

target_link_libraries(
  icts_source_data_manager_adapter_fast_sta_timing
  PRIVATE
  icts_source_data_manager_adapter_fast_sta_clock_net_parasitic
  icts_source_data_manager_adapter_fast_sta_liberty_model
  absl::fixed_array
)

target_include_directories(
  icts_source_data_manager_adapter_fast_sta_timing
  PRIVATE
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS}
)

add_library(
  icts_source_data_manager_adapter_fast_sta_power
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_POWER}/FastSTAPower.cc
)

target_link_libraries(
  icts_source_data_manager_adapter_fast_sta_power
  PRIVATE
  icts_source_data_manager_adapter_fast_sta_liberty_model
)

target_include_directories(
  icts_source_data_manager_adapter_fast_sta_power
  PRIVATE
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS}
)

add_library(
  icts_source_data_manager_adapter_fast_sta_clock_sizing
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_SIZING}/FastSTAIncremental.cc
)

target_link_libraries(
  icts_source_data_manager_adapter_fast_sta_clock_sizing
  PRIVATE
  icts_source_data_manager_adapter_fast_sta_liberty
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_data_manager_adapter_fast_sta_clock_sizing
  PRIVATE
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS}
)

add_library(
  icts_source_data_manager_adapter_fast_sta_clock_state
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_CLOCK_STATE}/FastSTABuilder.cc
)

target_link_libraries(
  icts_source_data_manager_adapter_fast_sta_clock_state
  PRIVATE
  icts_source_data_manager_adapter_fast_sta_clock_tree
  icts_source_data_manager_adapter_fast_sta_liberty
  icts_source_data_manager_config
  icts_source_data_manager_design
  icts_source_data_manager_io
  icts_source_data_manager_routing
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_data_manager_adapter_fast_sta_clock_state
  PRIVATE
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS}
  ${ICTS_DATA_MANAGER}
)

add_library(
  icts_source_data_manager_adapter_fast_sta_segment_char
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_SEGMENT_CHAR}/FastSTAChar.cc
)

target_link_libraries(
  icts_source_data_manager_adapter_fast_sta_segment_char
  PRIVATE
  icts_source_data_manager_adapter_fast_sta_liberty
  icts_source_data_manager_adapter_fast_sta_power
  icts_source_data_manager_adapter_fast_sta_timing
  icts_source_data_manager_io
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_data_manager_adapter_fast_sta_segment_char
  PRIVATE
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS}
  ${ICTS_DATA_MANAGER}
)

add_library(icts_source_data_manager_adapter_fast_sta ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}/FastSTA.cc)

target_link_libraries(
  icts_source_data_manager_adapter_fast_sta
  PRIVATE
  icts_source_data_manager_adapter_fast_sta_clock_sizing
  icts_source_data_manager_adapter_fast_sta_clock_state
  icts_source_data_manager_adapter_fast_sta_segment_char
  icts_source_data_manager_design
  icts_source_toolkit_logger
)

target_include_directories(
  icts_source_data_manager_adapter_fast_sta
  PUBLIC
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA}
  PRIVATE
  ${ICTS_DATA_MANAGER_ADAPTER_FAST_STA_INTERNAL_INCLUDE_DIRS}
  ${ICTS_DATA_MANAGER}
)

if(DEBUG_ICTS_SOURCE_DATA_MANAGER_ADAPTER_FAST_STA)
  foreach(
    target_name
    icts_source_data_manager_adapter_fast_sta
    icts_source_data_manager_adapter_fast_sta_clock_net_parasitic
    icts_source_data_manager_adapter_fast_sta_clock_sizing
    icts_source_data_manager_adapter_fast_sta_clock_state
    icts_source_data_manager_adapter_fast_sta_clock_tree
    icts_source_data_manager_adapter_fast_sta_liberty
    icts_source_data_manager_adapter_fast_sta_liberty_model
    icts_source_data_manager_adapter_fast_sta_power
    icts_source_data_manager_adapter_fast_sta_segment_char
    icts_source_data_manager_adapter_fast_sta_timing
  )
    if(MSVC)
      target_compile_options(${target_name} PRIVATE /Zi /Od /Ob0)
      target_link_options(${target_name} PRIVATE /DEBUG)
    else()
      target_compile_options(${target_name} PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
    endif()
  endforeach()
endif()
