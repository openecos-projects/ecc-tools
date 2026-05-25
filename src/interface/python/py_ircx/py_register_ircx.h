// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "py_ircx.h"

namespace python_interface {
namespace py = pybind11;

void register_ircx(py::module& m)
{
  m.def("init_rcx", init_rcx, py::arg("thread_number") = ircx::kDefaultThreadCount,
        py::arg("temperature") = ircx::kDefaultOperatingTemperature);
  m.def("read_rcx_corner", read_rcx_corner, py::arg("corner_name"),
        py::arg("itf_file"), py::arg("captab_file"));
  m.def("read_rcx_mapping", read_rcx_mapping, py::arg("mapping_file"));

  m.def("adapt_rcx_db", adapt_rcx_db);
  m.def("build_rcx_topology", build_rcx_topology);
  m.def("build_rcx_environment", build_rcx_environment);
  m.def("build_rcx_process_variation", build_rcx_process_variation);
  m.def("extract_rcx_parasitics", extract_rcx_parasitics);

  m.def("run_rcx", run_rcx, py::arg("config"));
  m.def("report_rcx", report_rcx, py::arg("output_dir") = ".");
}

}  // namespace python_interface
