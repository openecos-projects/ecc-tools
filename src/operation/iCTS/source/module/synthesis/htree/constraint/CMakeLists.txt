add_library(
  icts_source_module_synthesis_htree_constraint
  ${ICTS_MODULE_SYNTHESIS_HTREE_CONSTRAINT}/Constraint.cc
)

target_link_libraries(
  icts_source_module_synthesis_htree_constraint
  PRIVATE
  icts_source_module_characterization
)

target_include_directories(
  icts_source_module_synthesis_htree_constraint
  PUBLIC
  ${ICTS_SOURCE}
  ${ICTS_MODULE}
)
