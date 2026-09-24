message(STATUS "")
message(STATUS "############## iPW: START CMAKE ##############")

############################ cmake flag ############################

set(CMAKE_CXX_STANDARD 20)

if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.17)
    set(CMAKE_DEFAULT_BUILD_TYPE Release)
else ()
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif ()
endif ()

# add_compile_options("-Wconversion")
# add_compile_options("-fsanitize=address")
# link_libraries("-fsanitize=address")

############################ setting path ############################

set(HOME_IPW ${HOME_OPERATION}/iPW)

set(IPW_INTERFACE ${HOME_IPW}/interface)
set(IPW_SOURCE ${HOME_IPW}/source)

############################ cmake subdirectory ############################

add_subdirectory(${IPW_INTERFACE})
add_subdirectory(${IPW_SOURCE})

########################################################

message(STATUS "############## iPW: END CMAKE ##############\n")
