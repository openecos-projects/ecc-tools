cmake_minimum_required(VERSION 3.11)

set (CMAKE_CXX_STANDARD 20)
# set (CMAKE_BUILD_TYPE "Release")

set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -DNDEBUG")

aux_source_directory(./ SRC)
add_library(liberty ${SRC})

target_link_libraries(liberty str sta-solver log dl)

target_include_directories(liberty PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${HOME_THIRDPARTY}/parser/liberty/)
