add_library(ista_interface
    ${ISTA_INTERFACE}/STAInterface.cpp
)

target_link_libraries(ista_interface
    PRIVATE
    ista_source
    ista_sdc_command
    idm
    liberty
    spef
)

target_include_directories(ista_interface
    PUBLIC
    ${ISTA_INTERFACE}
)
