file(GLOB_RECURSE DB_SRC "*.cpp")
# Unit tests carry their own main(); keep them out of the library.
list(FILTER DB_SRC EXCLUDE REGEX ".*/test/.*")
if(BUILD_STATIC_LIB)
  add_library(geometry_db ${DB_SRC})
else()
  add_library(geometry_db SHARED ${DB_SRC})
endif()

target_include_directories(geometry_db 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
)




install(TARGETS geometry_db
    LIBRARY DESTINATION ${ECC_INSTALL_LIB_DIR}
    RUNTIME DESTINATION ${ECC_INSTALL_LIB_DIR})

add_executable(idb_geometry_tests
    test/IdbGeometryTests.cpp
)

target_link_libraries(idb_geometry_tests
    PRIVATE
        geometry_db
)

add_test(NAME idb_geometry_tests COMMAND idb_geometry_tests)
