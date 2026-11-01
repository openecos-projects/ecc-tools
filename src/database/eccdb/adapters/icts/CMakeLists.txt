# SPDX-License-Identifier: MulanPSL-2.0

# iCTS keeps the algorithm and legacy iDB wrapper in Operation. This target
# supplies the EccDB implementation of its backend-neutral CtsDesignSource.
add_library(eccdb_icts_adapter STATIC CtsEccdbAdapter.cpp)

target_compile_features(eccdb_icts_adapter PUBLIC cxx_std_20)

target_include_directories(
  eccdb_icts_adapter
  PUBLIC
    ${ECCDB_SOURCE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}
  PRIVATE
    ${HOME_DATABASE}/manager/parser/liberty
)

target_link_libraries(
  eccdb_icts_adapter
  PUBLIC
    icts_interface
    icts_source_data_manager_io
    icts_source_data_manager_adapter_sdc
  PRIVATE
    eccdb_core
    icts_source_data_manager_design
    icts_source_toolkit_logger
    idm
    liberty
    salt
)
