// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#pragma once

#include <pybind11/pybind11.h>

namespace python_interface {

void register_eccdb(pybind11::module_& module);

}  // namespace python_interface
