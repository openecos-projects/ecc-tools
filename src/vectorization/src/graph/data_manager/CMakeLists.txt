set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(POLICY CMP0100)
    cmake_policy(SET CMP0100 NEW)
endif()

set(IVEC_GRAPH_DM_SRCS
    vec_graph_dm.cpp
    vec_graph_check.cc
    vec_net_graph_gen.cc
)

add_library(ivec_graph_dm ${IVEC_GRAPH_DM_SRCS})

target_link_libraries(ivec_graph_dm
    PUBLIC
    idb
    idm
    geometry_db
    log
    usage
    ivec_db
    ivec_dm
    ivec_layout_db
    ivec_util
    eval_timing_api
)

target_include_directories(ivec_graph_dm
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HOME_UTILITY}/json
)

include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
