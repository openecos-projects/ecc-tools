message(STATUS "")
message(STATUS "############## iFP: START CMAKE ##############")

############################ cmake flag ############################

set(CMAKE_CXX_STANDARD 20)

############################ debug interface ############################

# set(DEBUG_IFP_INTERFACE ON)

############################ debug source data_manager ############################

# set(DEBUG_IFP_DATA_MANAGER ON)

############################ debug source module ############################

# set(DEBUG_IFP_DIE_BUILDER ON)
# set(DEBUG_IFP_IO_PLACER ON)
# set(DEBUG_IFP_MACRO_PLACER ON)
# set(DEBUG_IFP_PDN_GENERATOR ON)
# set(DEBUG_IFP_PHY_PLACER ON)

############################ debug source toolkit ############################

# set(DEBUG_IFP_LOGGER ON)
# set(DEBUG_IFP_MONITOR ON)
# set(DEBUG_IFP_UTILITY ON)

############################ setting path ############################

set(HOME_IFP ${HOME_OPERATION}/iFP)

set(IFP_INTERFACE ${HOME_IFP}/interface)
set(IFP_SOURCE ${HOME_IFP}/source)

############################ cmake subdirectory ############################

add_subdirectory(${IFP_INTERFACE})
add_subdirectory(${IFP_SOURCE})

########################################################

message(STATUS "############## iFP: END CMAKE ##############\n")
