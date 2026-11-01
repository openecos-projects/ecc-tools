# SPDX-License-Identifier: MulanPSL-2.0

# These libraries are the Operation-facing boundary for the native EccDB
# providers.  Operation itself is configured after the database tree in the
# full ecc-tools build, so keep the operation target names as link items here
# rather than making the SDK depend on Operation during a standalone build.
if(NOT ECCDB_STANDALONE AND TARGET eccdb_core)
  add_library(eccdb_idrc_adapter
      ${CMAKE_CURRENT_LIST_DIR}/idrc/DRCInterfaceEccdb.cpp
  )
  target_link_libraries(eccdb_idrc_adapter
      PUBLIC
        idrc_interface
      PRIVATE
        idrc_source
        eccdb_core
  )
  target_include_directories(eccdb_idrc_adapter
      PUBLIC
        ${ECCDB_SOURCE_DIR}
  )

  add_library(eccdb_iemir_adapter
      ${CMAKE_CURRENT_LIST_DIR}/iemir/EMIRInterfaceEccdb.cpp
  )
  target_link_libraries(eccdb_iemir_adapter
      PUBLIC
        iemir_interface
      PRIVATE
        iemir_source
        eccdb_core
  )
  target_include_directories(eccdb_iemir_adapter
      PUBLIC
        ${ECCDB_SOURCE_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/iemir
  )

  add_library(eccdb_ilvs_adapter
      ${CMAKE_CURRENT_LIST_DIR}/ilvs/LVSInterfaceEccdb.cpp
  )
  target_link_libraries(eccdb_ilvs_adapter
      PRIVATE
        ilvs_source
        ilvs_interface
        eccdb_core
  )
  target_include_directories(eccdb_ilvs_adapter
      PUBLIC
        ${ECCDB_SOURCE_DIR}
  )

  add_library(eccdb_imj_adapter
      ${CMAKE_CURRENT_LIST_DIR}/imj/MJInterfaceEccdb.cpp
      ${CMAKE_CURRENT_LIST_DIR}/imj/MetalFillEccdb.cpp
      ${CMAKE_CURRENT_LIST_DIR}/imj/metal/MetalFillKernel.cpp
      ${CMAKE_CURRENT_LIST_DIR}/imj/AntennaEccdb.cpp
      ${CMAKE_CURRENT_LIST_DIR}/imj/AntennaIdb.cpp
      ${CMAKE_CURRENT_LIST_DIR}/imj/antenna/AntennaKernel.cpp
  )
  target_link_libraries(eccdb_imj_adapter
      PUBLIC
        imj_interface
      PRIVATE
        imj_source
        eccdb_core
  )
  target_include_directories(eccdb_imj_adapter
      PUBLIC
        ${ECCDB_SOURCE_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/imj
  )

  # Operation-facing native providers are kept in dedicated targets so the
  # Operation interfaces retain their legacy/iDB implementation boundary.
  # The concrete sources implement Operation-owned contracts; consumers bind
  # them here without adding a reverse link from Operation to EccDB.
  add_library(eccdb_ifp_adapter
      ${CMAKE_CURRENT_LIST_DIR}/ifp/FPInterfaceEccdb.cpp
  )
  target_link_libraries(eccdb_ifp_adapter
      PUBLIC
        ifp_interface
      PRIVATE
        ifp_source
        eccdb_core
  )
  target_include_directories(eccdb_ifp_adapter
      PUBLIC
        ${ECCDB_SOURCE_DIR}
  )

  add_library(eccdb_ipw_adapter
      ${CMAKE_CURRENT_LIST_DIR}/ipw/PWInterfaceEccdb.cpp
  )
  target_link_libraries(eccdb_ipw_adapter
      PUBLIC
        ipw_interface
      PRIVATE
        ipw_source
        eccdb_core
  )
  target_include_directories(eccdb_ipw_adapter
      PUBLIC
        ${ECCDB_SOURCE_DIR}
      PRIVATE
        ${CMAKE_SOURCE_DIR}/src/database/manager/parser
  )

  if(ECCDB_BUILD_TESTS)
    add_subdirectory(
        ${ECCDB_SOURCE_DIR}/tests/adapters/idrc
        ${CMAKE_CURRENT_BINARY_DIR}/idrc_adapter_tests
    )
  endif()

  add_subdirectory(irt)

  add_library(eccdb_ista_adapter
      ${CMAKE_CURRENT_LIST_DIR}/ista/STAInterfaceEccdb.cpp
  )
  target_link_libraries(eccdb_ista_adapter
      PUBLIC
        ista_interface
      PRIVATE
        eccdb_core
  )
  target_include_directories(eccdb_ista_adapter
      PUBLIC
        ${ECCDB_SOURCE_DIR}
      PRIVATE
        ${CMAKE_SOURCE_DIR}/src/operation/iSTA/source/data_manager
        ${CMAKE_SOURCE_DIR}/src/operation/iSTA/source/module/reader
        ${CMAKE_SOURCE_DIR}/src/operation/iSTA/source/toolkit/common
        ${CMAKE_SOURCE_DIR}/src/operation/iSTA/source/toolkit/logger
        ${CMAKE_SOURCE_DIR}/src/database/manager/parser
  )

  add_subdirectory(icts)

  if(ECCDB_BUILD_TESTS)
    add_subdirectory(
        ${ECCDB_SOURCE_DIR}/tests/adapters/ifp
        ${CMAKE_CURRENT_BINARY_DIR}/ifp_adapter_tests
    )
    add_subdirectory(
        ${ECCDB_SOURCE_DIR}/tests/adapters/ista
        ${CMAKE_CURRENT_BINARY_DIR}/ista_adapter_tests
    )
    add_subdirectory(
        ${ECCDB_SOURCE_DIR}/tests/adapters/ipw
        ${CMAKE_CURRENT_BINARY_DIR}/ipw_adapter_tests
    )
    add_subdirectory(
        ${ECCDB_SOURCE_DIR}/tests/adapters/izh
        ${CMAKE_CURRENT_BINARY_DIR}/imj_adapter_tests
    )
  endif()

  # One link target is the public full ecc-tools boundary.  Keep the
  # per-operation targets above so tests and selective consumers can choose a
  # single adaptor, while scripting/frontends can depend on this aggregate.
  add_library(eccdb_adapters INTERFACE)
  add_library(EccDB::adapters ALIAS eccdb_adapters)
  target_compile_features(eccdb_adapters INTERFACE cxx_std_20)
  target_include_directories(eccdb_adapters INTERFACE
      $<BUILD_INTERFACE:${ECCDB_SOURCE_DIR}>
  )
  target_link_libraries(eccdb_adapters INTERFACE
      eccdb_idrc_adapter
      eccdb_iemir_adapter
      eccdb_ilvs_adapter
      eccdb_imj_adapter
      eccdb_ifp_adapter
      eccdb_ipw_adapter
      eccdb_irt_adapter
      eccdb_ista_adapter
      eccdb_icts_adapter
  )
endif()
