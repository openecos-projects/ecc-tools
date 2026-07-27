if(DEBUG_IEMIR_UTILITY)
    message(STATUS "iEMIR: DEBUG_IEMIR_UTILITY")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    message(STATUS "iEMIR: RELEASE_IEMIR_UTILITY")
    set(CMAKE_BUILD_TYPE "Release")
endif()

add_library(iemir_utility
    ${IEMIR_TOOLKIT}/utility/Utility.cpp
)

target_link_libraries(iemir_utility
    PUBLIC
    iemir_data_manager
    iemir_logger
)

target_include_directories(iemir_utility
    PUBLIC
    ${IEMIR_TOOLKIT}/utility
)
