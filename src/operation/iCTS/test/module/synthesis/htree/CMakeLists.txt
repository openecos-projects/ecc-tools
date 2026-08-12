add_subdirectory(analytical_solver)

icts_add_test_executable(
  icts_test_module_synthesis_htree
  SOURCES
    ${ICTS_TEST}/module/synthesis/htree/HTreeTest.cc
    ${ICTS_TEST}/module/synthesis/htree/SinkLoadRegionSplitTest.cc
    ${ICTS_TEST}/module/synthesis/htree/WirelengthGridTest.cc
  LIBS
    icts_test_data_manager_design_fixture_data_pin_factory
)

if(ICTS_BUILD_REALTECH_TESTS)
  icts_add_test_executable(
    icts_test_module_synthesis_htree_realtech
    REALTECH
    SOURCES
      ${ICTS_TEST}/module/synthesis/htree/HTreeRealTechScenario.cc
      ${ICTS_TEST}/module/synthesis/htree/HTreeVisualizationDelayPower.cc
      ${ICTS_TEST}/module/synthesis/htree/HTreeVisualizationRendering.cc
      ${ICTS_TEST}/module/synthesis/htree/HTreeArtifactWriter.cc
      ${ICTS_TEST}/module/synthesis/htree/HTreeRealTechMatrixRunner.cc
      ${ICTS_TEST}/module/synthesis/htree/HTreeRealTechMatrixTest.cc
      ${ICTS_TEST}/module/synthesis/htree/HTreeRealTechSmokeTest.cc
    LIBS
      icts_test_data_manager_design_fixture_data_pin_factory
      icts_test_toolkit_io
      icts_test_data_manager_realtech_setup
      icts_test_toolkit_visualization
      icts_test_module_characterization_fixture
  )

  target_compile_definitions(
    icts_test_module_synthesis_htree_realtech
    PRIVATE
      ICTS_ENABLE_SLOW_REALTECH_REGRESSION=0
  )
endif()

if(ICTS_BUILD_REALTECH_TESTS AND ICTS_BUILD_SLOW_REALTECH_TESTS)
  icts_add_test_executable(
    icts_test_module_synthesis_htree_realtech_regression
    REALTECH
    SOURCES
      ${ICTS_TEST}/module/synthesis/htree/HTreeRealTechScenario.cc
      ${ICTS_TEST}/module/synthesis/htree/HTreeVisualizationDelayPower.cc
      ${ICTS_TEST}/module/synthesis/htree/HTreeVisualizationRendering.cc
      ${ICTS_TEST}/module/synthesis/htree/HTreeArtifactWriter.cc
      ${ICTS_TEST}/module/synthesis/htree/HTreeRealTechBranchBufferRegressionTest.cc
    LIBS
      icts_test_data_manager_design_fixture_data_pin_factory
      icts_test_toolkit_io
      icts_test_data_manager_realtech_setup
      icts_test_toolkit_visualization
      icts_test_module_characterization_fixture
  )

  target_compile_definitions(
    icts_test_module_synthesis_htree_realtech_regression
    PRIVATE
      ICTS_ENABLE_SLOW_REALTECH_REGRESSION=1
  )
endif()
