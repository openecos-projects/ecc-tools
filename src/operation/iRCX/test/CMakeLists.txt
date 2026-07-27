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
