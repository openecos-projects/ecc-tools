cmake_minimum_required(VERSION 3.20)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(eccdb_design LANGUAGES CXX)
  enable_testing()

  if(NOT DEFINED ECCDB_BUILD_TESTS)
    option(ECCDB_BUILD_TESTS "Build ECCDB tests" ON)
  endif()

  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../library" "${CMAKE_CURRENT_BINARY_DIR}/library")
endif()

if(NOT TARGET eccdb_entt OR NOT TARGET eccdb_library)
  message(FATAL_ERROR "eccdb_design requires eccdb_entt and eccdb_library")
endif()

set(ECCDB_DESIGN_ENTITY_BITS "64" CACHE STRING
  "Design Entity storage width: 32 for compact mode or 64 for extended mode")
set_property(CACHE ECCDB_DESIGN_ENTITY_BITS PROPERTY STRINGS 32 64)

if(NOT ECCDB_DESIGN_ENTITY_BITS MATCHES "^(32|64)$")
  message(FATAL_ERROR "ECCDB_DESIGN_ENTITY_BITS must be 32 or 64")
endif()

add_library(eccdb_design
  DesignStore.cpp
  common/DesignGeometryValidation.cpp
  constraint/storage/DesignConstraintStorage.cpp
  fill/storage/DesignFillStorage.cpp
  floorplan/storage/DesignFloorplanStorage.cpp
  global/storage/DesignGlobalStorage.cpp
  netlist/storage/DesignNetlistStorage.cpp
  routing/pool/WireRoutingInput.cpp
  routing/pool/DesignRoutingPool.cpp
  routing/storage/DesignRoutingStorage.cpp
  routing/view/WireRoutingView.cpp
)
target_compile_features(eccdb_design PUBLIC cxx_std_20)
target_include_directories(eccdb_design
  PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/.."
)
target_compile_definitions(eccdb_design
  PUBLIC
    ECCDB_DESIGN_ENTITY_BITS=${ECCDB_DESIGN_ENTITY_BITS}
)
target_link_libraries(eccdb_design
  PUBLIC
    eccdb_entt
    eccdb_library
)
