if(BUILD_TESTING)
  add_executable(ircx_data_manager_test
      RCXDataManagerTest.cpp
  )

  target_link_libraries(ircx_data_manager_test
      PRIVATE
      ircx_data_manager
  )

  # Keep the assert-based permutation regression active in Release builds.
  target_compile_options(ircx_data_manager_test PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>
      $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
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

  add_executable(ircx_topo_builder_test
      RCXTopoBuilderTest.cpp
  )

  # This executable uses assert for its checks, including Release builds.
  target_compile_options(ircx_topo_builder_test PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>
      $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
  )

  target_include_directories(ircx_topo_builder_test PRIVATE
      ${IRCX_MODULE}/env_builder/eb_data_manager
  )

  target_link_libraries(ircx_topo_builder_test
      PRIVATE
      ircx_topo_builder
  )

  add_test(NAME ircx_topo_builder_test COMMAND ircx_topo_builder_test)

  add_executable(ircx_eccdb_projection_test
      RCXEccdbProjectionTest.cpp
  )

  # Keep assertions active in the Release configurations used for operation
  # differential tests.
  target_compile_options(ircx_eccdb_projection_test PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>
      $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
  )

  target_include_directories(ircx_eccdb_projection_test PRIVATE
      ${IRCX_SOURCE}/data_manager
  )

  target_link_libraries(ircx_eccdb_projection_test
      PRIVATE
      ircx_interface
      ircx_data_manager
      eccdb_core
  )

  add_test(NAME ircx_eccdb_projection_test COMMAND ircx_eccdb_projection_test)

  add_executable(ircx_spef_name_canonicalization_test
      RCXSpefNameCanonicalizationTest.cpp
  )

  target_compile_options(ircx_spef_name_canonicalization_test PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>
      $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
  )

  target_link_libraries(ircx_spef_name_canonicalization_test
      PRIVATE
      ircx_aux
      ircx_source
      spef
  )

  add_test(NAME ircx_spef_name_canonicalization_test COMMAND ircx_spef_name_canonicalization_test)
endif()
