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

#include <iostream>

#include "py_db.h"
#include "py_db_op.h"
#include <pybind11/cast.h>
#include <pybind11/numpy.h>
#include <pybind11/stl_bind.h>
namespace python_interface {
namespace py = pybind11;

void register_idb(py::module& m)
{
  m.def("tech_lef_init", initTechLef);
  m.def("lef_init", initLef, py::arg("lef_paths"));
  m.def("def_init", initDef, py::arg("def_path"));
  m.def("verilog_init", initVerilog, py::arg("verilog_path"), py::arg("top_module"));
  m.def("lvs_verilog_init", initLvsVerilog, py::arg("verilog_path"), py::arg("top_module"));
  m.def("lib_init", initLib, py::arg("lib_paths"));
  m.def("sdc_init", initSdc, py::arg("sdc_path"));
  m.def("spef_init", initSpef, py::arg("spef_path"));
  m.def("def_save", saveDef, py::arg("def_name"));
  // TODO:
  m.def("tcl_save", saveMacroTCL, py::arg("tcl_name"));
  m.def("netlist_save", saveNetList, py::arg("netlist_path"), py::arg("exclude_cell_names") = std::set<std::string>{},
        py::arg("is_add_space_for_escape_name") = false);
  m.def("gds_save", saveGDSII, py::arg("gds_name"), py::arg("is_harden") = false);
  m.def("view_json_save", saveViewJson, py::arg("output_dir"), py::arg("json_format") = "pretty", py::arg("compress") = false);
  m.def("geometry_snapshot_save", saveGeometrySnapshot, py::arg("output_dir"));
  m.def("place_instance", placeInstance, py::arg("inst_name"), py::arg("llx"), py::arg("lly"), py::arg("orient"), py::arg("cellmaster"),
        py::arg("source") = "", py::arg("placement_status") = "fixed", py::arg("create_if_missing") = true);
  m.def("initialize_geometry_session", initializeGeometrySession);
  m.def("sync_instance_geometry", syncInstanceGeometry, py::arg("inst_name"));
  m.def("geometry_session_snapshot_save", saveGeometrySessionSnapshot, py::arg("output_dir"));
  m.def("reset_geometry_session", resetGeometrySession);
  m.def("view_json_apply_edits", applyViewJsonEdits, py::arg("edits_path"), py::arg("compress") = false);
  m.def("save_data", saveData, py::arg("path"));
  m.def("reset_data", resetData);
  m.def("load_data", loadData, py::arg("path"));
  m.def("write_soc_json", writeSocJson, py::arg("path"), py::arg("harden_cores") = std::vector<std::string>{});
  m.def("write_abstract_lef", writeAbstractLef, py::arg("output_lef_path"));
}

void register_idb_op(pybind11::module& m)
{
  m.def("set_net", setNet, py::arg("net_name"), py::arg("net_type"));

  pybind11::class_<idm::DataManager>(m, "DataManager").def(pybind11::init<>());

  m.def("get_dmInst", &getDMInst, "A function which returns a DataManager instance", pybind11::return_value_policy::reference);
  m.def(
      "write_placement_back",
      [](idm::DataManager* db, pybind11::array_t<float, pybind11::array::c_style | pybind11::array::forcecast> const& x,
         pybind11::array_t<float, pybind11::array::c_style | pybind11::array::forcecast> const& y) {
        return write_placement_back(db, x, y);
      },
      "Write Placement Solution (float)");
}

}  // namespace python_interface
