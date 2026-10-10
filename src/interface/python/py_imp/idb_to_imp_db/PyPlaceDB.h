/**
 * @file   PyPlaceDB.h
 * @author Yibo Lin
 * @date   Apr 2020
 * @brief  Placement database for python
 */

#ifndef _DREAMPLACE_PLACE_IO_PYPLACEDB_H
#define _DREAMPLACE_PLACE_IO_PYPLACEDB_H

#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "IdbEnum.h"
#include "IdbInstance.h"
#include "idm.h"

namespace ista {
struct TimingSnapshot;
}

namespace python_interface {
typedef int coordinate_type;
typedef int index_type;

struct Box
{
  coordinate_type xl, yl, xh, yh;
  Box(coordinate_type xl, coordinate_type yl, coordinate_type xh, coordinate_type yh) : xl(xl), yl(yl), xh(xh), yh(yh) {}
  coordinate_type width() const { return xh - xl; }
  coordinate_type height() const { return yh - yl; }
  int64_t area() const { return 1LL * width() * height(); }
};

double intersectDistance(Box const& i1, Box const& i2, bool is_x);

/// \return the intersection area of two boxes
double intersectArea(Box const& b1, Box const& b2);

bool isInvailidNet(IdbNet* net, bool with_sta = false);

std::string IdbOrientToString(IdbOrient orient);

inline bool isPlacementFixed(idb::IdbInstance* node)
{
  const auto status = node->get_status();
  if (status == idb::IdbPlacementStatus::kFixed) {
    return true;
  }

  auto* cell_master = node->get_cell_master();
  // Preplaced IO pads are fixed physical terminals even though PAD is not BLOCK.
  return cell_master != nullptr && (cell_master->is_block() || cell_master->is_io_cell())
         && (status == idb::IdbPlacementStatus::kPlaced || status == idb::IdbPlacementStatus::kCover);
}

/// database for python
struct PyPlaceDB
{
 public:
  unsigned int num_nodes;           ///< number of nodes, including terminals and terminal_NIs
  unsigned int num_terminals;       ///< number of terminals, essentially fixed macros
  unsigned int num_terminal_NIs;    ///< number of terminal_NIs, essentially IO pins
  unsigned int m2_pg_rail_blockage_rects;
  pybind11::list m2_pg_rail_boxes;
  pybind11::list m2_pg_rail_density_boxes;
  pybind11::dict node_name2id_map;  ///< node name to id map, cell name
  pybind11::list node_names;        ///< 1D array, cell name
  pybind11::list node_x;            ///< 1D array, cell position x
  pybind11::list node_y;            ///< 1D array, cell position y
  pybind11::list node_orient;       ///< 1D array, cell orientation
  pybind11::list node_size_x;       ///< 1D array, cell width
  pybind11::list node_size_y;       ///< 1D array, cell height

  pybind11::list node2orig_node_map;  ///< due to some fixed nodes may have non-rectangular shapes, we flat the node
                                      ///< list; this map maps the new indices back to the original ones

  pybind11::list pin_direct;    ///< 1D array, pin direction IO
  pybind11::list pin_offset_x;  ///< 1D array, pin offset x to its node
  pybind11::list pin_offset_y;  ///< 1D array, pin offset y to its node
  pybind11::list pin_names;     ///< 1D array, pin name

  pybind11::dict net_name2id_map;         ///< net name to id map
  pybind11::list net_names;               ///< net name
  pybind11::list clock_net_names;         ///< clock identities; retained for timing with zero placement weight
  pybind11::list net2pin_map;             ///< array of 1D array, each row stores pin id
  pybind11::list flat_net2pin_map;        ///< flatten version of net2pin_map
  pybind11::list flat_net2pin_start_map;  ///< starting index of each net in flat_net2pin_map
  pybind11::list net_weights;             ///< net weight

  pybind11::list node2pin_map;             ///< array of 1D array, contains pin id of each node
  pybind11::list flat_node2pin_map;        ///< flatten version of node2pin_map
  pybind11::list flat_node2pin_start_map;  ///< starting index of each node in flat_node2pin_map

  pybind11::list pin2node_map;  ///< 1D array, contain parent node id of each pin
  pybind11::list pin2net_map;   ///< 1D array, contain parent net id of each pin

  // Timing export.  Indices are design-pin ids, times are ps, capacitances
  // are pF, and CSR offsets are half-open [start, end) ranges.
  int timing_schema_version = 0;
  pybind11::list timing_diagnostics;
  pybind11::list timing_clock_names;
  pybind11::list timing_clock_info;
  double timing_source_time_unit_ps = 1.0;
  double timing_source_capacitance_unit_pf = 1.0;
  double timing_source_resistance_unit_ohm = 1.0;
  std::string timing_unit_provenance;
  std::string timing_sdc_path;
  std::string timing_spef_path;
  std::string timing_parasitics_initialization;
  pybind11::list start_points;
  pybind11::list end_points;
  pybind11::list clock_pins;
  pybind11::list FF_ids;
  pybind11::list clk_pin_r_aat;
  pybind11::list clk_pin_f_aat;
  pybind11::list clk_pin_rtran;
  pybind11::list clk_pin_ftran;
  pybind11::list clk_pin_names;
  pybind11::list inrdelays;
  pybind11::list infdelays;
  pybind11::list inrtrans;
  pybind11::list inftrans;
  pybind11::list outcaps;
  pybind11::list endpoints_rRAT;
  pybind11::list endpoints_fRAT;
  pybind11::list backend_endpoint_rAAT;
  pybind11::list backend_endpoint_fAAT;
  pybind11::list backend_endpoint_rRAT;
  pybind11::list backend_endpoint_fRAT;
  pybind11::list backend_endpoint_rSlew;
  pybind11::list backend_endpoint_fSlew;
  pybind11::list backend_endpoint_min_rAAT;
  pybind11::list backend_endpoint_min_fAAT;
  pybind11::list backend_endpoint_min_rRAT;
  pybind11::list backend_endpoint_min_fRAT;
  pybind11::list net2driver_pin_map;
  pybind11::list net_flat_arcs_start;
  pybind11::list net_flat_arcs;
  pybind11::list inst_flat_arcs_start;
  pybind11::list inst_flat_arcs;
  pybind11::list flat_inst_arcs_by_level;
  pybind11::list flat_inst_arcs_by_level_start;
  pybind11::list flat_pin_to_graph;
  pybind11::list flat_pin_to_graph_start;
  pybind11::list flat_pin_to_graph_reverse;
  pybind11::list flat_pin_to_graph_start_reverse;
  pybind11::list pin_pair_arc_keys;
  pybind11::list flat_pin_pair_arc_start;
  pybind11::list flat_pin_pair_arc_indices;
  pybind11::list arc_level_start;
  pybind11::list arc_src_pin;
  pybind11::list arc_dst_pin;
  pybind11::list arc_inst_id;
  pybind11::list arc_libcell_id;
  pybind11::list arc_libarc_id;
  pybind11::list arc_sense;
  pybind11::list arc_type;
  pybind11::list arc_offset;
  pybind11::list pin_pred_start;
  pybind11::list pin_pred_pin;
  pybind11::list pin_pred_arc_id;
  pybind11::list pin_succ_start;
  pybind11::list pin_succ_pin;
  pybind11::list pin_succ_arc_id;
  pybind11::list endpoint_pin_ids;
  pybind11::list start_pin_ids;
  pybind11::list pin_to_inst_id;
  pybind11::list pin_to_node_id;
  pybind11::list inst_topo_start;
  pybind11::list inst_topo_ids;
  pybind11::list endpoints_constraint_arcs;
  pybind11::list endpoints_timing_check_arcs;

  pybind11::list flat_libcell_info;
  int32_t buffer_main_type_index = -1;
  std::string buffer_main_type_status = "unsupported";
  pybind11::list buffer_main_type_candidate_indices;
  pybind11::list flat_libcell_names;
  pybind11::list flat_libcell_width;
  pybind11::list flat_libcell_height;
  pybind11::list flat_libcell_leakage;
  pybind11::list flat_libcell_main_id2size_vt_limit;
  pybind11::list main_id_is_sizeable;
  pybind11::list main_id_2_cell_id_start;
  pybind11::list inst_main_id;
  pybind11::list inst_libcell_offset;
  pybind11::list cell_id_2_arc_id_start;
  pybind11::list cell_id_2_libpin_id_start;
  pybind11::list pin_2_libpin_offset;
  pybind11::list flat_lib_pin_offset_x;
  pybind11::list flat_lib_pin_offset_y;
  pybind11::list flat_lib_pin_cap;
  pybind11::list flat_lib_pin_rcap;
  pybind11::list flat_lib_pin_fcap;
  pybind11::list flat_lib_pin_cap_limit;
  pybind11::list flat_lib_pin_slew_limit;
  pybind11::list flat_libarc_info;
  pybind11::list f_delay_flat_luts_values;
  pybind11::list f_delay_flat_luts_trans_table;
  pybind11::list f_delay_flat_luts_cap_table;
  pybind11::list f_delay_flat_luts_dim;
  pybind11::list r_delay_flat_luts_values;
  pybind11::list r_delay_flat_luts_trans_table;
  pybind11::list r_delay_flat_luts_cap_table;
  pybind11::list r_delay_flat_luts_dim;
  pybind11::list f_trans_flat_luts_values;
  pybind11::list f_trans_flat_luts_trans_table;
  pybind11::list f_trans_flat_luts_cap_table;
  pybind11::list f_trans_flat_luts_dim;
  pybind11::list r_trans_flat_luts_values;
  pybind11::list r_trans_flat_luts_trans_table;
  pybind11::list r_trans_flat_luts_cap_table;
  pybind11::list r_trans_flat_luts_dim;
  double c_unit = 1.0;
  double r_unit = 1.0;

  pybind11::list rows;  ///< NumRows x 4 array, stores xl, yl, xh, yh of each row

  pybind11::list regions;                  ///< array of 1D array, each region contains rectangles
  pybind11::list flat_region_boxes;        ///< flatten version of regions
  pybind11::list flat_region_boxes_start;  ///< starting index of each region in flat_region_boxes

  pybind11::list node2fence_region_map;  ///< only record fence regions for each cell

  unsigned int num_routing_grids_x;  ///< number of routing grids in x
  unsigned int num_routing_grids_y;  ///< number of routing grids in y
  int routing_grid_xl;               ///< routing grid region may be different from placement region
  int routing_grid_yl;
  int routing_grid_xh;
  int routing_grid_yh;
  int routing_grids_size_x;
  int routing_grids_size_y;
  int dbu;                                       ///< database unit, used to convert coordinate to integer
  pybind11::list unit_horizontal_capacities;     ///< number of horizontal tracks of layers per unit distance
  pybind11::list unit_vertical_capacities;       /// number of vertical tracks of layers per unit distance
  pybind11::list initial_horizontal_demand_map;  ///< initial routing demand from fixed cells, indexed by (layer, grid x, grid y)
  pybind11::list initial_vertical_demand_map;    ///< initial routing demand from fixed cells, indexed by (layer, grid x, grid y)
  pybind11::list min_wire_widths;                ///< min wire width for each routing layer
  pybind11::list min_wire_spacings;              ///< min wire spacing for each routing layer

  // A native mutation invalidates this snapshot until a full PyPlaceDB rebuild.
  bool native_state_dirty = false;
  uint64_t native_mutation_epoch = 0;

  int xl;
  int yl;
  int xh;
  int yh;

  int row_height;
  int site_width;
  double total_fixed_node_area;
  double total_space_area;  ///< total placeable space area excluding fixed cells.
                            ///< This is not the exact area, because we cannot exclude the overlapping fixed cells
                            ///< within a bin.

  int num_movable_pins;

  PyPlaceDB(idm::DataManager* db, int numRoutingGridsX, int numRoutingGridsY, bool with_routability, bool with_sta,
            bool include_m2_pg_rail_blockage = false, bool include_m2_pg_rail_density = true)
  {
    set(db, numRoutingGridsX, numRoutingGridsY, with_routability, with_sta, include_m2_pg_rail_blockage,
        include_m2_pg_rail_density);
  }

  const std::vector<bool>& getNodeIsHardMacro() const { return _node_is_hard_macro; }
  const std::vector<bool>& getMacroWritebackCandidate() const { return _macro_writeback_candidate; }
  std::size_t writeMacroPlacementBack(
      const pybind11::array_t<float, pybind11::array::c_style | pybind11::array::forcecast>& movable_x,
      const pybind11::array_t<float, pybind11::array::c_style | pybind11::array::forcecast>& movable_y);
  pybind11::dict applySizing(const std::vector<int>& cell_ids, const std::vector<std::string>& target_master_names);
  pybind11::dict applyBufferActions(const pybind11::list& actions, const std::string& action_digest);

  void set(idm::DataManager* db, int numRoutingGridsX, int numRoutingGridsY, bool with_routability, bool with_sta,
           bool include_m2_pg_rail_blockage = false, bool include_m2_pg_rail_density = true);
  void init_timing(const ista::TimingSnapshot& snapshot, idm::DataManager* db);
  void init_routability(idm::DataManager* db, std::vector<IdbInstance*> inst_resort_list);
  std::vector<std::vector<float>> getCongestionMap(string method = "max", string stage = "egr3D", string resolve_congestion = "low");

 private:
  struct MacroWritebackCandidate
  {
    index_type node_id;
    std::string instance_name;
    uint64_t instance_id;
    idb::IdbOrient orient;
  };

  idm::DataManager* _db = nullptr;
  idb::IdbDesign* _design = nullptr;
  std::vector<bool> _node_is_hard_macro;
  std::vector<bool> _macro_writeback_candidate;
  std::vector<MacroWritebackCandidate> _macro_writeback_candidates;
};

}  // namespace python_interface

#endif
