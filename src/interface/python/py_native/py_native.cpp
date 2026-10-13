// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// Licensed under Mulan PSL v2. See the project LICENSE for details.
#include "py_register_native.h"

#include <pybind11/stl.h>

#include "NativeToolchain.h"

namespace python_interface {
namespace py = pybind11;

void register_native(py::module_& module)
{
  const auto json = py::module_::import("json");
  py::class_<ecc::NativeToolchain>(module, "NativeToolchain")
      .def(py::init<const std::vector<std::string>&, const std::string&>(),
           py::arg("lef_files"), py::arg("def_file"), py::call_guard<py::gil_scoped_release>())
      .def(py::init<const std::string&>(), py::arg("binary_directory"), py::call_guard<py::gil_scoped_release>())
      .def("load_liberty", &ecc::NativeToolchain::loadLiberty, py::call_guard<py::gil_scoped_release>())
      .def("load_sdc", &ecc::NativeToolchain::loadSdc)
      .def("load_spef", &ecc::NativeToolchain::loadSpef, py::call_guard<py::gil_scoped_release>())
      .def("load_reference_verilog", &ecc::NativeToolchain::loadReferenceVerilog,
           py::arg("file"), py::arg("top"), py::call_guard<py::gil_scoped_release>())
      .def("run", [json](ecc::NativeToolchain& flow, const std::string& stage,
                         const std::string& config, const py::dict& options) {
        const auto encoded = json.attr("dumps")(options).cast<std::string>();
        std::string result;
        { py::gil_scoped_release release; result = flow.run(stage, config, encoded); }
        return json.attr("loads")(result);
      }, py::arg("stage"), py::arg("config") = "", py::arg("options") = py::dict())
      .def("summary", [json](const ecc::NativeToolchain& flow) { return json.attr("loads")(flow.summary()); })
      .def("inspect_instance", [json](const ecc::NativeToolchain& flow, const std::string& name) {
        return json.attr("loads")(flow.inspectInstance(name));
      })
      .def("inspect_net", [json](const ecc::NativeToolchain& flow, const std::string& name) {
        return json.attr("loads")(flow.inspectNet(name));
      })
      .def("write_def", &ecc::NativeToolchain::writeDef, py::call_guard<py::gil_scoped_release>())
      .def("write_binary", &ecc::NativeToolchain::writeBinary, py::call_guard<py::gil_scoped_release>());
}
}  // namespace python_interface
