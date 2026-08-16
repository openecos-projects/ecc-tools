add_library(idm
    idm.cpp
    idm_init.cpp
    idm_checker.cpp
    idm_design.cpp
    idm_design_inst.cpp
    idm_design_inst_replace.cpp
    idm_design_net.cpp
    idm_layout.cpp
    idm_save.cpp
    # idm_pa.cpp
    idm_transform.cpp
    config/dm_config.cpp
    checker/check_connection.cpp
)

target_link_libraries(idm
    PUBLIC
    IdbBuilder
    liberty
    spef
    vcd
    # file_manager_drc
    ecc_logger
    )

target_include_directories(idm
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/config


        ${HOME_PLATFORM}/file_manager
        ${HOME_DATABASE}/manager/parser

        ##json
        ${HOME_THIRDPARTY}/json
        ${HOME_UTILITY}/json

        ##auxiliary
        ${HOME_UTILITY}
        ${HOME_UTILITY}/json

        ##operation
        ${HOME_OPERATION}

)

if(BUILD_TESTING)
    add_executable(check_connection_graph_test
        checker/check_connection_test.cpp
    )

    target_include_directories(check_connection_graph_test
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    set_target_properties(check_connection_graph_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    add_test(NAME check_connection_graph_test COMMAND check_connection_graph_test)
endif()
