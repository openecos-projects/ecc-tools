function(eccdb_add_standalone_lef_def_dependencies)
  if(NOT TARGET lef OR NOT TARGET def)
    add_subdirectory(
      "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/lefdef"
      "${CMAKE_BINARY_DIR}/standalone/lefdef"
      EXCLUDE_FROM_ALL
    )
  endif()

  # The direct importer currently has no logger calls, but its normal project
  # target links ecc_logger. Keep that target contract in standalone builds.
  if(NOT TARGET ecc_logger)
    add_library(ecc_logger
      "${ECCDB_ECC_TOOLS_ROOT}/src/utility/logger/Logger.cpp"
    )
    target_include_directories(ecc_logger
      PUBLIC
        "${ECCDB_ECC_TOOLS_ROOT}/src"
    )
  endif()
endfunction()

function(eccdb_add_standalone_legacy_idb_dependencies)
  if(TARGET idb AND TARGET lef_builder AND TARGET lef_service)
    return()
  endif()

  set(BUILD_STATIC_LIB ON)
  set(ECC_INSTALL_LIB_DIR "${CMAKE_BINARY_DIR}/install/lib")
  set(HOME_UTILITY "${ECCDB_ECC_TOOLS_ROOT}/src/utility")
  set(HOME_DATABASE "${ECCDB_ECC_TOOLS_ROOT}/src/database")
  set(HOME_THIRDPARTY "${ECCDB_ECC_TOOLS_ROOT}/src/third_party")

  if(NOT TARGET absl::strings)
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    add_subdirectory(
      "${ECCDB_ECC_TOOLS_ROOT}/src/third_party/abseil-cpp"
      "${CMAKE_BINARY_DIR}/standalone/abseil"
      EXCLUDE_FROM_ALL
    )
  endif()

  add_subdirectory(
    "${ECCDB_ECC_TOOLS_ROOT}/src/database/basic/geometry"
    "${CMAKE_BINARY_DIR}/standalone/legacy/geometry"
    EXCLUDE_FROM_ALL
  )
  add_subdirectory(
    "${ECCDB_ECC_TOOLS_ROOT}/src/database/data/design"
    "${CMAKE_BINARY_DIR}/standalone/legacy/design"
    EXCLUDE_FROM_ALL
  )
  add_subdirectory(
    "${ECCDB_ECC_TOOLS_ROOT}/src/database/manager/service/lef_service"
    "${CMAKE_BINARY_DIR}/standalone/legacy/lef_service"
    EXCLUDE_FROM_ALL
  )
  add_subdirectory(
    "${ECCDB_ECC_TOOLS_ROOT}/src/database/manager/builder/lef_builder"
    "${CMAKE_BINARY_DIR}/standalone/legacy/lef_builder"
    EXCLUDE_FROM_ALL
  )
endfunction()
