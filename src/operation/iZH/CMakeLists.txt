message(STATUS "")
message(STATUS "############## ZH: START CMAKE ##############")

############################ cmake flag ############################

set(CMAKE_CXX_STANDARD 20)

# add_compile_options("-Wconversion")
# add_compile_options("-fsanitize=address")
# link_libraries("-fsanitize=address")

############################ debug interface ############################

# set(DEBUG_IZH_INTERFACE ON)

############################ debug source utility ############################

# set(DEBUG_IZH_LOGGER ON)
# set(DEBUG_IZH_MONITOR ON)
# set(DEBUG_IZH_UTILITY ON)

############################ setting path ############################

set(HOME_IZH ${HOME_OPERATION}/iZH)

set(IZH_INTERFACE ${HOME_IZH}/interface)
set(IZH_TOOLKIT ${HOME_IZH}/toolkit)

############################ cmake subdirectory ############################

add_subdirectory(${IZH_INTERFACE})
add_subdirectory(${IZH_TOOLKIT})

########################################################

message(STATUS "############## ZH: END CMAKE ##############\n")
