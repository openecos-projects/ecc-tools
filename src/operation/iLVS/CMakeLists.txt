message(STATUS "")
message(STATUS "############## LVS: START CMAKE ##############")

############################ cmake flag ############################

set(CMAKE_CXX_STANDARD 20)

# add_compile_options("-Wconversion")
# add_compile_options("-fsanitize=address")
# link_libraries("-fsanitize=address")

############################ debug interface ############################

# set(DEBUG_ILVS_INTERFACE ON)

############################ debug source data_manager ############################

# set(DEBUG_ILVS_DATA_MANAGER ON)

############################ debug source module ############################

# set(DEBUG_ILVS_ENTITY_CHECKER ON)
# set(DEBUG_ILVS_ROUTING_CHECKER ON)
# set(DEBUG_ILVS_PDN_CHECKER ON)
# set(DEBUG_ILVS_LVS_REPORTER ON)

############################ debug source utility ############################

# set(DEBUG_ILVS_LOGGER ON)
# set(DEBUG_ILVS_MONITOR ON)
# set(DEBUG_ILVS_UTILITY ON)

############################ setting path ############################

set(HOME_ILVS ${HOME_OPERATION}/iLVS)

set(ILVS_INTERFACE ${HOME_ILVS}/interface)
set(ILVS_SOURCE ${HOME_ILVS}/source)
set(ILVS_TEST ${HOME_ILVS}/test)

############################ cmake subdirectory ############################

add_subdirectory(${ILVS_INTERFACE})
add_subdirectory(${ILVS_SOURCE})
add_subdirectory(${ILVS_TEST})

########################################################

message(STATUS "############## LVS: END CMAKE ##############\n")
