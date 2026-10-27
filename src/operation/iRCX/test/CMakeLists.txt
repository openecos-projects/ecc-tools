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
  add_executable(ircx_prebuilt_test RCXPrebuiltTest.cpp)
  target_link_libraries(ircx_prebuilt_test PRIVATE ircx_cap_extractor)
  foreach(mode prebuilt apply)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${mode}_query")
    if(mode STREQUAL "prebuilt")
      set(fixture "prebuilt_test.model")
    else()
      set(fixture "exact_test.table")
    endif()
    add_test(NAME ircx_${mode}_query_test COMMAND ircx_prebuilt_test
        ${mode} "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${fixture}")
    set_tests_properties(ircx_${mode}_query_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${mode}_query")
  endforeach()
endif()
