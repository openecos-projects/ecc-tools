add_subdirectory(database)
add_subdirectory(parser)
add_subdirectory(builder)

add_library(ecc_feature
  feature_manager.cpp
)

target_link_libraries(ecc_feature
    PUBLIC
        idb
        feature_parser
        feature_db
        feature_builder
)

target_include_directories(ecc_feature
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)