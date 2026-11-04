cmake_minimum_required(VERSION 3.11)
set(CMAKE_CXX_STANDARD 20)

find_package(TCL 8.6 QUIET)
if(TCL_FOUND)
  include_directories(${TCL_INCLUDE_PATH})
  get_property(ECC_TCL_INCLUDE_DIR CACHE TCL_INCLUDE_PATH PROPERTY VALUE)
else(TCL_FOUND)
  # install Tcl 8.6 https://github.com/tcltk/tcl/tree/core-8-6-branch
  message(FATAL_ERROR "Could not find TCL!")
endif(TCL_FOUND)

aux_source_directory(./ SRC)
get_property(ECC_TCL_LIBRARY CACHE TCL_LIBRARY PROPERTY VALUE)
get_filename_component(ECC_TCL_LIBRARY_DIR "${ECC_TCL_LIBRARY}" DIRECTORY)
find_library(TCL_STATIC_LIB
  NAMES libtcl8.6.a
  PATHS /usr/lib/x86_64-linux-gnu
  NO_DEFAULT_PATH
)

if(BUILD_STATIC_LIB AND TCL_STATIC_LIB)
  add_library(tcl STATIC ${SRC})
else()
  add_library(tcl SHARED ${SRC})
endif()

# Keep the embedded Tcl command wrapper distinct from the Tcl runtime library.
# Both otherwise use libtcl.so, which makes ld.bfd treat the wrapper and the
# runtime as the same SONAME and leaves the wrapper's Tcl_* references unresolved.
if(NOT BUILD_STATIC_LIB)
  set_target_properties(tcl PROPERTIES OUTPUT_NAME ecc_tcl)
endif()

target_link_libraries(tcl PUBLIC ecc_logger)
if(BUILD_STATIC_LIB AND TCL_STATIC_LIB)
  target_link_libraries(tcl PUBLIC ${TCL_STATIC_LIB})
elseif(ECC_TCL_LIBRARY)
  target_link_libraries(tcl PUBLIC "-L${ECC_TCL_LIBRARY_DIR}" "${ECC_TCL_LIBRARY}")
else()
  target_link_libraries(tcl PUBLIC tcl8.6)
endif()

target_include_directories(tcl
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ECC_TCL_INCLUDE_DIR}
)
if(ECC_TCL_INCLUDE_DIR)
  target_include_directories(tcl SYSTEM PUBLIC ${ECC_TCL_INCLUDE_DIR})
endif()

install(TARGETS tcl
    LIBRARY DESTINATION ${ECC_INSTALL_LIB_DIR}
    RUNTIME DESTINATION ${ECC_INSTALL_LIB_DIR})

# Bundle the Tcl script library into the python wheel: the embedded SDC
# interpreter's Tcl_Init needs init.tcl, whose compiled-in search paths only
# exist on RHEL-like distros. SdcCommand finds this directory next to the
# ecc_py module at runtime.
if(BUILD_PYTHON)
  if(NOT ECC_TCL_SCRIPT_DIR)
    # Prefer the scripts beside the library selected by FindTCL. TCL_TCLSH can
    # resolve to another Tcl installation through PATH.
    get_filename_component(ECC_TCL_LIBRARY_DIR "${TCL_LIBRARY}" DIRECTORY)
    set(ECC_TCL_SCRIPT_DIR "${ECC_TCL_LIBRARY_DIR}/tcl8.6")
    if(NOT EXISTS "${ECC_TCL_SCRIPT_DIR}/init.tcl")
      # Fall back to the script library reported by tclsh when the library
      # installation does not place scripts in lib/tcl8.6.
      execute_process(
        COMMAND ${CMAKE_COMMAND} -E echo "puts $tcl_library"
        COMMAND ${TCL_TCLSH}
        OUTPUT_VARIABLE ECC_TCL_SCRIPT_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    endif()
  endif()
  if(NOT EXISTS "${ECC_TCL_SCRIPT_DIR}/init.tcl")
    message(FATAL_ERROR
      "Tcl script library not found; pass -DECC_TCL_SCRIPT_DIR=<dir containing init.tcl>")
  endif()
  message(STATUS "Bundling Tcl script library from ${ECC_TCL_SCRIPT_DIR}")
  install(DIRECTORY "${ECC_TCL_SCRIPT_DIR}/" DESTINATION ecc_tools_bin/tcl8.6)
endif()
