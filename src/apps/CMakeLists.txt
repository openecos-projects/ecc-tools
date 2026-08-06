# SET(CMAKE_BUILD_TYPE "Release")
# SET(CMAKE_BUILD_TYPE "Debug")

if(NOT DEFINED CMD_BUILD)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin)
endif()

# add GIT_VERSION to definitions
find_package(Git)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --abbrev=40 --always OUTPUT_VARIABLE GIT_VERSION)
    string(STRIP "${GIT_VERSION}" ECC_GIT_VERSION)
    message(STATUS "Git Version: " ${ECC_GIT_VERSION})
else()
    message(WARNING "Git Version: not available")
    set(ECC_GIT_VERSION "not available")
endif()

add_definitions(-DECC_GIT_VERSION="${ECC_GIT_VERSION}")

add_executable(ecc_bin
    ecc_main.cpp
)

target_link_libraries(ecc_bin flow ecc_logger)
