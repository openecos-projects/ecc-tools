# Logic simulation is also independently testable against the graph value model.
add_library(ista_logic_simulator LogicSimulator.cpp)
target_link_libraries(ista_logic_simulator PUBLIC ista_model)
target_include_directories(ista_logic_simulator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# These classes cooperate to build one topology, with one public component target.
add_library(ista_graph_builder
    GraphBuilder.cpp
    PinDirectionResolver.cpp
    GraphConnectivityBuilder.cpp
    GraphEndpointClassifier.cpp
    GraphOrderBuilder.cpp
)
target_link_libraries(ista_graph_builder PUBLIC ista_model PRIVATE ista_network ista_logic_simulator ista_toolkit)
target_include_directories(ista_graph_builder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
