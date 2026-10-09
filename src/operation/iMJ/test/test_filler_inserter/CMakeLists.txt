add_executable(test_imj_filler_inserter
    ${IMJ_TEST}/test_filler_inserter/test_filler_inserter.cpp
)

target_link_libraries(test_imj_filler_inserter
    PRIVATE
    imj_filler_inserter
)

add_test(NAME test_imj_filler_inserter COMMAND test_imj_filler_inserter)
