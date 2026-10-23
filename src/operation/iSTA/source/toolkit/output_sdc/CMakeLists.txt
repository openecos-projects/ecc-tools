add_library(ista_output_sdc SdcWriter.cpp SdcObjectWriter.cpp)
target_link_libraries(ista_output_sdc PUBLIC ista_data_manager PRIVATE tcl)
target_include_directories(ista_output_sdc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
