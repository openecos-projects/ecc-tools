add_executable(test_izh_metal_inserter
    ${IZH_TEST}/test_metal_inserter/test_metal_inserter.cpp
)

target_link_libraries(test_izh_metal_inserter
    PRIVATE
    izh_metal_inserter
)

add_test(NAME test_izh_metal_inserter COMMAND test_izh_metal_inserter)
