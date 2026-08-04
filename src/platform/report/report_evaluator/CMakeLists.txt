add_library(ecc_report_evaluator
  report_evaluator.cpp
)

target_link_libraries(ecc_report_evaluator
    PUBLIC
        report_basic
        tool_manager
        def_service
        eval_wirelength_api
        eval_congestion_api
        eval_density_api
        ecc_logger
)

target_include_directories(ecc_report_evaluator
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
