if(BUILD_TESTING)
  add_executable(ircx_data_manager_test
      RCXDataManagerTest.cpp
  )

  target_link_libraries(ircx_data_manager_test
      PRIVATE
      ircx_data_manager
  )

  add_test(NAME ircx_data_manager_test COMMAND ircx_data_manager_test)

  add_executable(ircx_utility_test
      RCXUtilityTest.cpp
  )

  target_link_libraries(ircx_utility_test
      PRIVATE
      ircx_utility
  )

  add_test(NAME ircx_utility_test COMMAND ircx_utility_test)

  add_executable(ircx_interface_test
      RCXInterfaceTest.cpp
  )

  target_link_libraries(ircx_interface_test
      PRIVATE
      ircx_interface
  )

  add_test(NAME ircx_interface_test COMMAND ircx_interface_test)
endif()

if(BUILD_TESTING)
  add_executable(ircx_pattern_test RCXPatternTest.cpp)
  target_link_libraries(ircx_pattern_test PRIVATE ircx_cap_extractor)
  target_compile_options(ircx_pattern_test PRIVATE -UNDEBUG)
  add_test(NAME ircx_pattern_test COMMAND ircx_pattern_test)
endif()

if(BUILD_TESTING)
  add_executable(ircx_capacitance_table_test RCXCapacitanceTableTest.cpp)
  target_link_libraries(ircx_capacitance_table_test PRIVATE ircx_cap_extractor)
  add_test(NAME ircx_capacitance_table_test COMMAND ircx_capacitance_table_test)
endif()

if(BUILD_TESTING)
  add_executable(ircx_resistance_geometry_test RCXResistanceGeometryTest.cpp)
  target_link_libraries(ircx_resistance_geometry_test PRIVATE ircx_res_extractor)
  foreach(scale 0.9 0.73 1 0 -1 nan)
    add_test(NAME ircx_resistance_geometry_${scale} COMMAND ircx_resistance_geometry_test ${scale})
  endforeach()
endif()
