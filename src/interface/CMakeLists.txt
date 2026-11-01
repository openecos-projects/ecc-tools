if(TARGET eccdb_database_owner)
    add_subdirectory(eccdb)
endif()

add_subdirectory(tcl)

if(BUILD_PYTHON)
    add_subdirectory(python)
endif()
