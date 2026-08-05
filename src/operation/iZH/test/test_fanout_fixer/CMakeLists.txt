add_executable(test_izh_fanout_fixer
    ${IZH_TEST}/test_fanout_fixer/test_fanout_fixer.cpp
)

target_link_libraries(test_izh_fanout_fixer
    PRIVATE
    izh_fanout_fixer
)

add_test(NAME test_izh_fanout_fixer COMMAND test_izh_fanout_fixer)
