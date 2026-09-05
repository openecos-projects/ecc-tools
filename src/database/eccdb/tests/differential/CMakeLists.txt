# SPDX-License-Identifier: Apache-2.0

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

if(TARGET eccdb_idb_import AND TARGET eccdb_legacy_lef_reader)
  add_executable(eccdb_lef_idb_conversion_test
    LefIdbConversionTest.cpp
  )
  eccdb_storage_target(eccdb_lef_idb_conversion_test)
  target_compile_definitions(eccdb_lef_idb_conversion_test
    PRIVATE
      ECC_TOOLS_SOURCE_DIR="${ECCDB_TEST_DATA_ROOT}"
  )
  target_link_libraries(eccdb_lef_idb_conversion_test
    PRIVATE
      eccdb_idb_import
      eccdb_legacy_lef_reader
      GTest::gtest_main
  )
  gtest_discover_tests(eccdb_lef_idb_conversion_test
    PROPERTIES LABELS eccdb_differential)
  list(APPEND _eccdb_differential_targets eccdb_lef_idb_conversion_test)

  if(TARGET eccdb_lef_import)
    add_executable(eccdb_direct_lef_differential_test
      DirectLefDifferentialTest.cpp
    )
    eccdb_storage_target(eccdb_direct_lef_differential_test)
    target_compile_definitions(eccdb_direct_lef_differential_test
      PRIVATE
        ECC_TOOLS_SOURCE_DIR="${ECCDB_TEST_DATA_ROOT}"
    )
    target_link_libraries(eccdb_direct_lef_differential_test
      PRIVATE
        eccdb_idb_import
        eccdb_legacy_lef_reader
        eccdb_lef_import
        GTest::gtest_main
    )
    gtest_discover_tests(eccdb_direct_lef_differential_test
      PROPERTIES LABELS eccdb_differential)
    list(APPEND _eccdb_differential_targets eccdb_direct_lef_differential_test)

    if(TARGET eccdb_lef_export)
      add_executable(eccdb_lef_full_corpus_semantic_test
        LefFullCorpusSemanticDifferentialTest.cpp
      )
      eccdb_storage_target(eccdb_lef_full_corpus_semantic_test)
      target_compile_definitions(eccdb_lef_full_corpus_semantic_test
        PRIVATE
          ECC_TOOLS_SOURCE_DIR="${ECCDB_TEST_DATA_ROOT}"
      )
      target_link_libraries(eccdb_lef_full_corpus_semantic_test
        PRIVATE
          eccdb_idb_import
          eccdb_legacy_lef_reader
          eccdb_lef_import
          eccdb_lef_export
          GTest::gtest_main
      )
      gtest_discover_tests(eccdb_lef_full_corpus_semantic_test
        PROPERTIES LABELS eccdb_differential)
      list(APPEND _eccdb_differential_targets eccdb_lef_full_corpus_semantic_test)
    endif()

  endif()
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
