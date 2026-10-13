add_executable(test_imj_metal_inserter
    ${IMJ_TEST}/test_metal_inserter/test_metal_inserter.cpp
)

target_link_libraries(test_imj_metal_inserter
    PRIVATE
    imj_metal_inserter
)

add_test(NAME test_imj_metal_inserter COMMAND test_imj_metal_inserter)
