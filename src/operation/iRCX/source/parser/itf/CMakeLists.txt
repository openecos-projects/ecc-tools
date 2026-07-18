set(ITF_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${ITF_GENERATED_DIR})

find_package(FLEX REQUIRED)
flex_target(itfScanner
  ${CMAKE_CURRENT_SOURCE_DIR}/itf_lex.l
  ${ITF_GENERATED_DIR}/itf_lex.cpp
  DEFINES_FILE  ${ITF_GENERATED_DIR}/itf_lex.hpp
  # COMPILE_FLAGS "-d" # debug
)

find_package(BISON REQUIRED)
bison_target(itfParser
  ${CMAKE_CURRENT_SOURCE_DIR}/itf_parser.yy
  ${ITF_GENERATED_DIR}/itf_parser.cpp
  DEFINES_FILE  ${ITF_GENERATED_DIR}/itf_parser.hpp
  # COMPILE_FLAGS "-t" # debug
)
add_flex_bison_dependency(itfScanner itfParser)

##-------------------------- Library --------------------------------##
set(SRC
  itfiConductor.cpp
  itfiDielectric.cpp
  itfiVia.cpp
  itfrCallBacks.cpp
  itfrData.cpp
  itfrReader.cpp
  itfrSettings.cpp
)

add_library(itf
  STATIC
    ${SRC}
    ${FLEX_itfScanner_OUTPUTS}
    ${BISON_itfParser_OUTPUTS}
)

target_compile_options(itf PRIVATE -Wall -Wextra -pedantic)

target_include_directories(itf
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE
    ${ITF_GENERATED_DIR}
)
