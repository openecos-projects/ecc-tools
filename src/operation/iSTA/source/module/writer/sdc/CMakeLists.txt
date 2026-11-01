add_library(ista_sdc_writer SdcWriter.cpp SdcObjectWriter.cpp)
target_link_libraries(ista_sdc_writer PUBLIC ista_model PRIVATE tcl)
target_include_directories(ista_sdc_writer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
