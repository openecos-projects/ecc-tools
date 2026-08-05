cmake_minimum_required(VERSION 3.11)
set(CMAKE_CXX_STANDARD 20)

# add include and lib dirs
include_directories(SYSTEM ${HOME_THIRDPARTY})
include_directories(${HOME_UTILITY})

link_directories(${CMAKE_BINARY_DIR}/lib)

add_subdirectory(json)
add_subdirectory(logger)
