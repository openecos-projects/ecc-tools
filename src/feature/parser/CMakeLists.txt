add_library(feature_parser
    feature_parser.cpp
    feature_parser_summary.cpp
    feature_parser_tools.cpp
    feature_parser_eval.cpp
    feature_parser_drc.cpp
)

target_include_directories(feature_parser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}

        ##json
        ${HOME_THIRDPARTY}/json
        ${HOME_UTILITY}/json

        ##auxiliary
        ${HOME_UTILITY}
        ${HOME_UTILITY}/json

        ${HOME_DATABASE}/interaction
)

target_link_libraries(feature_parser
    PRIVATE
        idb
        feature_db
        idm
        # irt_interface
        # ipl-source
        # ipl-api
        # icts_interface
        # ista-engine
        # idrc_pro_api
        # ecc_report_evaluator
        eval_wirelength_api
        eval_congestion_api
        eval_timing_api
        ecc_logger
)
