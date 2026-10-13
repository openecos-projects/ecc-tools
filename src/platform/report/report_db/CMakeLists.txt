add_library(ecc_report_db
  report_db.cpp
  report_design.cpp
  report_layout.cpp
)

target_link_libraries(ecc_report_db
    PUBLIC
        report_basic
        idm
        def_service
)

target_include_directories(ecc_report_db
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
