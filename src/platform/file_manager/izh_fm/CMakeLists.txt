add_library(file_manager_zh
    file_zh.cpp
)

target_link_libraries(file_manager_zh
    file_manager_base
    idm
    izh_interface
)

target_include_directories(file_manager_zh
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HOME_UTILITY}/json
    ${HOME_OPERATION}/iZH/interface
)
