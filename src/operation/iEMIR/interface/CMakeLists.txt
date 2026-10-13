add_library(iemir_interface
    ${IEMIR_INTERFACE}/EMIRInterface.cpp
    ${CMAKE_SOURCE_DIR}/src/database/eccdb/adapters/iemir/EMIRInterfaceEccdb.cpp
)

target_link_libraries(iemir_interface
    PRIVATE
    iemir_ir_solver
    iemir_source
    idm
    eccdb_core
)

target_include_directories(iemir_interface
    PUBLIC
    ${IEMIR_INTERFACE}
)
