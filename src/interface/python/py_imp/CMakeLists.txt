aux_source_directory(. PY_IMP_SRC)
aux_source_directory(./idb_to_imp_db/ PY_IMPDB_SRC)
aux_source_directory(./invoke_ista/ PY_MACRO_ISTA_SRC)
include_directories(./invoke_ista/)
include_directories(./invoke_ista/)
include_directories(./)

add_library(py_imp ${PY_IMP_SRC} 
${PY_IMPDB_SRC}
${PY_MACRO_ISTA_SRC}
)

target_link_libraries(py_imp
    PUBLIC
    # imp_api
    idm
    str
    sta
    # power
    pybind11::pybind11
)

target_include_directories(py_imp
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # ${CMAKE_SOURCE_DIR}/third_party/iEDA/src/operation/iPW/api/
    ${HOME_OPERATION}
    ${HOME_PLATFORM}/data_manager
    ${HOME_DATABASE}/data/design)
