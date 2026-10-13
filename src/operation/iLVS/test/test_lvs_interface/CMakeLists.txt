add_executable(test_lvs_interface
    test_lvs_interface.cpp
)

# This executable uses assert for its checks, including Release builds.
target_compile_options(test_lvs_interface PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-UNDEBUG>
    $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
)

target_link_libraries(test_lvs_interface
    PRIVATE
    ilvs_interface
    ilvs_data_manager
    ilvs_entity_checker
    ilvs_toolkit
    idm
    eccdb_core
)

add_test(NAME test_lvs_interface COMMAND test_lvs_interface)
