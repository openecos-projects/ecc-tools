message(STATUS "")
message(STATUS "############## ZH: START CMAKE ##############")

############################ cmake flag ############################

set(CMAKE_CXX_STANDARD 20)

# add_compile_options("-Wconversion")
# add_compile_options("-fsanitize=address")
# link_libraries("-fsanitize=address")

############################ debug interface ############################

# set(DEBUG_IZH_INTERFACE ON)

############################ debug source module ############################

# set(DEBUG_IZH_ANTENNA_CHECKER ON)
# set(DEBUG_IZH_FANOUT_FIXER ON)
# set(DEBUG_IZH_FILLER_INSERTER ON)
# set(DEBUG_IZH_METAL_INSERTER ON)

############################ debug source toolkit ############################

# set(DEBUG_IZH_LOGGER ON)
# set(DEBUG_IZH_MONITOR ON)
# set(DEBUG_IZH_UTILITY ON)

############################ setting path ############################

set(HOME_IZH ${HOME_OPERATION}/iZH)

set(IZH_INTERFACE ${HOME_IZH}/interface)
set(IZH_SOURCE ${HOME_IZH}/source)
set(IZH_TEST ${HOME_IZH}/test)

############################ cmake subdirectory ############################

add_subdirectory(${IZH_INTERFACE})
add_subdirectory(${IZH_SOURCE})
add_subdirectory(${IZH_TEST})

########################################################

message(STATUS "############## ZH: END CMAKE ##############\n")
