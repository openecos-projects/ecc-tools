# set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
# set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_BUILD_TYPE "Debug")

add_subdirectory(${ILVS_TEST}/test_lvs_data_manager)
add_subdirectory(${ILVS_TEST}/test_lvs_flow)
add_subdirectory(${ILVS_TEST}/test_lvs_interface)
