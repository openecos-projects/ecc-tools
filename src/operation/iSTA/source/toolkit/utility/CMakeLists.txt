add_library(ista_utility
    ${ISTA_TOOLKIT}/utility/Utility.cpp
)

target_link_libraries(ista_utility
    PUBLIC
    ista_data_manager
)

target_include_directories(ista_utility
    PUBLIC
    ${ISTA_TOOLKIT}/utility
)
