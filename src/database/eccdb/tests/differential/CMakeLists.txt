# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

find_package(GTest REQUIRED)
include(GoogleTest)

set(_eccdb_differential_targets)

if(TARGET lef AND TARGET def)
  add_executable(eccdb_si2_lefrw
    ${ECCDB_ECC_TOOLS_ROOT}/src/third_party/lefdef/lef/lefrw/lefrw.cpp
  )
  target_link_libraries(eccdb_si2_lefrw PRIVATE lef)

  add_executable(eccdb_si2_defrw
    ${ECCDB_ECC_TOOLS_ROOT}/src/third_party/lefdef/def/defrw/defrw.cpp
  )
  target_link_libraries(eccdb_si2_defrw PRIVATE def)
endif()

if(TARGET eccdb_lef_import AND TARGET eccdb_def_import AND TARGET eccdb_def_export)
  set(OPENDB_TCL_EXECUTABLE "" CACHE FILEPATH "Path to the standalone OpenDB odbtcl executable")
  set(OPENDB_PYTHON_MODULE_DIR "" CACHE PATH "Directory containing OpenDB's odb.py and _odb.so")
  set(OPENROAD_SOURCE_DIR "" CACHE PATH "Path to an OpenROAD source checkout used by DEF corpus tests")

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(NOT OPENDB_PYTHON_MODULE_DIR AND OPENROAD_SOURCE_DIR)
    set(_open_db_bazel_python_dir "${OPENROAD_SOURCE_DIR}/bazel-bin/src/odb")
    if(EXISTS "${_open_db_bazel_python_dir}/odb.py" AND EXISTS "${_open_db_bazel_python_dir}/_odb.so")
      set(OPENDB_PYTHON_MODULE_DIR "${_open_db_bazel_python_dir}")
    endif()
  endif()

  add_executable(eccdb_def_full_corpus_semantic_test
    DefFullCorpusSemanticTest.cpp
  )
  eccdb_storage_target(eccdb_def_full_corpus_semantic_test)
  if(OPENROAD_SOURCE_DIR)
    target_compile_definitions(eccdb_def_full_corpus_semantic_test
      PRIVATE
        OPENROAD_SOURCE_DEFAULT="${OPENROAD_SOURCE_DIR}"
    )
  endif()
  target_link_libraries(eccdb_def_full_corpus_semantic_test
    PRIVATE
      eccdb_lef_import
      eccdb_def_import
      eccdb_def_export
      GTest::gtest_main
  )
  gtest_discover_tests(eccdb_def_full_corpus_semantic_test
    PROPERTIES LABELS eccdb_differential)
  list(APPEND _eccdb_differential_targets eccdb_def_full_corpus_semantic_test)

  add_executable(eccdb_def_semantic_probe ${ECCDB_ECC_TOOLS_ROOT}/test/native/def_semantic_probe.cpp)
  eccdb_storage_target(eccdb_def_semantic_probe)
  target_include_directories(eccdb_def_semantic_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(eccdb_def_semantic_probe PRIVATE
    eccdb_lef_import eccdb_def_import eccdb_def_export eccdb_service GTest::gtest)
  set_target_properties(eccdb_def_semantic_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

  add_executable(eccdb_ifp_semantic_differential_test
    IfpSemanticDifferentialTest.cpp
  )
  eccdb_storage_target(eccdb_ifp_semantic_differential_test)
  target_link_libraries(eccdb_ifp_semantic_differential_test PRIVATE
    eccdb_service eccdb_lef_import eccdb_def_import GTest::gtest_main)
  gtest_discover_tests(eccdb_ifp_semantic_differential_test
    PROPERTIES LABELS eccdb_differential)
  list(APPEND _eccdb_differential_targets eccdb_ifp_semantic_differential_test)

  add_executable(eccdb_opendb_design_differential_test
    OpenDbDesignDifferentialTest.cpp
  )
  eccdb_storage_target(eccdb_opendb_design_differential_test)
  target_compile_definitions(eccdb_opendb_design_differential_test
    PRIVATE
      OPENDB_NORMALIZE_DEF_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/tools/OpenDbNormalizeDef.tcl"
      OPENDB_NORMALIZE_DEF_PYTHON_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/tools/OpenDbNormalizeDef.py"
  )
  if(TARGET eccdb_si2_lefrw AND TARGET eccdb_si2_defrw)
    target_compile_definitions(eccdb_opendb_design_differential_test
      PRIVATE
        SI2_LEFRW_EXECUTABLE="$<TARGET_FILE:eccdb_si2_lefrw>"
        SI2_DEFRW_EXECUTABLE="$<TARGET_FILE:eccdb_si2_defrw>"
    )
    add_dependencies(eccdb_opendb_design_differential_test eccdb_si2_lefrw eccdb_si2_defrw)
  endif()
  if(OPENROAD_SOURCE_DIR)
    target_compile_definitions(eccdb_opendb_design_differential_test
      PRIVATE
        OPENROAD_SOURCE_DEFAULT="${OPENROAD_SOURCE_DIR}"
    )
  endif()
  if(Python3_Interpreter_FOUND AND OPENDB_PYTHON_MODULE_DIR)
    target_compile_definitions(eccdb_opendb_design_differential_test
      PRIVATE
        OPENDB_PYTHON_DEFAULT="${Python3_EXECUTABLE}"
        OPENDB_PYTHONPATH_DEFAULT="${OPENDB_PYTHON_MODULE_DIR}"
    )
  endif()
  if(OPENDB_TCL_EXECUTABLE)
    target_compile_definitions(eccdb_opendb_design_differential_test
      PRIVATE
        OPENDB_TCL_DEFAULT="${OPENDB_TCL_EXECUTABLE}"
    )
  endif()
  target_link_libraries(eccdb_opendb_design_differential_test
    PRIVATE
      eccdb_lef_import
      eccdb_def_import
      eccdb_def_export
      GTest::gtest_main
  )
  gtest_discover_tests(eccdb_opendb_design_differential_test
    PROPERTIES LABELS eccdb_differential)
  list(APPEND _eccdb_differential_targets eccdb_opendb_design_differential_test)

endif()

if(_eccdb_differential_targets)
  add_custom_target(eccdb_differential_tests
    DEPENDS ${_eccdb_differential_targets}
  )
endif()
