add_executable(eval_map_layout_writer_test
    map_layout_writer_test.cpp
)

target_link_libraries(eval_map_layout_writer_test
    PRIVATE
        eval_util_map_layout_writer
)

add_test(NAME eval_map_layout_writer_test COMMAND eval_map_layout_writer_test)

add_executable(eval_map_layout_reference_replay
    map_layout_reference_replay.cpp
)

target_link_libraries(eval_map_layout_reference_replay
    PRIVATE
        ieda_feature
        idm
)

set(EVAL_MAP_LAYOUT_REFERENCE_WORKSPACE
    ${CMAKE_BINARY_DIR}/ecc-runs/ecos-ecc-cli-gcd-ics55-20260708-133807/workspace/place_dreamplace)
if(EXISTS ${EVAL_MAP_LAYOUT_REFERENCE_WORKSPACE}/output/gcd_place.def.gz)
    add_test(NAME eval_map_layout_reference_replay COMMAND eval_map_layout_reference_replay ${EVAL_MAP_LAYOUT_REFERENCE_WORKSPACE})
endif()
