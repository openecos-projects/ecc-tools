add_library(idrc_case
    ${IDRC_TEST}/DRCCase.cpp
)

target_link_libraries(idrc_case
    PUBLIC
    idrc_data_manager
)

target_include_directories(idrc_case
    PUBLIC
    ${IDRC_TEST}
)

add_executable(idrc_case_runner ${IDRC_TEST}/drc_case_runner.cpp)
target_link_libraries(idrc_case_runner PRIVATE idrc_case idrc_rule_validator)

# Four cases currently fail on known RuleValidator cluster-partition defects; see
# "Known failing cases" in README.md.
set(IDRC_CASE_LIST
    metal_short_abutting
    metal_short_on_cluster_boundary
    metal_short_on_cluster_boundary_control
    metal_short_overlap
    minimum_area
    minimum_area_cluster_split
    minimum_area_split_polygon
    minimum_area_split_polygon_control
    minimum_width
    out_of_die
    prl_spacing_at_minimum
    prl_spacing_corner
    prl_spacing_env_only
    prl_spacing_violation
)
foreach(case_name ${IDRC_CASE_LIST})
    add_test(NAME idrc_case_${case_name} COMMAND idrc_case_runner ${IDRC_TEST}/cases/${case_name}.drc)
endforeach()

add_test(NAME idrc_case_bad_no_expectation COMMAND idrc_case_runner ${IDRC_TEST}/cases/bad_no_expectation.drc)
set_tests_properties(idrc_case_bad_no_expectation PROPERTIES PASS_REGULAR_EXPRESSION "no expectation; write 'expect none'")
