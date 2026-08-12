message(STATUS "")
message(STATUS "############## iEMIR: START CMAKE ##############")

############################ cmake flag ############################

set(CMAKE_CXX_STANDARD 20)

############################ setting path ############################

set(HOME_IEMIR ${HOME_OPERATION}/iEMIR)

set(IEMIR_INTERFACE ${HOME_IEMIR}/interface)
set(IEMIR_SOURCE ${HOME_IEMIR}/source)
set(IEMIR_TEST ${HOME_IEMIR}/test)

############################ cmake subdirectory ############################

add_subdirectory(${IEMIR_INTERFACE})
add_subdirectory(${IEMIR_SOURCE})
add_subdirectory(${IEMIR_TEST})

########################################################

message(STATUS "############## iEMIR: END CMAKE ##############\n")
