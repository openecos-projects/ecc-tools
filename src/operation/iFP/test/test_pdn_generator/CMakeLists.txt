add_executable(test_ifp_pdn_generator
    test_pdn_generator.cpp
)

target_link_libraries(test_ifp_pdn_generator
    PRIVATE
    ifp_interface
    ifp_pdn_generator
    ifp_toolkit
)

add_test(NAME test_ifp_pdn_generator
    COMMAND test_ifp_pdn_generator ${CMAKE_CURRENT_BINARY_DIR}/pdn_generator
)
