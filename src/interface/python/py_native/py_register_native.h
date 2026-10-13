// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// Licensed under Mulan PSL v2. See the project LICENSE for details.
#pragma once

#include <pybind11/pybind11.h>

namespace python_interface {
void register_native(pybind11::module_& module);
}
