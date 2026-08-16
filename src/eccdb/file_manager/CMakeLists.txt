include_directories(      
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${HOME_PLATFORM}
        ${HOME_OPERATION}
)

add_subdirectory(base)
add_subdirectory(db_fm)
add_subdirectory(idrc_fm)
