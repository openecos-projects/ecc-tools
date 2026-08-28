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

bool isInvailidNet(IdbNet* net);

std::string IdbOrientToString(IdbOrient orient);

inline bool isPlacementFixed(idb::IdbInstance* node)
{
  const auto status = node->get_status();
  if (status == idb::IdbPlacementStatus::kFixed) {
    return true;
  }

  auto* cell_master = node->get_cell_master();
  return cell_master != nullptr && cell_master->is_block()
         && (status == idb::IdbPlacementStatus::kPlaced || status == idb::IdbPlacementStatus::kCover);
}

/// database for python
struct PyPlaceDB
{
 public:
  unsigned int num_nodes;           ///< number of nodes, including terminals and terminal_NIs
  unsigned int num_terminals;       ///< number of terminals, essentially fixed macros
  unsigned int num_terminal_NIs;    ///< number of terminal_NIs, essentially IO pins
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
  pybind11::list net2pin_map;             ///< array of 1D array, each row stores pin id
  pybind11::list flat_net2pin_map;        ///< flatten version of net2pin_map
  pybind11::list flat_net2pin_start_map;  ///< starting index of each net in flat_net2pin_map
  pybind11::list net_weights;             ///< net weight

  pybind11::list node2pin_map;             ///< array of 1D array, contains pin id of each node
  pybind11::list flat_node2pin_map;        ///< flatten version of node2pin_map
  pybind11::list flat_node2pin_start_map;  ///< starting index of each node in flat_node2pin_map

  pybind11::list pin2node_map;  ///< 1D array, contain parent node id of each pin
  pybind11::list pin2net_map;   ///< 1D array, contain parent net id of each pin

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

  int xl;
  int yl;
  int xh;
  int yh;

  int row_height;
  int site_width;
  double total_space_area;  ///< total placeable space area excluding fixed cells.
                            ///< This is not the exact area, because we cannot exclude the overlapping fixed cells
                            ///< within a bin.

  int num_movable_pins;

  PyPlaceDB(idm::DataManager* db, int numRoutingGridsX, int numRoutingGridsY, bool with_routability, bool with_sta)
  {
    set(db, numRoutingGridsX, numRoutingGridsY, with_routability, with_sta);
  }

  const std::vector<bool>& getNodeIsHardMacro() const { return _node_is_hard_macro; }
  const std::vector<bool>& getMacroWritebackCandidate() const { return _macro_writeback_candidate; }
  std::size_t writeMacroPlacementBack(
      const pybind11::array_t<float, pybind11::array::c_style | pybind11::array::forcecast>& movable_x,
      const pybind11::array_t<float, pybind11::array::c_style | pybind11::array::forcecast>& movable_y);

  void set(idm::DataManager* db, int numRoutingGridsX, int numRoutingGridsY, bool with_routability, bool with_sta);
  void init_routability(idm::DataManager* db, std::vector<IdbInstance*> inst_resort_list);
  std::vector<std::vector<float>> getCongestionMap(string method = "max", string stage = "egr3D", string resolve_congestion = "low");

 private:
  struct MacroWritebackCandidate
  {
    index_type node_id;
    std::string instance_name;
    idb::IdbInstance* instance;
  };

  idm::DataManager* _db = nullptr;
  idb::IdbDesign* _design = nullptr;
  std::vector<bool> _node_is_hard_macro;
  std::vector<bool> _macro_writeback_candidate;
  std::vector<MacroWritebackCandidate> _macro_writeback_candidates;
};

}  // namespace python_interface

#endif
