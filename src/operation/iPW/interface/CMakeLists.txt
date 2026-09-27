add_library(ipw_interface
    ${IPW_INTERFACE}/PWInterface.cpp
)

target_link_libraries(ipw_interface
    PRIVATE
    ipw_source
    ipw_sdc_command
    idm
    liberty
    spef
)

target_include_directories(ipw_interface
    PUBLIC
    ${IPW_INTERFACE}
)
