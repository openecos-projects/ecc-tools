message(STATUS "")
message(STATUS "############## MJ: START CMAKE ##############")

############################ cmake flag ############################

set(CMAKE_CXX_STANDARD 20)

# add_compile_options("-Wconversion")
# add_compile_options("-fsanitize=address")
# link_libraries("-fsanitize=address")

############################ debug interface ############################

# set(DEBUG_IMJ_INTERFACE ON)

############################ debug source module ############################

# set(DEBUG_IMJ_ANTENNA_CHECKER ON)
# set(DEBUG_IMJ_FILLER_INSERTER ON)
# set(DEBUG_IMJ_METAL_INSERTER ON)

############################ debug source toolkit ############################

# set(DEBUG_IMJ_LOGGER ON)
# set(DEBUG_IMJ_MONITOR ON)
# set(DEBUG_IMJ_UTILITY ON)

############################ setting path ############################

set(HOME_IMJ ${HOME_OPERATION}/iMJ)

set(IMJ_INTERFACE ${HOME_IMJ}/interface)
set(IMJ_SOURCE ${HOME_IMJ}/source)
set(IMJ_TEST ${HOME_IMJ}/test)

############################ cmake subdirectory ############################

add_subdirectory(${IMJ_INTERFACE})
add_subdirectory(${IMJ_SOURCE})
add_subdirectory(${IMJ_TEST})

########################################################

message(STATUS "############## MJ: END CMAKE ##############\n")
