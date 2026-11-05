cmake_minimum_required(VERSION 3.11)

set (CMAKE_CXX_STANDARD 20)
# set (CMAKE_BUILD_TYPE "Release")

set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -DNDEBUG")

find_package(ZLIB REQUIRED)

aux_source_directory(./ SRC)
add_library(liberty ${SRC})

target_link_libraries(liberty PUBLIC ecc_logger tcl dl ZLIB::ZLIB)

get_property(ECC_ZLIB_INCLUDE_DIR CACHE ZLIB_INCLUDE_DIR PROPERTY VALUE)
if(ECC_ZLIB_INCLUDE_DIR)
    target_include_directories(liberty SYSTEM PUBLIC ${ECC_ZLIB_INCLUDE_DIR})
endif()

target_include_directories(liberty PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HOME_UTILITY}
    ${HOME_THIRDPARTY}/parser/liberty/)

# The parser is shared by iCTS, iSTA and the Tcl/eval drivers, so its regression
# test lives here rather than in any one operation's test tree.
if(BUILD_TESTING)
    add_executable(liberty_statement_terminator_test test/LibertyStatementTerminatorTest.cc)
    target_link_libraries(liberty_statement_terminator_test PRIVATE liberty gtest pthread)
    add_test(NAME liberty_statement_terminator_test COMMAND liberty_statement_terminator_test)
endif()
