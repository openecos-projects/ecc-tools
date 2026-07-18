if(TEST_SHELL)
  add_definitions(-DTEST_SHELL)
endif()

# ADD_COMPILE_OPTIONS("-fsanitize=address") LINK_LIBRARIES("-fsanitize=address")

add_library(ircx_headers INTERFACE)

target_compile_features(ircx_headers INTERFACE cxx_std_20)

target_include_directories(ircx_headers
  INTERFACE
    ${HOME_UTILITY}
    ${HOME_UTILITY}/log
    ${HOME_UTILITY}/stdBase
    ${HOME_UTILITY}/stdBase/include
    ${HOME_OPERATION}/iRCX/source/utils
)

target_include_directories(ircx_headers
  SYSTEM INTERFACE
    ${HOME_THIRDPARTY}
)

add_subdirectory(ics55)
add_subdirectory(source/parser)
add_subdirectory(source/database)

add_subdirectory(source/flow/setup)
add_subdirectory(source/flow/extraction)
add_subdirectory(source/flow/report)
add_subdirectory(source/flow)

add_subdirectory(source/module/topology)
add_subdirectory(source/module/environment)
add_subdirectory(source/module/process)
add_subdirectory(source/module/calculate)
add_subdirectory(source/module/report)

add_subdirectory(source/tool/compare_spef)
add_subdirectory(source/tool/dump_net_shape)
add_subdirectory(source/tool/run_rcx_from_topology)
add_subdirectory(source/tool/plot_spef)

add_subdirectory(api)
