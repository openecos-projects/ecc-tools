if(BUILD_TESTING)
  add_executable(ista_arc_timing_arc_delay_test
      ArcTimingArcDelayTest.cpp
  )

  target_link_libraries(ista_arc_timing_arc_delay_test
      PRIVATE
      ista_data_manager
  )

  add_test(NAME ista_arc_timing_arc_delay_test COMMAND ista_arc_timing_arc_delay_test)
endif()
