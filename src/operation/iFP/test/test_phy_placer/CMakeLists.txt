add_executable(test_ifp_phy_placer
    test_phy_placer.cpp
)

target_link_libraries(test_ifp_phy_placer
    PRIVATE
    ifp_phy_placer
    ifp_toolkit
)

add_test(NAME test_ifp_phy_placer COMMAND test_ifp_phy_placer)
