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
      ircx_aux
      ircx_spef_writer
      ircx_data_manager
      ircx_utility
      idm
      spef
  )

  add_test(NAME ircx_interface_test COMMAND ircx_interface_test)
  foreach(scenario missing_database missing_lef missing_def retry_after_missing_database spef_escaping spef_writer_sections spef_writer_names
                   grid_capacitive_etch grid_invalid_resistive_etch)
    add_test(NAME ircx_interface_${scenario} COMMAND ircx_interface_test ${scenario})
  endforeach()
  foreach(scenario grid override corners coupling overlap process_names reject_a reject_b reject_empty reject_mixed
                   override_corners removed_mode removed_prebuilt removed_rank removed_table)
    add_test(NAME ircx_interface_captab_${scenario} COMMAND ircx_interface_test captab_${scenario})
  endforeach()

  add_executable(ircx_spef_test RCXSPEFTest.cpp)
  target_link_libraries(ircx_spef_test PRIVATE ircx_spef_writer ircx_aux spef)
  foreach(scenario decode names comments empty_sections plot_names topology_names)
    add_test(NAME ircx_spef_${scenario} COMMAND ircx_spef_test ${scenario})
  endforeach()
endif()

if(BUILD_TESTING)
  add_executable(ircx_grid_layer_names_test RCXGridLayerNamesTest.cpp)
  target_link_libraries(ircx_grid_layer_names_test PRIVATE ircx_cap_extractor)
  add_test(NAME ircx_grid_layer_names_test COMMAND ircx_grid_layer_names_test)
  add_executable(ircx_pattern_test RCXPatternTest.cpp)
  target_link_libraries(ircx_pattern_test PRIVATE ircx_cap_extractor)
  target_compile_options(ircx_pattern_test PRIVATE -UNDEBUG)
  add_test(NAME ircx_pattern_test COMMAND ircx_pattern_test)
endif()

if(BUILD_TESTING)
  add_executable(ircx_resistance_geometry_test RCXResistanceGeometryTest.cpp)
  target_link_libraries(ircx_resistance_geometry_test PRIVATE ircx_res_extractor ircx_var_processor ircx_interface idm)
  foreach(scale 0.9 0.73 1 0 -1 nan)
    add_test(NAME ircx_resistance_geometry_${scale} COMMAND ircx_resistance_geometry_test ${scale})
  endforeach()
  foreach(scenario wire_etch via_area invalid_geometry process_input logger_exit)
    add_test(NAME ircx_resistance_${scenario} COMMAND ircx_resistance_geometry_test ${scenario})
  endforeach()
endif()
