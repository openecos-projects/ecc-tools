add_library(icts_interface ${ICTS_INTERFACE}/CTSAPI.cc)

target_include_directories(icts_interface PUBLIC ${ICTS_INTERFACE})

target_link_libraries(
  icts_interface
  PRIVATE
  icts_source
  idm
  feature_db
)

if(DEBUG_ICTS_INTERFACE)
  if(MSVC)
    target_compile_options(icts_interface PRIVATE /Zi /Od /Ob0)
    target_link_options(icts_interface PRIVATE /DEBUG)
  else()
    target_compile_options(icts_interface PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endif()
