cmake_minimum_required(VERSION 3.11)
set(CMAKE_CXX_STANDARD 20)

find_package(TCL 8.6 QUIET)
if(TCL_FOUND)
  include_directories(${TCL_INCLUDE_PATH})
else(TCL_FOUND)
  # install Tcl 8.6 https://github.com/tcltk/tcl/tree/core-8-6-branch
  message(FATAL_ERROR "Could not find TCL!")
endif(TCL_FOUND)

aux_source_directory(./ SRC)
set(LINK_tcl "tcl8.6")
find_library(TCL_STATIC_LIB
  NAMES libtcl8.6.a
  PATHS /usr/lib/x86_64-linux-gnu
  NO_DEFAULT_PATH
)

if(BUILD_STATIC_LIB AND TCL_STATIC_LIB)
  set(LINK_tcl ${TCL_STATIC_LIB})
  add_library(tcl STATIC ${SRC})
else()
  add_library(tcl SHARED ${SRC})
endif()

target_link_libraries(tcl PUBLIC ${LINK_tcl} ecc_logger)

target_include_directories(tcl
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

install(TARGETS tcl
    LIBRARY DESTINATION ${ECC_INSTALL_LIB_DIR}
    RUNTIME DESTINATION ${ECC_INSTALL_LIB_DIR})

# Bundle the Tcl script library into the python wheel: the embedded SDC
# interpreter's Tcl_Init needs init.tcl, whose compiled-in search paths only
# exist on RHEL-like distros. SdcCommand finds this directory next to the
# ecc_py module at runtime.
if(BUILD_PYTHON)
  if(NOT ECC_TCL_SCRIPT_DIR)
    # tclsh reports the script library of the Tcl installation it belongs to.
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E echo "puts $tcl_library"
      COMMAND ${TCL_TCLSH}
      OUTPUT_VARIABLE ECC_TCL_SCRIPT_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
  endif()
  if(NOT ECC_TCL_SCRIPT_DIR)
    message(FATAL_ERROR
      "Tcl script library not found; pass -DECC_TCL_SCRIPT_DIR=<dir containing init.tcl>")
  endif()
  message(STATUS "Bundling Tcl script library from ${ECC_TCL_SCRIPT_DIR}")
  install(DIRECTORY "${ECC_TCL_SCRIPT_DIR}/" DESTINATION ecc_tools_bin/tcl8.6)
endif()
