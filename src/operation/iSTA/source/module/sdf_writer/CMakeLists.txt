add_library(ista_sdf_writer
    ${ISTA_MODULE}/sdf_writer/SDFWriter.cpp
)

target_link_libraries(ista_sdf_writer
    PUBLIC
    ista_data_manager
    ista_toolkit
)

target_include_directories(ista_sdf_writer
    PUBLIC
    ${ISTA_MODULE}/sdf_writer/sw_data_manager
    ${ISTA_MODULE}/sdf_writer
)
