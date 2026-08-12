add_library(iemir_interface
    ${IEMIR_INTERFACE}/EMIRInterface.cpp
)

target_link_libraries(iemir_interface
    PRIVATE
    iemir_source
    idm
)

target_include_directories(iemir_interface
    PUBLIC
    ${IEMIR_INTERFACE}
)
