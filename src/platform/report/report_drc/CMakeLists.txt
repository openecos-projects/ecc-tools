add_library(ecc_report_drc
  report_drc.cpp
)

target_link_libraries(ecc_report_drc
    PUBLIC
        report_basic
        tool_manager
        def_service
        idm
)

target_include_directories(ecc_report_drc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
