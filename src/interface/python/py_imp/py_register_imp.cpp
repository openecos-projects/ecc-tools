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

#define GLOG_NO_ABBREVIATED_SEVERITIES
#include "py_register_imp.h"

#include <pybind11/cast.h>

#include "idb_to_imp_db/PyPlaceDB.h"

namespace python_interface {
namespace py = pybind11;

void register_imp(pybind11::module& m)
{
  pybind11::class_<PyPlaceDB>(m, "PyPlaceDB")
      .def("getCongestionMap", &PyPlaceDB::getCongestionMap, py::arg("method") = "max", py::arg("stage") = "egr3D", py::arg("resolve_congestion") = "low")
      .def_readwrite("num_nodes", &PyPlaceDB::num_nodes)
      .def_readwrite("num_terminals", &PyPlaceDB::num_terminals)
      .def_readwrite("num_terminal_NIs", &PyPlaceDB::num_terminal_NIs)
      .def_readwrite("node_name2id_map", &PyPlaceDB::node_name2id_map)
      .def_readwrite("node_names", &PyPlaceDB::node_names)
      .def_readwrite("node_x", &PyPlaceDB::node_x)
      .def_readwrite("node_y", &PyPlaceDB::node_y)
      .def_readwrite("node_orient", &PyPlaceDB::node_orient)
      .def_readwrite("node_size_x", &PyPlaceDB::node_size_x)
      .def_readwrite("node_size_y", &PyPlaceDB::node_size_y)
      .def_readwrite("node2orig_node_map", &PyPlaceDB::node2orig_node_map)
      .def_readwrite("pin_direct", &PyPlaceDB::pin_direct)
      .def_readwrite("pin_offset_x", &PyPlaceDB::pin_offset_x)
      .def_readwrite("pin_offset_y", &PyPlaceDB::pin_offset_y)
      .def_readwrite("pin_names", &PyPlaceDB::pin_names)
      .def_readwrite("net_name2id_map", &PyPlaceDB::net_name2id_map)
      // .def_readwrite("pin_name2id_map", &PyPlaceDB::pin_name2id_map)
      .def_readwrite("net_names", &PyPlaceDB::net_names)
      .def_readwrite("net2pin_map", &PyPlaceDB::net2pin_map)
      .def_readwrite("flat_net2pin_map", &PyPlaceDB::flat_net2pin_map)
      .def_readwrite("flat_net2pin_start_map", &PyPlaceDB::flat_net2pin_start_map)
      .def_readwrite("net_weights", &PyPlaceDB::net_weights)
      // .def_readwrite("net_weight_deltas", &PyPlaceDB::net_weight_deltas)
      // .def_readwrite("net_criticality", &PyPlaceDB::net_criticality)
      // .def_readwrite("net_criticality_deltas", &PyPlaceDB::net_criticality_deltas)
      .def_readwrite("node2pin_map", &PyPlaceDB::node2pin_map)
      .def_readwrite("flat_node2pin_map", &PyPlaceDB::flat_node2pin_map)
      .def_readwrite("flat_node2pin_start_map", &PyPlaceDB::flat_node2pin_start_map)
      .def_readwrite("regions", &PyPlaceDB::regions)
      .def_readwrite("flat_region_boxes", &PyPlaceDB::flat_region_boxes)
      .def_readwrite("flat_region_boxes_start", &PyPlaceDB::flat_region_boxes_start)
      .def_readwrite("node2fence_region_map", &PyPlaceDB::node2fence_region_map)
      .def_readwrite("pin2node_map", &PyPlaceDB::pin2node_map)
      .def_readwrite("pin2net_map", &PyPlaceDB::pin2net_map)
      .def_readwrite("rows", &PyPlaceDB::rows)
      .def_readwrite("xl", &PyPlaceDB::xl)
      .def_readwrite("yl", &PyPlaceDB::yl)
      .def_readwrite("xh", &PyPlaceDB::xh)
      .def_readwrite("yh", &PyPlaceDB::yh)
      .def_readwrite("row_height", &PyPlaceDB::row_height)
      .def_readwrite("site_width", &PyPlaceDB::site_width)
      .def_readwrite("total_space_area", &PyPlaceDB::total_space_area)
      .def_readwrite("num_movable_pins", &PyPlaceDB::num_movable_pins)
      .def_readwrite("num_routing_grids_x", &PyPlaceDB::num_routing_grids_x)
      .def_readwrite("num_routing_grids_y", &PyPlaceDB::num_routing_grids_y)
      .def_readwrite("routing_grid_xl", &PyPlaceDB::routing_grid_xl)
      .def_readwrite("routing_grid_yl", &PyPlaceDB::routing_grid_yl)
      .def_readwrite("routing_grid_xh", &PyPlaceDB::routing_grid_xh)
      .def_readwrite("routing_grid_yh", &PyPlaceDB::routing_grid_yh)
      .def_readwrite("unit_horizontal_capacities", &PyPlaceDB::unit_horizontal_capacities)
      .def_readwrite("unit_vertical_capacities", &PyPlaceDB::unit_vertical_capacities)
      .def_readwrite("initial_horizontal_demand_map", &PyPlaceDB::initial_horizontal_demand_map)
      .def_readwrite("initial_vertical_demand_map", &PyPlaceDB::initial_vertical_demand_map)
      .def_readwrite("dbu", &PyPlaceDB::dbu);

  m.def(
      "pydb",
      [](idm::DataManager* db, int numRoutingGridsX, int numRoutingGridsY, bool with_routability, bool with_sta) {
        return PyPlaceDB(db, numRoutingGridsX, numRoutingGridsY, with_routability, with_sta);
      },
      "Convert PlaceDB to PyPlaceDB");
}

}  // namespace python_interface
