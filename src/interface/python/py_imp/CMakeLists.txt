set(PY_IMP_SRC
    py_register_imp.cpp
    idb_to_imp_db/PyPlaceDB.cpp
    idb_to_imp_db/PyPlaceDBRoutbility.cpp
)

add_library(py_imp ${PY_IMP_SRC})

target_link_libraries(py_imp
    PUBLIC
    idm
    eval_congestion_api
    pybind11::pybind11
    ecc_logger
)

target_include_directories(py_imp
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HOME_OPERATION}
    ${HOME_PLATFORM}/data_manager
    ${HOME_DATABASE}/data/design)
