add_library(eccdb_service
  context/RoutingContextService.cpp
  query/PhysicalDesignService.cpp
  routing/NetRouteService.cpp
)
eccdb_storage_target(eccdb_service)
target_include_directories(eccdb_service PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(eccdb_service PUBLIC eccdb_design eccdb_tech_database eccdb_library)
