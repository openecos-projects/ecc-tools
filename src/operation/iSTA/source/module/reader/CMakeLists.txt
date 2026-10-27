add_library(ista_reader ReadNetwork.cpp ReadHierarchy.cpp ReadLiberty.cpp ReadParasitics.cpp)
target_link_libraries(ista_reader PRIVATE ista_model ista_network ista_utility ista_monitor idm verilog_builder verilog liberty spef)
target_include_directories(ista_reader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
