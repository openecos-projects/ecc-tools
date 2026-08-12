if(BUILD_STATIC_LIB)
  add_library( lef_service  
  lef_service.cpp
  )
else()
  add_library( lef_service  SHARED
  lef_service.cpp
  )
endif()

target_link_libraries(lef_service idb)

target_include_directories(lef_service 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_DATABASE}/data/design
        ${HOME_DATABASE}/data/design/db_design
        ${HOME_DATABASE}/data/design/db_layout
)

install(TARGETS lef_service
    LIBRARY DESTINATION ${ECC_INSTALL_LIB_DIR}
    RUNTIME DESTINATION ${ECC_INSTALL_LIB_DIR})
