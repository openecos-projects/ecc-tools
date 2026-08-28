#include "PyPlaceDB.h"
#include "utility/logger/Logger.hpp"
// #include "ContestDriver.h"
#include <algorithm>
#include <boost/polygon/polygon.hpp>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbPins.h"
#include "idm.h"
#include <boost/polygon/polygon.hpp>
#include <vector>

namespace python_interface {

double intersectDistance(Box const& i1, Box const& i2, bool is_x)
{
  coordinate_type l;
  coordinate_type h;
  if (is_x) {
    l = std::max(i1.xl, i2.xl);
    h = std::min(i1.xh, i2.xh);
  } else {
    l = std::max(i1.yl, i2.yl);
    h = std::min(i1.yh, i2.yh);
  }
  return (l < h) ? (double) h - l : 0;
}

double intersectArea(Box const& b1, Box const& b2)
{
  double dist[2] = {intersectDistance(b1, b2, true), intersectDistance(b1, b2, false)};
  return dist[0] * dist[1];
}

bool isInvailidNet(IdbNet* net)
{
  return net->is_ground() || net->is_power() || net->is_pdn() || net->is_clock()
         || net->get_instance_pin_list()->get_pin_list().size() == 0;
}

void PyPlaceDB::set(idm::DataManager* db, int numRoutingGridsX, int numRoutingGridsY, bool with_routability, bool with_sta)
{
  ECCLOG.info(ecc::Loc::current(), "PyPlaceDB::set start. Db address is ", db);
  ECCLOG.info(ecc::Loc::current(), "PyPlaceDB::set start. idb_design address is ", db->get_idb_design());
  num_routing_grids_x = numRoutingGridsX;
  num_routing_grids_y = numRoutingGridsY;
  _db = db;
  _design = db->get_idb_design();
  using namespace idb;
  namespace gtl = boost::polygon;
  using namespace gtl::operators;
  typedef gtl::polygon_90_set_data<coordinate_type> PolygonSet;
  IdbDesign* db_deisgn = db->get_idb_design();
  db_deisgn->m_instID2Name.clear();
  num_terminal_NIs = 0;  // IO pins
  dbu = db_deisgn->get_layout()->get_units()->get_micron_dbu();

  if (with_sta) {
    throw std::runtime_error("PyPlaceDB timing initialization is disabled in this ecc_py build");
  }

  double total_fixed_node_area = 0;  // sum of fixed body and synthetic obstacle rectangles
  // Collect the rectangles used by DreamPlace and the corresponding unioned geometry
  // separately.  Fixed instances retain their body rectangle for pin/write-back
  // identity, while halos and blockages become pin-less synthetic terminals.
  std::vector<gtl::rectangle_data<coordinate_type>> fixed_boxes;
  std::vector<gtl::rectangle_data<coordinate_type>> fixed_body_boxes;
  std::vector<gtl::rectangle_data<coordinate_type>> fixed_halo_boxes;
  // record original node to new node mapping
  int inst_num = db_deisgn->get_instance_list()->get_num();
  std::map<std::string, index_type> mNode2PyNondeID;
  std::map<std::string, index_type> mNode2idbID;
  std::vector<IdbInstance*> inst_resort_list = db_deisgn->get_instance_list()->get_instance_list();
  std::stable_sort(inst_resort_list.begin(), inst_resort_list.end(),
                   [](IdbInstance* a, IdbInstance* b) { return isPlacementFixed(a) < isPlacementFixed(b); });
  for (IdbInstance* node : inst_resort_list) {
    mNode2idbID[node->get_name()] = node->get_id();
  }
  std::map<std::string, int> mNet2ID;
  int net_id = 0;
  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    // is special net
    if (isInvailidNet(net)) {
      continue;
    }

    mNet2ID[net->get_net_name()] = net_id++;
  }
  std::unordered_map<std::string, int> mPin2ID;
  int pin_id = 0;
  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    if (isInvailidNet(net)) {
      continue;
    }

    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      std::string inst_name = pin->get_instance()->get_name();
      std::string pin_name = pin->get_pin_name();
      std::string full_pin_name = inst_name + ":" + pin_name;  //: or /
      pin_names.append(full_pin_name);
      mPin2ID[inst_name + pin->get_pin_name()] = pin_id++;
    }

    for (IdbPin* io_pin : net->get_io_pins()->get_pin_list()) {
      std::string inst_name = io_pin->get_pin_name();
      pin_names.append(inst_name);
      mPin2ID[io_pin->get_pin_name()] = pin_id++;
    }
  }
  // add a node to a bin
  auto addNode2Bin = [&](Box const& box) { fixed_boxes.emplace_back(box.xl, box.yl, box.xh, box.yh); };
  auto buildInstanceBox = [](IdbInstance* node) {
    auto* coord = node->get_coordinate();
    auto* box = node->get_bounding_box();
    if (node->get_cell_master() != nullptr && (box->get_width() <= 0 || box->get_height() <= 0)) {
      node->set_bounding_box();
      box = node->get_bounding_box();
    }
    int32_t lx = coord->get_x();
    int32_t ly = coord->get_y();
    if (box->get_width() > 0 && box->get_height() > 0) {
      return Box(lx, ly, box->get_high_x(), box->get_high_y());
    }
    auto* cell_master = node->get_cell_master();
    int32_t width = cell_master == nullptr ? 1 : static_cast<int32_t>(cell_master->get_width());
    int32_t height = cell_master == nullptr ? 1 : static_cast<int32_t>(cell_master->get_height());
    return Box(lx, ly, lx + width, ly + height);
  };
  // general add a node
  auto addNode = [&](std::string orient_str, std::string const& name, Box const& box, bool isFixed, bool is_hard_macro,
                     bool is_macro_writeback_candidate, IdbInstance* instance) {
    // this id may be different from node id
    int id = node_names.size();
    node_name2id_map[pybind11::str(name)] = id;
    node_names.append(pybind11::str(name));
    dmInst->get_idb_design()->m_instID2Name.push_back(name);
    node_x.append(box.xl);
    node_y.append(box.yl);
    node_orient.append(pybind11::str(orient_str));
    node_size_x.append(box.width());
    node_size_y.append(box.height());
    _node_is_hard_macro.push_back(is_hard_macro);
    _macro_writeback_candidate.push_back(is_macro_writeback_candidate);
    if (is_macro_writeback_candidate) {
      _macro_writeback_candidates.push_back({id, name, instance});
    }
    // map new node to original index
    if (mNode2idbID.count(name)) {
      node2orig_node_map.append(mNode2idbID[name]);
    } else {
      node2orig_node_map.append(-1);
    }
    assert(mNode2PyNondeID.count(name) == 0);
    mNode2PyNondeID[name] = id;
    if (isFixed) {
      addNode2Bin(box);
    }
  };
  num_terminals = 0;  // regard only fixed macros as macros, placement blockages are ignored
  for (int i = 0; i < inst_num; ++i) {
    IdbInstance* node = inst_resort_list.at(i);
    auto* cell_master = node->get_cell_master();
    const bool is_hard_macro = cell_master != nullptr && cell_master->is_block();
    const auto status = node->get_status();
    const bool is_macro_writeback_candidate
        = is_hard_macro && (status == IdbPlacementStatus::kNone || status == IdbPlacementStatus::kUnplaced);
    if (is_hard_macro) {
      ECCLOG.info(ecc::Loc::current(), "Node ", node->get_name(), " is a block.");
    }
    if (!isPlacementFixed(node)) {
      Box box_tmp = buildInstanceBox(node);
      if (node->get_halo()) {
        // Jiaqi: add halo for fixed cells
        // ECCLOG.info(ecc::Loc::current(), "PyPlaceDB detects fixed cell with halo.");
        box_tmp.xl -= node->get_halo()->get_extend_lef();
        box_tmp.yl -= node->get_halo()->get_extend_bottom();
        box_tmp.xh += node->get_halo()->get_extend_right();
        box_tmp.yh += node->get_halo()->get_extend_top();
        ECCLOG.info(ecc::Loc::current(), "Instance ", node->get_name(), ", halo (", node->get_halo()->get_extend_lef(), ", ",
                     node->get_halo()->get_extend_bottom(), ", ", node->get_halo()->get_extend_right(), ", ",
                     node->get_halo()->get_extend_top(), ").");
      }
      addNode(IdbOrientToString(node->get_orient()), node->get_name(), box_tmp, false, is_hard_macro,
              is_macro_writeback_candidate, node);
    }
    else
    {
      Box body_box = buildInstanceBox(node);
      Box halo_box = body_box;
      if (node->get_halo()) {
        halo_box.xl -= node->get_halo()->get_extend_lef();
        halo_box.yl -= node->get_halo()->get_extend_bottom();
        halo_box.xh += node->get_halo()->get_extend_right();
        halo_box.yh += node->get_halo()->get_extend_top();
        ECCLOG.info(ecc::Loc::current(), "Macro instance ", node->get_name(), ", halo (", node->get_halo()->get_extend_lef(),
                     ", ", node->get_halo()->get_extend_bottom(), ", ", node->get_halo()->get_extend_right(), ", ",
                     node->get_halo()->get_extend_top(), ").");
      }
      // Keep the real instance as a body-only terminal.  Its halo is added to
      // the unioned obstacle set below so overlapping halos cannot be counted
      // multiple times as independent fixed nodes.
      addNode(IdbOrientToString(node->get_orient()), node->get_name(), body_box, true, is_hard_macro,
              is_macro_writeback_candidate, node);
      if (node->get_cell_master()->is_io_cell()) {
        ECCLOG.info(ecc::Loc::current(), "Fixed IO instance ", node->get_name(), ", coordinate (",
                     node->get_coordinate()->get_x(), ", ", node->get_coordinate()->get_y(), ", ",
                     node->get_bounding_box()->get_high_x(), ", ", node->get_bounding_box()->get_high_y(), ").");
      }
      num_terminals += 1;
      total_fixed_node_area += body_box.area();

      fixed_body_boxes.emplace_back(body_box.xl, body_box.yl, body_box.xh, body_box.yh);
      if (node->get_halo()) {
        fixed_halo_boxes.emplace_back(halo_box.xl, halo_box.yl, halo_box.xh, halo_box.yh);
      }
    }
  }

  PolygonSet blockage_ps_list;
  // def blockage list
  for (auto blockage : db->get_idb_design()->get_blockage_list()->get_blockage_list()) {
    if (!blockage->is_palcement_blockage()) {
      continue;
    }
    PolygonSet ps;
    for (auto rect : blockage->get_rect_list()) {
      // convert to absolute box
      Box box(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
      ps.insert(gtl::rectangle_data<coordinate_type>(box.xl, box.yl, box.xh, box.yh));
    }
    blockage_ps_list += ps;
  }
  auto core = db->get_idb_layout()->get_core();
  row_height = db->get_idb_layout()->get_rows()->get_row_height();
  auto second_routing_layer = db->get_idb_layout()->get_layers()->get_routing_layers().at(1);
  assert(second_routing_layer->get_name().find("2") != std::string::npos);
  idb::IdbLayerRouting* second_idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(second_routing_layer);
#if 1
  for (auto* special_net : db_deisgn->get_special_net_list()->get_net_list()) {
    if (special_net->is_vdd() || special_net->is_vss()) {
      for (auto segment : special_net->get_wire_list()->get_wire_list()) {
        for (auto* seg : segment->get_segment_list()) {
          if (seg->is_line()) {
            auto layer = seg->get_layer();
            PolygonSet ps;
            if (layer->is_routing() && layer == second_idb_routing_layer) {
              auto rect = seg->get_bounding_box();

              coordinate_type orig_xl = rect->get_low_x();
              coordinate_type orig_yl = rect->get_low_y();
              coordinate_type orig_xh = rect->get_high_x();
              coordinate_type orig_yh = rect->get_high_y();
              Box box(orig_xl, orig_yl, orig_xh, orig_yh);
              ps.insert(gtl::rectangle_data<coordinate_type>(box.xl, box.yl, box.xh, box.yh));
              blockage_ps_list += ps;
            }
          }
        }
      }
    }
  }
#endif
  // Union all hard obstacles before decomposition.  Subtract only the fixed
  // body union: halo area outside the body must remain unavailable, while any
  // overlap between halos, blockages, and bodies is represented exactly once.
  PolygonSet fixed_body_ps(gtl::HORIZONTAL, fixed_body_boxes.begin(), fixed_body_boxes.end());
  PolygonSet fixed_halo_ps(gtl::HORIZONTAL, fixed_halo_boxes.begin(), fixed_halo_boxes.end());
  PolygonSet obstacle_ps = blockage_ps_list;
  obstacle_ps += fixed_halo_ps;
  obstacle_ps -= fixed_body_ps;
  int ext_blockage_num = 0;

  std::vector<gtl::rectangle_data<coordinate_type>> vRect;
  obstacle_ps.get_rectangles(vRect);
  for (auto const& rect : vRect) {
    Box box(gtl::xl(rect), gtl::yl(rect), gtl::xh(rect), gtl::yh(rect));
    int id = node_names.size();
    string block_name = "blockage" + std::to_string(id);
    ECCLOG.info(ecc::Loc::current(), "PyPlaceDB detects fixed blockage ", block_name, ", (", box.xl, ", ", box.yl, ", ",
                 box.xh, ", ", box.yh, ").");
    addNode("R0", block_name, box, true, false, false, nullptr);
    total_fixed_node_area += 1LL * box.area();
  }
  num_terminals += vRect.size();
  ext_blockage_num += vRect.size();

  // IO PINS
  for (auto io_pin : db_deisgn->get_io_pin_list()->get_pin_list()) {
    int lx = 0;
    int ly = 0;
    IdbTerm* term = io_pin->get_term();
    if (term->is_port_exist()) {
      for (auto port : term->get_port_list()) {
        lx = port->get_io_average_coordinate()->get_x();
        ly = port->get_io_average_coordinate()->get_y();
        break;
      }
    } else {
      lx = io_pin->get_location()->get_x();
      ly = io_pin->get_location()->get_y();
    }
    Box box_tmp(lx, ly, lx + 1, ly + 1);
    addNode("R0", io_pin->get_pin_name(), box_tmp, false, false, false, nullptr);
    ECCLOG.info(ecc::Loc::current(), "IO pin ", io_pin->get_pin_name(), ", coordinate (", lx, ", ", ly, ").");
    num_terminal_NIs += 1;
  }
  // we only know num_nodes when all fixed cells with shapes are expanded
  ECCLOG.info(ecc::Loc::current(), "num_terminals ", num_terminals, ", num_place_blockages ", ext_blockage_num,
               ", num_terminal_NIs ", num_terminal_NIs, ".");
  num_nodes = inst_num + ext_blockage_num + num_terminal_NIs;  // db.nodes().size() + num_terminals - db.numFixed() - db.numPlaceBlockages()
  // Compute the exact fixed/obstacle area inside the core from the same union
  // represented by the terminal rectangles above.
  PolygonSet ps(gtl::HORIZONTAL, fixed_boxes.begin(), fixed_boxes.end());
  // critical to make sure only overlap with the die area is computed
  IdbRect* core_rect = db->get_idb_layout()->get_core()->get_bounding_box();
  auto core_box = gtl::rectangle_data<coordinate_type>(core_rect->get_low_x(), core_rect->get_low_y(), core_rect->get_high_x(),
                                                       core_rect->get_high_y());
  ps &= core_box;
  double total_fixed_geometry_area = gtl::area(ps);
  total_space_area = core_rect->get_area() - total_fixed_geometry_area;

  PolygonSet body_core_ps(gtl::HORIZONTAL, fixed_body_boxes.begin(), fixed_body_boxes.end());
  body_core_ps &= core_box;
  PolygonSet halo_core_ps(gtl::HORIZONTAL, fixed_halo_boxes.begin(), fixed_halo_boxes.end());
  halo_core_ps &= core_box;
  PolygonSet residual_obstacle_core_ps(gtl::HORIZONTAL, vRect.begin(), vRect.end());
  residual_obstacle_core_ps &= core_box;
  ECCLOG.info(ecc::Loc::current(), "PyPlaceDB fixed geometry: body_union_area ", gtl::area(body_core_ps),
               ", halo_union_area ", gtl::area(halo_core_ps), ", residual_obstacle_area ",
               gtl::area(residual_obstacle_core_ps), ", terminal_union_area ", total_fixed_geometry_area,
               ", terminal_area_sum ", total_fixed_node_area, ", synthetic_rectangles ", ext_blockage_num, ".");
  int count = 0;
  for (int i = 0; i < mNode2PyNondeID.size() - num_terminal_NIs - ext_blockage_num; ++i) {
    auto node_name = node_names[i].cast<std::string>();
    IdbInstance* node = inst_resort_list[mNode2PyNondeID[node_name]];
    pybind11::list pins;
    int pin_num = 0;
    for (IdbPin* pin : node->get_pin_list()->get_pin_list()) {
      string inst_name = pin->get_instance()->get_name();
      // assert(mPin2ID.count(inst_name + pin->get_pin_name()));
      if (!mPin2ID.count(inst_name + pin->get_pin_name())) {
        continue;
      }
      int pin_id = mPin2ID[inst_name + pin->get_pin_name()];
      pins.append(pin_id);
      flat_node2pin_map.append(pin_id);
      pin_num += 1;
    }
    node2pin_map.append(pins);
    flat_node2pin_start_map.append(count);
    count += pin_num;
  }
  for (int i = 0; i < ext_blockage_num; i++) {
    node2pin_map.append(pybind11::list());
    flat_node2pin_start_map.append(count);
  }
  for (int i = mNode2PyNondeID.size() - num_terminal_NIs; i < mNode2PyNondeID.size(); ++i) {
    auto io_pin_name = node_names[i].cast<std::string>();
    pybind11::list pins;
    int pin_num = 0;
    if (mPin2ID.count(io_pin_name)) {
      int pin_id = mPin2ID[io_pin_name];
      pins.append(pin_id);
      flat_node2pin_map.append(pin_id);
      pin_num = 1;
    }
    node2pin_map.append(pins);
    flat_node2pin_start_map.append(count);
    count += pin_num;
  }
  flat_node2pin_start_map.append(count);

  num_movable_pins = 0;
  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    if (isInvailidNet(net)) {
      continue;
    }
    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      // Pin const& pin = db.pin(i);
      IdbInstance* node = pin->get_instance();
      pin_direct.append(pybind11::str(IdbOrientToString(node->get_orient())));
      // for fixed macros with multiple boxes, put all pins to the first one
      string temp_name = node->get_name();
      // if (mNode2PyNondeID.find(node->get_name()) == mNode2PyNondeID.end()
      //     || (mNode2PyNondeID.find(node->get_name()) != mNode2PyNondeID.end() && mNode2PyNondeID[node->get_name()].size() == 0)) {
      //   ECCLOG.warn(ecc::Loc::current(), "Node ", node->get_name(), " is not found in mNode2PyNondeID.");
      // } else if (mNode2PyNondeID[node->get_name()].size() == 0) {
      //   ECCLOG.warn(ecc::Loc::current(), "Node ", node->get_name(), " has no new nodes.");
      // }
      assert(mNode2PyNondeID.count(temp_name));
      index_type new_node_id = mNode2PyNondeID[node->get_name()];  //==0
      IdbCoordinate<int>* inst_coord = pin->get_instance()->get_coordinate();
      IdbCoordinate<int>* pin_coord = pin->get_average_coordinate();
      // Pin::point_type pin_pos(node.pinPos(pin));
      pin_offset_x.append(pin_coord->get_x() - inst_coord->get_x());
      pin_offset_y.append(pin_coord->get_y() - inst_coord->get_y());
      pin2node_map.append(new_node_id);
      assert(mNet2ID.count(pin->get_net()->get_net_name()));
      pin2net_map.append(mNet2ID[pin->get_net()->get_net_name()]);

      if (!isPlacementFixed(node) /*&& node.status() != PlaceStatusEnum::DUMMY_FIXED*/) {
        num_movable_pins += 1;
      }
    }

    for (IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      // Pin const& pin = db.pin(i);
      string io_pin_name = pin->get_pin_name();
      pin_direct.append(pybind11::str("R0"));
      // for fixed macros with multiple boxes, put all pins to the first one
      assert(mNode2PyNondeID.count(io_pin_name));
      index_type new_node_id = mNode2PyNondeID[io_pin_name];
      // Pin::point_type pin_pos(node.pinPos(pin));
      pin_offset_x.append(0);
      pin_offset_y.append(0);
      pin2node_map.append(new_node_id);
      pin2net_map.append(mNet2ID[pin->get_net()->get_net_name()]);
    }
  }

  count = 0;
  for (IdbNet* net : db_deisgn->get_net_list()->get_net_list()) {
    if (isInvailidNet(net)) {
      continue;
    }
    // Net const& net = db.net(i);
    net_weights.append(1);
    net_name2id_map[pybind11::str(net->get_net_name())] = mNet2ID[net->get_net_name()];
    net_names.append(pybind11::str(net->get_net_name()));
    pybind11::list pins;
    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      string inst_name = pin->get_instance()->get_name();
      assert(mPin2ID.count(inst_name + pin->get_pin_name()));
      pins.append(mPin2ID[inst_name + pin->get_pin_name()]);
    }
    for (IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      assert(mPin2ID.count(pin->get_pin_name()));
      pins.append(mPin2ID[pin->get_pin_name()]);
    }
    net2pin_map.append(pins);

    // Make driving pin the first pin
    IdbPin* driver = net->get_driving_pin();
    std::string driver_name;
    int pin_num;
    if (driver) {
      if (driver->get_instance() != nullptr) {  // Instance Pin
        driver_name = driver->get_instance()->get_name() + driver->get_pin_name();
      } else {  // IO Pin
        driver_name = driver->get_pin_name();
      }
      flat_net2pin_map.append(mPin2ID[driver_name]);
      pin_num = 1;  // include driving pin
    } else {
      ECCLOG.error(ecc::Loc::current(), "Net ", net->get_net_name(), " has no driver.");
      pin_num = 0;
    }
    for (IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      if (pin == driver) {
        continue;
      }
      string inst_name = pin->get_instance()->get_name();
      assert(mPin2ID.count(inst_name + pin->get_pin_name()));
      flat_net2pin_map.append(mPin2ID[inst_name + pin->get_pin_name()]);
      pin_num += 1;
    }

    for (IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      if (pin == driver) {
        continue;
      }
      flat_net2pin_map.append(mPin2ID[pin->get_pin_name()]);
      pin_num += 1;
    }
    flat_net2pin_start_map.append(count);
    count += pin_num;
  }
  flat_net2pin_start_map.append(count);

  for (IdbRow* idb_row : db->get_idb_layout()->get_rows()->get_row_list()) {
    IdbRect* row_rect = idb_row->get_bounding_box();
    pybind11::tuple row
        = pybind11::make_tuple(row_rect->get_low_x(), row_rect->get_low_y(), row_rect->get_high_x(), row_rect->get_high_y());
    rows.append(row);
  }

  // initialize regions
  count = 0;

  for (IdbRegion* region : db->get_idb_design()->get_region_list()->get_region_list()) {
    // Region const& region = *it;
    pybind11::list boxes;
    for (IdbRect* itb : region->get_boundary()) {
      pybind11::tuple box = pybind11::make_tuple(itb->get_low_x(), itb->get_low_y(), itb->get_high_x(), itb->get_high_y());
      boxes.append(box);
      flat_region_boxes.append(box);
    }
    regions.append(boxes);
    flat_region_boxes_start.append(count);
    count += region->get_boundary().size();
  }
  flat_region_boxes_start.append(count);

  // I assume one cell only belongs to one FENCE region
  std::vector<int> vNode2FenceRegion(inst_num, std::numeric_limits<int>::max());
  int region_id = 0;
  for (IdbRegion* region : db->get_idb_design()->get_region_list()->get_region_list()) {
    // Group const& group = *it;
    if (region->get_type() == IdbRegionType::kFence) {
      for (IdbInstance* inst : region->get_instance_list()) {
        // FIXME:
        index_type node_id = mNode2PyNondeID[inst->get_name()];
        if (!isPlacementFixed(inst))  // ignore fixed cells
        {
          vNode2FenceRegion.at(node_id) = region_id;
        }
      }
    }
    region_id++;
  }
  for (std::vector<int>::const_iterator it = vNode2FenceRegion.begin(), ite = vNode2FenceRegion.end(); it != ite; ++it) {
    node2fence_region_map.append(*it);
  }
  // auto core = db->get_idb_layout()->get_core();
  xl = core->get_bounding_box()->get_low_x();
  yl = core->get_bounding_box()->get_low_y();
  xh = core->get_bounding_box()->get_high_x();
  yh = core->get_bounding_box()->get_high_y();

  assert(yl >= 0 && xl >= 0 && xh > xl && yh > yl);
  row_height = db->get_idb_layout()->get_rows()->get_row_height();
  site_width = db->get_idb_layout()->get_rows()->get_row_list().at(0)->get_site()->get_width();
#if 1

  if (with_routability) {
    init_routability(db, inst_resort_list);
  }
  ECCLOG.info(ecc::Loc::current(), "PyPlaceDB::set end.");

#endif
}

std::size_t PyPlaceDB::writeMacroPlacementBack(
    const pybind11::array_t<float, pybind11::array::c_style | pybind11::array::forcecast>& movable_x,
    const pybind11::array_t<float, pybind11::array::c_style | pybind11::array::forcecast>& movable_y)
{
  const auto x = movable_x.request();
  const auto y = movable_y.request();
  const auto num_movable_nodes = static_cast<pybind11::ssize_t>(num_nodes - num_terminals - num_terminal_NIs);
  if (x.ndim != 1 || y.ndim != 1 || x.size != y.size || x.size != num_movable_nodes) {
    throw std::invalid_argument("Macro placement writeback requires equal one-dimensional arrays for every movable node");
  }
  if (_db == nullptr || _design == nullptr || _db->get_idb_design() != _design) {
    throw std::runtime_error("Macro placement snapshot no longer matches the active iDB design");
  }

  const auto* node_x = static_cast<const float*>(x.ptr);
  const auto* node_y = static_cast<const float*>(y.ptr);
  std::vector<idm::InstancePlacementUpdate> updates;
  updates.reserve(_macro_writeback_candidates.size());
  for (const auto& candidate : _macro_writeback_candidates) {
    if (candidate.node_id < 0 || candidate.node_id >= num_movable_nodes) {
      throw std::runtime_error("Frozen macro candidate is outside the movable node range");
    }
    const float candidate_x = node_x[candidate.node_id];
    const float candidate_y = node_y[candidate.node_id];
    const double checked_x = static_cast<double>(candidate_x);
    const double checked_y = static_cast<double>(candidate_y);
    if (!std::isfinite(candidate_x) || !std::isfinite(candidate_y)
        || checked_x < static_cast<double>(std::numeric_limits<int32_t>::lowest())
        || checked_x > static_cast<double>(std::numeric_limits<int32_t>::max())
        || checked_y < static_cast<double>(std::numeric_limits<int32_t>::lowest())
        || checked_y > static_cast<double>(std::numeric_limits<int32_t>::max())) {
      throw std::invalid_argument("Macro placement writeback coordinates must be finite int32-compatible values");
    }
    updates.push_back(
        {candidate.instance_name, candidate.instance, static_cast<int32_t>(candidate_x), static_cast<int32_t>(candidate_y)});
  }
  return _db->write_selected_placement_back(updates);
}

}  // namespace python_interface
   // namespace python_interface
