#pragma once

#include "ClockTreeSnapshot.h"

#include <pybind11/pybind11.h>

#include <cstdint>
#include <optional>

namespace python_interface {

std::optional<ecc::geometry::ClockTreeSnapshot> clockTreeSnapshotFromPython(
    const pybind11::object& graph, std::uint32_t dbu_per_micron);

}  // namespace python_interface
