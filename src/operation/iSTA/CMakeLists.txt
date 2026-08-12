message(STATUS "")
message(STATUS "############## iSTA: START CMAKE ##############")

############################ cmake flag ############################

set(CMAKE_CXX_STANDARD 20)

# add_compile_options("-Wconversion")
# add_compile_options("-fsanitize=address")
# link_libraries("-fsanitize=address")

############################ debug interface ############################

# set(DEBUG_ISTA_INTERFACE ON)

############################ debug source data_manager ############################

# set(DEBUG_ISTA_DATA_MANAGER ON)

############################ debug source module ############################

# set(DEBUG_ISTA_DESIGN_LOADER ON)
# set(DEBUG_ISTA_CONSTRAINT_MANAGER ON)
# set(DEBUG_ISTA_GRAPH_BUILDER ON)
# set(DEBUG_ISTA_DELAY_CALCULATOR ON)
# set(DEBUG_ISTA_CLOCK_PROPAGATOR ON)
# set(DEBUG_ISTA_TIMING_PROPAGATOR ON)
# set(DEBUG_ISTA_POWER_PROPAGATOR ON)
# set(DEBUG_ISTA_TIMING_ANALYZER ON)
# set(DEBUG_ISTA_POWER_ANALYZER ON)
# set(DEBUG_ISTA_TIMING_REPORTER ON)
# set(DEBUG_ISTA_POWER_REPORTER ON)
# set(DEBUG_ISTA_SDF_WRITER ON)

############################ debug source toolkit ############################

# set(DEBUG_ISTA_LOGGER ON)
# set(DEBUG_ISTA_MONITOR ON)
# set(DEBUG_ISTA_UTILITY ON)

############################ setting path ############################

set(HOME_ISTA ${HOME_OPERATION}/iSTA)

set(ISTA_INTERFACE ${HOME_ISTA}/interface)
set(ISTA_SOURCE ${HOME_ISTA}/source)
set(ISTA_TEST ${HOME_ISTA}/test)

############################ cmake subdirectory ############################

add_subdirectory(${ISTA_INTERFACE})
add_subdirectory(${ISTA_SOURCE})
add_subdirectory(${ISTA_TEST})

########################################################

message(STATUS "############## iSTA: END CMAKE ##############\n")
