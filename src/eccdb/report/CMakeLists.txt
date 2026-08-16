add_subdirectory(report_table)
add_subdirectory(report_basic)
add_subdirectory(report_db)
add_subdirectory(report_evaluator)
add_subdirectory(report_route)
add_subdirectory(report_place)
add_subdirectory(report_drc)

add_library(ecc_report
  report_manager.cpp
)

target_link_libraries(ecc_report
    PUBLIC
        ecc_logger
    PRIVATE
        report_basic
        ecc_report_db
        ecc_report_evaluator
        ecc_report_route
        ecc_report_place
        ecc_report_drc
)

target_include_directories(ecc_report
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
