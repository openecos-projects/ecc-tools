message(STATUS "")
message(STATUS "############## iSTA: START CMAKE ##############")

############################ cmake flag ############################

set(CMAKE_CXX_STANDARD 20)

# add_compile_options("-Wconversion")
# add_compile_options("-fsanitize=address")
# link_libraries("-fsanitize=address")

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
