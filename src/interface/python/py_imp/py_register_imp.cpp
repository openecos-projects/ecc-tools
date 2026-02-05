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

// #include "../../../operation/iMP/api/MPAPI.hh"
#include "idb_to_imp_db/PyPlaceDB.h"

// #include "../../../operation/iMP/api/MPAPI.hh"
#include "py_mp.h"
namespace python_interface {
namespace py = pybind11;

void register_imp(pybind11::module& m)
{
  // m.def("SAPlaceSeqPairInt64", imp::SAPlaceSeqPairInt64);

  pybind11::class_<PyPlaceDB>(m, "PyPlaceDB")
      .def(pybind11::init<>())
      /*----------functions ---------*/
      .def("getCongestionMap", &PyPlaceDB::getCongestionMap)
      /*----------end  functions ---------*/
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
      .def_readwrite("min_wire_widths", &PyPlaceDB::min_wire_widths)
      .def_readwrite("min_wire_spacings", &PyPlaceDB::min_wire_spacings)
      .def_readwrite("initial_horizontal_demand_map", &PyPlaceDB::initial_horizontal_demand_map)
      .def_readwrite("initial_vertical_demand_map", &PyPlaceDB::initial_vertical_demand_map)
      .def_readwrite("start_points", &PyPlaceDB::start_points)
      .def_readwrite("end_points", &PyPlaceDB::end_points)
      .def_readwrite("clock_pins", &PyPlaceDB::clock_pins)
      .def_readwrite("FF_ids", &PyPlaceDB::FF_ids)
      .def_readwrite("clk_pin_rtran", &PyPlaceDB::clk_pin_rtran)
      .def_readwrite("clk_pin_ftran", &PyPlaceDB::clk_pin_ftran)
      .def_readwrite("clk_pin_names", &PyPlaceDB::clk_pin_names)
      // .def_readwrite("cells_by_level", &PyPlaceDB::cells_by_level)
      // .def_readwrite("cells_by_reverse_level", &PyPlaceDB::cells_by_reverse_level)
      .def_readwrite("inrdelays", &PyPlaceDB::inrdelays)
      .def_readwrite("infdelays", &PyPlaceDB::infdelays)
      .def_readwrite("inrtrans", &PyPlaceDB::inrtrans)
      .def_readwrite("inftrans", &PyPlaceDB::inftrans)
      .def_readwrite("outcaps", &PyPlaceDB::outcaps)
      .def_readwrite("net_flat_arcs_start", &PyPlaceDB::net_flat_arcs_start)
      .def_readwrite("net_flat_arcs", &PyPlaceDB::net_flat_arcs)
      .def_readwrite("inst_flat_arcs_start", &PyPlaceDB::inst_flat_arcs_start)
      .def_readwrite("inst_flat_arcs", &PyPlaceDB::inst_flat_arcs)
      .def_readwrite("endpoints_constraint_arcs", &PyPlaceDB::endpoints_constraint_arcs)
      .def_readwrite("main_id_2_cell_id_start", &PyPlaceDB::main_id_2_cell_id_start)
      .def_readwrite("cell_id_2_arc_id_start", &PyPlaceDB::cell_id_2_arc_id_start)
      .def_readwrite("inst_main_id", &PyPlaceDB::inst_main_id)
      .def_readwrite("inst_size", &PyPlaceDB::inst_size)
      .def_readwrite("f_delay_flat_luts_values", &PyPlaceDB::f_delay_flat_luts_values)
      .def_readwrite("f_delay_flat_luts_trans_table", &PyPlaceDB::f_delay_flat_luts_trans_table)
      .def_readwrite("f_delay_flat_luts_cap_table", &PyPlaceDB::f_delay_flat_luts_cap_table)
      .def_readwrite("f_delay_flat_luts_dim", &PyPlaceDB::f_delay_flat_luts_dim)
      .def_readwrite("r_delay_flat_luts_values", &PyPlaceDB::r_delay_flat_luts_values)
      .def_readwrite("r_delay_flat_luts_trans_table", &PyPlaceDB::r_delay_flat_luts_trans_table)
      .def_readwrite("r_delay_flat_luts_cap_table", &PyPlaceDB::r_delay_flat_luts_cap_table)
      .def_readwrite("r_delay_flat_luts_dim", &PyPlaceDB::r_delay_flat_luts_dim)
      .def_readwrite("f_trans_flat_luts_values", &PyPlaceDB::f_trans_flat_luts_values)
      .def_readwrite("f_trans_flat_luts_trans_table", &PyPlaceDB::f_trans_flat_luts_trans_table)
      .def_readwrite("f_trans_flat_luts_cap_table", &PyPlaceDB::f_trans_flat_luts_cap_table)
      .def_readwrite("f_trans_flat_luts_dim", &PyPlaceDB::f_trans_flat_luts_dim)
      .def_readwrite("r_trans_flat_luts_values", &PyPlaceDB::r_trans_flat_luts_values)
      .def_readwrite("r_trans_flat_luts_trans_table", &PyPlaceDB::r_trans_flat_luts_trans_table)
      .def_readwrite("r_trans_flat_luts_cap_table", &PyPlaceDB::r_trans_flat_luts_cap_table)
      .def_readwrite("r_trans_flat_luts_dim", &PyPlaceDB::r_trans_flat_luts_dim)
      .def_readwrite("flat_cells_by_level", &PyPlaceDB::flat_cells_by_level)
      .def_readwrite("flat_cells_by_reverse_level", &PyPlaceDB::flat_cells_by_reverse_level)
      .def_readwrite("flat_cells_by_level_start", &PyPlaceDB::flat_cells_by_level_start)
      .def_readwrite("net2driver_pin_map", &PyPlaceDB::net2driver_pin_map)
      .def_readwrite("cell_id_2_libpin_id_start", &PyPlaceDB::cell_id_2_libpin_id_start)
      .def_readwrite("pin_2_libpin_offset", &PyPlaceDB::pin_2_libpin_offset)
      .def_readwrite("flat_lib_pin_cap", &PyPlaceDB::flat_lib_pin_cap)
      .def_readwrite("flat_lib_pin_rcap", &PyPlaceDB::flat_lib_pin_rcap)
      .def_readwrite("flat_lib_pin_fcap", &PyPlaceDB::flat_lib_pin_fcap)
      .def_readwrite("flat_lib_pin_cap_limit", &PyPlaceDB::flat_lib_pin_cap_limit)
      .def_readwrite("flat_lib_pin_slew_limit", &PyPlaceDB::flat_lib_pin_slew_limit)
      .def_readwrite("flat_cells_by_reverse_level_start", &PyPlaceDB::flat_cells_by_reverse_level_start)
      .def_readwrite("c_unit", &PyPlaceDB::c_unit)
      .def_readwrite("r_unit", &PyPlaceDB::r_unit)
      .def_readwrite("dbu", &PyPlaceDB::dbu)
      .def_readwrite("endpoints_rRAT", &PyPlaceDB::endpoints_rRAT)
      .def_readwrite("endpoints_fRAT", &PyPlaceDB::endpoints_fRAT);

  // .def("sum_pin_weights", &_pybind::sum_pin_weights);
  m.def(
      "pydb",
      [](idm::DataManager* db, int numRoutingGridsX, int numRoutingGridsY, bool with_routability, bool with_sta) {
        return PyPlaceDB(db, numRoutingGridsX, numRoutingGridsY, with_routability, with_sta);
      },
      "Convert PlaceDB to PyPlaceDB");
  // m.def("SAPlaceSeqPairInt64", imp::SAPlaceSeqPairInt64);
  // m.def("runMP", runMP, py::arg("config"), py::arg("output_tcl") = "");
  // m.def("runRef", runRef, py::arg("output_tcl") = "");
}

}  // namespace python_interface
