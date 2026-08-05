add_executable(test_izh_filler_inserter
    ${IZH_TEST}/test_filler_inserter/test_filler_inserter.cpp
)

target_link_libraries(test_izh_filler_inserter
    PRIVATE
    izh_filler_inserter
)

add_test(NAME test_izh_filler_inserter COMMAND test_izh_filler_inserter)
