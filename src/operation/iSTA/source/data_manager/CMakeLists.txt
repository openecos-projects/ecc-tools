add_library(ista_data_manager
    ${ISTA_DATA_MANAGER}/DataManager.cpp
)

target_link_libraries(ista_data_manager
    PUBLIC
    ista_toolkit
    ista_model
    ista_sdc
    PRIVATE ista_link_network
)

target_include_directories(ista_data_manager
    PUBLIC
    ${ISTA_DATA_MANAGER}
)

# Connectivity queries do not depend on constraint evaluation or analysis.
add_library(ista_network ${ISTA_DATA_MANAGER}/network/Network.cpp)
target_link_libraries(ista_network PUBLIC ista_model)

# Queries of stored graph/clock/path state; no graph-construction dependency.
add_library(ista_graph_query ${ISTA_DATA_MANAGER}/graph/TimingStateQuery.cpp)
target_link_libraries(ista_graph_query PUBLIC ista_model)

add_library(ista_link_network network/LinkNetwork.cpp)
target_link_libraries(ista_link_network PUBLIC ista_model)
target_compile_options(ista_link_network PRIVATE -Wall -Wextra -Wpedantic)

add_library(ista_sdc sdc/ClockEdges.cpp sdc/Sdc.cpp sdc/SdcClock.cpp sdc/SdcException.cpp sdc/ExceptionMatcher.cpp)
target_link_libraries(ista_sdc PUBLIC ista_network)
