set(PY_IMP_SRC
    py_mp.cpp
    py_register_imp.cpp
    idb_to_imp_db/PyPlaceDB.cpp
    idb_to_imp_db/PyPlaceDBRoutbility.cpp
)
# aux_source_directory(./invoke_ista/ PY_MACRO_ISTA_SRC)  # disabled: depends on iSTA

add_library(py_imp ${PY_IMP_SRC})

target_link_libraries(py_imp
    PUBLIC
    # imp_api
    idm
    eval_congestion_api
    str
    # sta
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
