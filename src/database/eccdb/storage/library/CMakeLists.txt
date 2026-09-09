cmake_minimum_required(VERSION 3.11)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(eccdb_library LANGUAGES CXX)
  enable_testing()
endif()

if(NOT TARGET eccdb_entt)
  add_library(eccdb_entt INTERFACE)
  target_compile_features(eccdb_entt INTERFACE cxx_std_20)
  target_include_directories(eccdb_entt
    SYSTEM
    INTERFACE
      "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/entt"
  )
endif()

if(NOT COMMAND eccdb_storage_target)
  function(eccdb_storage_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
  endfunction()
endif()

if(NOT TARGET eccdb_geometry)
  add_library(eccdb_geometry
    "${CMAKE_CURRENT_LIST_DIR}/../geometry/PolygonRectDecomposer.cpp"
  )
  target_compile_features(eccdb_geometry PUBLIC cxx_std_20)
  target_include_directories(eccdb_geometry
    PUBLIC
      "${CMAKE_CURRENT_LIST_DIR}/.."
  )
endif()

add_library(eccdb_library
  LibraryStore.cpp
  common/LibraryGeometry.cpp
  cell_master/storage/CellMasterStorage.cpp
  master_port/storage/MasterPortStorage.cpp
  master_term/storage/MasterTermStorage.cpp
  site/storage/SiteStorage.cpp
)
eccdb_storage_target(eccdb_library)
target_include_directories(eccdb_library
  PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/.."
)
target_link_libraries(eccdb_library
  PUBLIC
    eccdb_entt
    eccdb_geometry
)

if(NOT DEFINED ECCDB_BUILD_TESTS)
  option(ECCDB_BUILD_TESTS "Build ECCDB tests" ON)
endif()
