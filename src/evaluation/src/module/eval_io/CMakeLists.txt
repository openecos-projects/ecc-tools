aux_source_directory(./ SRC)
add_library(eval_io ${SRC})

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HOME_THIRDPARTY}/json
)

set(EVAL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "Path to eval module headers")

target_link_libraries(eval_io
    PRIVATE
        idm
        ecc_logger
        eval_density_api
        eval_timing_api
        eval_wirelength_api
)

target_include_directories(eval_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
