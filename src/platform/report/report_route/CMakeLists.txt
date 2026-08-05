aux_source_directory(. REPORT_ROUTE_SRC)


add_library(ecc_report_route ${REPORT_ROUTE_SRC})

target_link_libraries(ecc_report_route
    PUBLIC
        report_basic
        idm
        tool_manager
        def_service
)

target_include_directories(ecc_report_route
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
