## set path
set(ISTA_DATA_MANAGER ${ISTA_SOURCE}/data_manager)
set(ISTA_MODULE ${ISTA_SOURCE}/module)
set(ISTA_TOOLKIT ${ISTA_SOURCE}/toolkit)

# Shared vocabulary and database model. Model enums retain existing logger diagnostics.
add_library(ista_common INTERFACE)
target_include_directories(ista_common INTERFACE
    ${ISTA_TOOLKIT}/common
)
add_library(ista_model INTERFACE)
target_include_directories(ista_model INTERFACE ${ISTA_DATA_MANAGER})
target_link_libraries(ista_model INTERFACE ista_common ista_logger)

## build
add_subdirectory(${ISTA_DATA_MANAGER})
add_subdirectory(${ISTA_MODULE})
add_subdirectory(${ISTA_TOOLKIT})
