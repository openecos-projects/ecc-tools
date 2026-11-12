add_executable(test_ifp_config test_config.cpp)

target_link_libraries(test_ifp_config
    PRIVATE
    ifp_interface
    ifp_toolkit
)

add_test(NAME test_ifp_config COMMAND test_ifp_config ${CMAKE_CURRENT_BINARY_DIR}/config)
