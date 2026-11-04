add_library(ista_liberty_writer
    LibertyWriter.cpp
)
target_link_libraries(ista_liberty_writer
    PUBLIC ista_model
    PRIVATE ista_utility liberty
)
target_include_directories(ista_liberty_writer
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
