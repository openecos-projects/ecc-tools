cmake_minimum_required(VERSION 3.11)

set (CMAKE_CXX_STANDARD 20)

# if(NOT DEFINED CMD_BUILD)
#     set(CMAKE_BUILD_TYPE "Debug")
#     set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")
# endif()

aux_source_directory(./ SRC)
add_library(iIR-Rust ${SRC})

set(RUST_PROJECT_NAME iir)
set(RUST_PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/iir)
set(RUST_LIB_TYPE a)

SETUP_RUST_PROJECT()

message(STATUS "iIR rust lib path ${RUST_LIB_PATH}")

ADD_EXTERNAL_PROJ(iIR-Rust)

target_link_libraries(iIR-Rust PRIVATE ${RUST_LIB_PATH} dl)