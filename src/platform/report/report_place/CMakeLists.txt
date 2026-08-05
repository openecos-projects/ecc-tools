aux_source_directory(. REPORT_PLACE_SRC)


add_library(ecc_report_place ${REPORT_PLACE_SRC})

target_link_libraries(ecc_report_place
    PUBLIC
        report_basic
        idm
        tool_manager
        def_service
        ecc_logger
)

target_include_directories(ecc_report_place
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
