add_library(report_basic
  report_basic.cpp
)

target_link_libraries(report_basic 
    PUBLIC
        report_table
        idm
        def_service
)

target_include_directories(report_basic 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# SET(CMAKE_BUILD_TYPE "Debug")
