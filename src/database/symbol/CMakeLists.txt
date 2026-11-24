add_library(ecc_symbol
    SymbolTable.cpp
)

target_include_directories(ecc_symbol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(ecc_symbol_tests
    test/SymbolTableTests.cpp
)

target_link_libraries(ecc_symbol_tests
    PRIVATE
        ecc_symbol
)

add_test(NAME ecc_symbol_tests COMMAND ecc_symbol_tests)

add_executable(ecc_named_table_tests
    test/NamedTableTests.cpp
)

target_link_libraries(ecc_named_table_tests
    PRIVATE
        ecc_symbol
)

add_test(NAME ecc_named_table_tests COMMAND ecc_named_table_tests)
