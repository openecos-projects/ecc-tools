# set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
# set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_BUILD_TYPE "Debug")

add_subdirectory(${ILVS_TEST}/test_netlist_extractor)
add_subdirectory(${ILVS_TEST}/test_snapshot_io)
add_subdirectory(${ILVS_TEST}/test_lvs_checker)
add_subdirectory(${ILVS_TEST}/test_lvs_reporter)
