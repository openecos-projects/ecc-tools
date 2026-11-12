add_library(ista_sdf_writer SdfWriter.cpp SdfValueExtractor.cpp SdfFormatter.cpp)
target_link_libraries(ista_sdf_writer PUBLIC ista_model PRIVATE ista_toolkit)
target_include_directories(ista_sdf_writer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(ista_sdf_writer PRIVATE -Wall -Wextra -Wpedantic)
