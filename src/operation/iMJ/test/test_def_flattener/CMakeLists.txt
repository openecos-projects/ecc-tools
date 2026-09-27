add_executable(test_imj_def_flattener
    ${IMJ_TEST}/test_def_flattener/test_def_flattener.cpp
)

target_link_libraries(test_imj_def_flattener
    PRIVATE
    imj_def_flattener
)

add_test(NAME test_imj_def_flattener COMMAND test_imj_def_flattener)
