include(GNUInstallDirs)

find_package(IrcxIcs55 CONFIG REQUIRED)

if(NOT TARGET IrcxIcs55::ircx_ics55)
  message(WARNING "IrcxIcs55 package must define IrcxIcs55::ircx_ics55")
  set(_IRCX_ICS55_INCLUDE_DIR "{CMAKE_CURRENT_LIST_DIR}/include")
  set(_IRCX_ICS55_LIBRARY "{CMAKE_CURRENT_LIST_DIR}/lib/libircx_ics55.so")
endif()

get_target_property(_IRCX_ICS55_INCLUDE_DIR IrcxIcs55::ircx_ics55 INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(_IRCX_ICS55_LIBRARY IrcxIcs55::ircx_ics55 IMPORTED_LOCATION)

file(WRITE "${CMAKE_BINARY_DIR}/ircx_ics55_library.txt" "${_IRCX_ICS55_LIBRARY}\n")

find_package(OpenMP REQUIRED)
find_package(CURL REQUIRED)

function(_ircx_ics55_find_library OUT_VAR)
  find_library(${OUT_VAR} NAMES ${ARGN})
  if(NOT ${OUT_VAR})
    string(REPLACE ";" ", " _IRCX_ICS55_LIBRARY_NAMES "${ARGN}")
    message(FATAL_ERROR "iRCX ICS55 SDK requires one of: ${_IRCX_ICS55_LIBRARY_NAMES}")
  endif()
  set(${OUT_VAR} "${${OUT_VAR}}" PARENT_SCOPE)
endfunction()

_ircx_ics55_find_library(IRCX_ICS55_GLOG_LIBRARY glog)
_ircx_ics55_find_library(IRCX_ICS55_GFLAGS_LIBRARY gflags)
_ircx_ics55_find_library(IRCX_ICS55_UNWIND_LIBRARY unwind)
_ircx_ics55_find_library(IRCX_ICS55_TCL_LIBRARY tcl)
_ircx_ics55_find_library(IRCX_ICS55_TCL86_LIBRARY tcl8.6)

set(_IRCX_ICS55_LINK_LIBRARIES
  OpenMP::OpenMP_CXX
  ${CURL_LIBRARIES}
  ${IRCX_ICS55_GLOG_LIBRARY}
  ${IRCX_ICS55_GFLAGS_LIBRARY}
  ${IRCX_ICS55_UNWIND_LIBRARY}
  ${IRCX_ICS55_TCL_LIBRARY}
  ${IRCX_ICS55_TCL86_LIBRARY}
)

add_library(ircx_ics55 SHARED IMPORTED GLOBAL)

set_target_properties(ircx_ics55
  PROPERTIES
    IMPORTED_LOCATION "${_IRCX_ICS55_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${_IRCX_ICS55_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${_IRCX_ICS55_LINK_LIBRARIES}"
)

install(FILES "${_IRCX_ICS55_LIBRARY}"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
