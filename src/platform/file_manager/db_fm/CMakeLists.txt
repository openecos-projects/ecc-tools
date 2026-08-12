add_library(file_manager_db
    file_soc.cpp
)

target_link_libraries(file_manager_db
    PUBLIC
        file_manager_base
        idm
)

target_include_directories(file_manager_db
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
