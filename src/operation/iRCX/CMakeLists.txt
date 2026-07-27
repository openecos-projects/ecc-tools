message(STATUS "")
message(STATUS "############## iRCX: START CMAKE ##############")

############################ cmake flag ############################

set(CMAKE_CXX_STANDARD 20)

############################ debug interface ############################

# set(DEBUG_IRCX_INTERFACE ON)

############################ debug source data_manager ############################

# set(DEBUG_IRCX_DATA_MANAGER ON)

############################ debug source module ############################

# set(DEBUG_IRCX_TOPO_BUILDER ON)
# set(DEBUG_IRCX_ENV_BUILDER ON)
# set(DEBUG_IRCX_VAR_PROCESSOR ON)
# set(DEBUG_IRCX_RES_EXTRACTOR ON)
# set(DEBUG_IRCX_CAP_EXTRACTOR ON)
# set(DEBUG_IRCX_SPEF_WRITER ON)

############################ debug source toolkit ############################

# set(DEBUG_IRCX_LOGGER ON)
# set(DEBUG_IRCX_MONITOR ON)
# set(DEBUG_IRCX_UTILITY ON)

############################ setting path ############################

set(HOME_IRCX ${HOME_OPERATION}/iRCX)

set(IRCX_INTERFACE ${HOME_IRCX}/interface)
set(IRCX_SOURCE ${HOME_IRCX}/source)
set(IRCX_TEST ${HOME_IRCX}/test)

############################ cmake subdirectory ############################

add_subdirectory(${IRCX_INTERFACE})
add_subdirectory(${IRCX_SOURCE})
add_subdirectory(${IRCX_TEST})

########################################################

message(STATUS "############## iRCX: END CMAKE ##############\n")
