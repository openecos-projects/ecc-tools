cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_BUILD_TYPE "Debug")
message(STATUS "iSTA Test")

find_package(GTest REQUIRED)
find_library(SYSTEM_FFI_LIBRARY
             NAMES ffi
             PATHS /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu
             NO_DEFAULT_PATH)

aux_source_directory(. SourceFiles)
add_executable(iSTATest ${SourceFiles})

target_link_libraries(
  iSTATest
  ista-engine
  idm
  eval_timing_api
  py_idb
  py_ista
  pybind11::embed
  inference
  sdc-cmd
  shell-cmd
  sta
  log
  str
  time
  netlist
  liberty
  delay
  ista_utility
  sta-solver
  verilog
  graph
  idb
  tcl
  usage
  pthread
  stdc++fs
  IdbBuilder
  def_service
  lef_service
  -u_ZN3ipl17SteinerWirelength25updateAllNetWorkPointPairEv
  -u_ZN3ipl13NesterovPlace21printNesterovDatabaseEv
  -u_ZN3ito3iTO5runTOEv
  -u_ZN3idm11DataManager7saveDefENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
  -u_ZN4icts9GDSPloter10plotDesignERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
  -u_ZTVN3ipl21ElectricFieldGradientE
  -u_ZN3ipl8Nesterov17correctNextCoordiEiNS_5PointIiEE
  -u_ZN3ipl21ElectricFieldGradient17initElectro2DListEv
  -u_ZN3ipl8Nesterov23calculateNextSteplengthESt6vectorINS_5PointIfEESaIS3_EE
  -u_ZN3ipl8Nesterov11runNextIterEii
  -u_ZN3ipl8Nesterov16runBackTrackIterEi
  -u_ZN3ipl8Nesterov12initNesterovESt6vectorINS_5PointIiEESaIS3_EES1_INS2_IfEESaIS6_EES5_S8_
  -u_ZN3ipl8Nesterov20correctNextSLPCoordiEiNS_5PointIiEE
  -u_ZN3ipl20WAWirelengthGradientC1EPNS_15TopologyManagerE
  gtest)

if(SYSTEM_FFI_LIBRARY)
  target_link_libraries(iSTATest ${SYSTEM_FFI_LIBRARY})
endif()

if(USE_CUDA_MODE)
  target_link_libraries(iSTATest delay-gpu propagation-gpu)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(iSTATest
                         PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:${NVCC_FLAGS}>)
endif()
