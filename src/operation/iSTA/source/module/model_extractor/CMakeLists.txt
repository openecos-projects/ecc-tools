add_library(ista_model_extractor
    ModelExtractor.cpp
    CheckArcExtractor.cpp
    DelayArcExtractor.cpp
    ModelPathFinder.cpp
)
target_link_libraries(ista_model_extractor
    PUBLIC ista_model
    PRIVATE ista_toolkit ista_network
)
target_include_directories(ista_model_extractor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
