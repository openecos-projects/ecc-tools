#include "PyPlaceDB.h"
// #include "ContestDriver.h"
#include <algorithm>
#include <boost/polygon/polygon.hpp>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbPins.h"
#include "Lib.hh"
// #include "PowerEngine.hh"
#include "TimingEngine.hh"
#include "TimingIDBAdapter.hh"
#include "idm.h"
#include "netlist/Instance.hh"
#include "netlist/Pin.hh"
#include "sdc/SdcSetIODelay.hh"
#include "sdc/SdcSetInputTransition.hh"
#include "sdc/SdcSetLoad.hh"
// #include "ContestDriver.h"
// #include "Power.hh"
#include <boost/polygon/polygon.hpp>
#include <vector>

#include "congestion_api.h"

namespace python_interface {

std::string IdbOrientToString(IdbOrient orient)
{
  switch (orient) {
    case IdbOrient::kNone:
      return "None";
    case IdbOrient::kN_R0:
      return "N_R0";
    case IdbOrient::kW_R90:
      return "W_R90";
    case IdbOrient::kS_R180:
      return "S_R180";
    case IdbOrient::kE_R270:
      return "E_R270";
    case IdbOrient::kFN_MY:
      return "FN_MY";
    case IdbOrient::kFE_MY90:
      return "FE_MY90";
    case IdbOrient::kFS_MX:
      return "FS_MX";
    case IdbOrient::kFW_MX90:
      return "FW_MX90";
    case IdbOrient::kMax:
      return "Max";
    default:
      return "Unknown";
  }
}

void PyPlaceDB::init_routability(idm::DataManager* db, std::vector<IdbInstance*> inst_resort_list)
{
  // routebilty driven placement
  // routing information initialized
  routing_grid_xl = xl;
  routing_grid_yl = yl;
  routing_grid_xh = xh;
  routing_grid_yh = yh;
  // int pitch = db->get_idb_layout()->get_track_grid_list()->get_track_grid_list()[0]->get_track()->get_pitch();
  // double tarck_width = db->get_idb_layout()->get_track_grid_list()->get_track_grid_list()[0]->;
  // double tarck_width = db->get_idb_layout()->get_track_grid_list()->get_track_grid_list()[0]->get_track()->get_width();

  // congestion map opt

  routing_grids_size_x = std::ceil((routing_grid_xh - routing_grid_xl) / num_routing_grids_x);
  routing_grids_size_y = std::ceil((routing_grid_yh - routing_grid_yl) / num_routing_grids_y);
  // num_routing_grids_x = std::floor((routing_grid_xh - routing_grid_xl) / routing_grids_size_x);
  // num_routing_grids_y = std::floor((routing_grid_yh - routing_grid_yl) / routing_grids_size_y);
  routing_grid_xh = routing_grid_xl + num_routing_grids_x * routing_grids_size_x;
  routing_grid_yh = routing_grid_yl + num_routing_grids_y * routing_grids_size_y;

  int track_layer_id = db->get_idb_layout()->get_track_grid_list()->get_track_grid_list()[0]->get_layer_list()[0]->get_id();
  for (index_type layer_idx = 0; layer_idx < db->get_idb_layout()->get_layers()->get_routing_layers_number(); ++layer_idx) {
    auto idb_layer = db->get_idb_layout()->get_layers()->get_routing_layers().at(layer_idx);
    idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
    if (idb_routing_layer->get_track_grid_list().empty()) {
      continue;
    }
    double track_ratio = idb_routing_layer->get_track_grid_list().size();
    for (IdbTrackGrid* track_grid : idb_routing_layer->get_track_grid_list()) {
      auto idb_track_grid = track_grid->get_track();

      int track_start = static_cast<int32_t>(idb_track_grid->get_start());
      int track_pitch = static_cast<int32_t>(idb_track_grid->get_pitch());
      int track_num = track_grid->get_track_num();
      if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionX) {
        unit_vertical_capacities.append(1. * track_num / routing_grids_size_x);
        // track_axis.get_x_grid_list().push_back(track_grid);
      } else if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionY) {
        unit_horizontal_capacities.append(1. * track_num / routing_grids_size_y);
      }
    }
  }
  // this is slightly different from db.routingGridOrigin
  // to be consistent with global placement
  int all_layer_num = db->get_idb_layout()->get_layers()->get_routing_layers_number();
  double routing_grid_area = routing_grids_size_x * routing_grids_size_y;
  std::vector<int> initial_horizontal_routing_map(all_layer_num * num_routing_grids_x * num_routing_grids_y, 0);
  std::vector<int> initial_vertical_routing_map(initial_horizontal_routing_map.size(), 0);

  for (unsigned int i = 0; i < inst_resort_list.size(); ++i) {
    IdbInstance* node = inst_resort_list.at(i);
    // Macro const& macro = db.macro(db.macroId(node));
    // else if (macro.className() != "DREAMPlace.PlaceBlockage") // fixed cells are special cases, skip placement blockages (looks like
    // ISPD2015 benchmarks do not process placement blockages)

    if (node->get_status() == IdbPlacementStatus::kFixed) {
      // Macro const& macro = db.macro(db.macroId(node));
      // printf("PyPlaceDB detect fixed cell: ");
      for (auto obs : node->get_cell_master()->get_obs_list()) {
        Box box(node->get_coordinate()->get_x(), node->get_coordinate()->get_y(), node->get_bounding_box()->get_high_x(),
                node->get_bounding_box()->get_high_y());
        // Box box(node.xl + obs_box.xl, node.yl + obs_box.yl, node.xl + obs_box.xh, node.yl + obs_box.yh);
        index_type grid_index_xl = std::max(int((box.xl - routing_grid_xl) / routing_grids_size_x), 0);
        index_type grid_index_yl = std::max(int((box.yl - routing_grid_yl) / routing_grids_size_y), 0);
        index_type grid_index_xh = std::min(unsigned((box.xh - routing_grid_xl) / routing_grids_size_x) + 1, num_routing_grids_x);
        index_type grid_index_yh = std::min(unsigned((box.yh - routing_grid_yl) / routing_grids_size_y) + 1, num_routing_grids_y);
        for (index_type k = grid_index_xl; k < grid_index_xh; ++k) {
          coordinate_type grid_xl = routing_grid_xl + k * routing_grids_size_x;
          coordinate_type grid_xh = grid_xl + routing_grids_size_x;
          for (index_type h = grid_index_yl; h < grid_index_yh; ++h) {
            coordinate_type grid_yl = routing_grid_yl + h * routing_grids_size_y;
            coordinate_type grid_yh = grid_yl + routing_grids_size_y;
            Box grid_box(grid_xl, grid_yl, grid_xh, grid_yh);
            for (auto obs_layer : obs->get_obs_layer_list()) {
              if (obs_layer->get_shape()->get_layer() == nullptr) {
                std::cout << "continue because obs_layer->get_shape()->get_layer() is nullptr" << std::endl;
                continue;
              }
              int layer_idx = obs_layer->get_shape()->get_layer()->get_id();
              index_type index = layer_idx * num_routing_grids_x * num_routing_grids_y + (k * num_routing_grids_y + h);
              double intersect_ratio = intersectArea(box, grid_box) / routing_grid_area;
              // dreamplaceAssert(intersect_ratio <= 1);
              auto idb_layer = db->get_idb_layout()->get_layers()->get_routing_layers().at(layer_idx);
              idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
              if (idb_routing_layer->get_track_grid_list().empty()) {
                continue;
              }
              for (IdbTrackGrid* track_grid : idb_routing_layer->get_track_grid_list()) {
                auto idb_track_grid = track_grid->get_track();
                int track_num = track_grid->get_track_num();
                if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionX) {
                  initial_vertical_routing_map[index] += ceil(intersect_ratio * track_num);
                  // track_axis.get_x_grid_list().push_back(track_grid);
                } else if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionY) {
                  // int track_num = static_cast<int32_t>((routing_grid_xh - routing_grid_xl) / track_pitch);
                  initial_horizontal_routing_map[index] += ceil(intersect_ratio * track_num);
                  // track_axis.get_y_grid_list().push_back(track_grid);
                }
              }
            }
            // printf("Instance %s, Coordinate (%d, %d, %d, %d)\n", node->get_name().c_str(), node->get_coordinate()->get_x(),
            //        node->get_coordinate()->get_y(), node->get_bounding_box()->get_high_x(),
            //        node->get_bounding_box()->get_high_y());
          }
        }
      }
    }
  }

  for (auto blockage : db->get_idb_design()->get_blockage_list()->get_blockage_list()) {
    if (!blockage->is_routing_blockage()) {
      continue;
    }
    IdbRoutingBlockage* routing_blockage = dynamic_cast<IdbRoutingBlockage*>(blockage);
    // auto rect = routing_blockage->get_rect();

    index_type layer = routing_blockage->get_layer()->get_id();
    idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(routing_blockage->get_layer());
    for (auto rect : blockage->get_rect_list()) {
      // convert to absolute box
      Box box(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
      index_type grid_index_xl = std::max(int((box.xl - routing_grid_xl) / routing_grids_size_x), 0);
      index_type grid_index_yl = std::max(int((box.yl - routing_grid_yl) / routing_grids_size_y), 0);
      index_type grid_index_xh = std::min(unsigned((box.xh - routing_grid_xl) / routing_grids_size_x) + 1, num_routing_grids_x);
      index_type grid_index_yh = std::min(unsigned((box.yh - routing_grid_yl) / routing_grids_size_y) + 1, num_routing_grids_y);
      for (index_type k = grid_index_xl; k < grid_index_xh; ++k) {
        coordinate_type grid_xl = routing_grid_xl + k * routing_grids_size_x;
        coordinate_type grid_xh = grid_xl + routing_grids_size_x;
        for (index_type h = grid_index_yl; h < grid_index_yh; ++h) {
          coordinate_type grid_yl = routing_grid_yl + h * routing_grids_size_y;
          coordinate_type grid_yh = grid_yl + routing_grids_size_y;
          Box grid_box(grid_xl, grid_yl, grid_xh, grid_yh);
          index_type index = layer * num_routing_grids_x * num_routing_grids_y + (k * num_routing_grids_y + h);
          double intersect_ratio = intersectArea(box, grid_box) / routing_grid_area;
          // dreamplaceAssert(intersect_ratio <= 1);
          for (IdbTrackGrid* track_grid : idb_routing_layer->get_track_grid_list()) {
            auto idb_track_grid = track_grid->get_track();
            int track_num = track_grid->get_track_num();
            if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionX) {
              // track_axis.get_x_grid_list().push_back(track_grid);
              initial_vertical_routing_map[index] += ceil(intersect_ratio * track_num);
            } else if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionY) {
              // int track_num = static_cast<int32_t>((routing_grid_xh - routing_grid_xl) / track_pitch);
              initial_horizontal_routing_map[index] += ceil(intersect_ratio * track_num);
              // track_axis.get_y_grid_list().push_back(track_grid);
            }
          }
          // initial_horizontal_routing_map[index] += ceil(intersect_ratio * db.numRoutingTracks(PlanarDirectEnum::HORIZONTAL, layer));
          // initial_vertical_routing_map[index] += ceil(intersect_ratio * db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer));
          if (layer == 2) {
            // dreamplaceAssert(db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer) == 0);
            // dreamplaceAssertMsg(initial_vertical_routing_map[index] == 0,
            //                     "intersect_ratio %g, initial_vertical_routing_map[%u] = %d, capacity %u, product %g",
            //                     intersect_ratio, index, initial_vertical_routing_map[index],
            //                     db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer), intersect_ratio *
            //                     db.numRoutingTracks(PlanarDirectEnum::VERTICAL, layer));
          }
        }
      }
    }
  }
  // clamp maximum for overlapping fixed cells
  for (index_type layer = 0; layer < all_layer_num; ++layer) {
    for (int i = 0, ie = num_routing_grids_x * num_routing_grids_y; i < ie; ++i) {
      auto idb_layer = db->get_idb_layout()->get_layers()->get_routing_layers().at(layer);
      idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
      for (IdbTrackGrid* track_grid : idb_routing_layer->get_track_grid_list()) {
        auto idb_track_grid = track_grid->get_track();
        int track_num = track_grid->get_track_num();
        if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionX) {
          auto& vvalue = initial_vertical_routing_map[layer * ie + i];
          vvalue = std::min(vvalue, track_num);
        } else if (idb_track_grid->get_direction() == idb::IdbTrackDirection::kDirectionY) {
          auto& hvalue = initial_horizontal_routing_map[layer * ie + i];
          hvalue = std::min(hvalue, track_num);
        }
      }
    }
  }
  for (auto item : initial_horizontal_routing_map) {
    initial_horizontal_demand_map.append(item);
  }
  for (auto item : initial_vertical_routing_map) {
    initial_vertical_demand_map.append(item);
  }
}

std::vector<std::vector<float>> PyPlaceDB::getCongestionMap(string method, string stage, string resolve_congestion)
{
  auto result_tuple = CONGESTION_API_INST->getAllEGRMap(true, stage, resolve_congestion);
  int new_size_x = num_routing_grids_x;
  int new_size_y = num_routing_grids_y;
  std::vector<std::vector<float>> result_map(new_size_y, std::vector<float>(new_size_x, 0));
  std::vector<std::vector<int>> sum_supply_map(new_size_y, std::vector<int>(new_size_x, 0));
  std::vector<std::vector<int>> sum_demand_map(new_size_y, std::vector<int>(new_size_x, 0));

  // std::vector<std::tuple<int, int, Box>> fixed_boxes;  // grid_x, grid_y, rect
  auto overflow_supply_map = std::get<0>(result_tuple);
  auto gcell_info_list = std::get<1>(result_tuple);

  for (auto const& [key, overflow_supply_pair] : overflow_supply_map) {
    auto& overflow_matrix = overflow_supply_pair.first;
    auto& supply_matrix = overflow_supply_pair.second;

    int old_size_y = supply_matrix.size();     // 行数 (Y-dim)
    int old_size_x = supply_matrix[0].size();  // 列数 (X-dim)
    double sum_overflow = 0;
    double sum_supply = 0;
    int overflow_gcell_num = 0;
    for (int i = 0; i < old_size_y; i++) {
      for (int j = 0; j < old_size_x; j++) {
        sum_supply += supply_matrix[i][j];
        if (overflow_matrix[i][j] > 0) {
          sum_overflow += overflow_matrix[i][j];
          overflow_gcell_num ++;
        }
      }
    }
    printf("Num gcell is %d /%d \n", overflow_gcell_num, (int) gcell_info_list.size());
    printf("Layer %s, Overflow :%f / %f\n", key.c_str(), sum_overflow, sum_supply);
    // assert(num_routing_grids_x <= old_size_x);
    // assert(num_routing_grids_y <= old_size_y);
    std::vector<std::vector<float>> result_map_supply(new_size_y, std::vector<float>(new_size_x, 0));
    std::vector<std::vector<float>> result_map_demand(new_size_y, std::vector<float>(new_size_x, 0));
    // std::vector<std::vector<int>> new_val(new_size_x, std::vector<int>(new_size_y, 0));
    for (auto& gcell_info : gcell_info_list) {
      if (gcell_info.lx < routing_grid_xl || gcell_info.ly < routing_grid_yl || gcell_info.ux > routing_grid_xh
          || gcell_info.uy > routing_grid_yh) {
        continue;
      }
      int py_grid_x_start = std::max(int((gcell_info.lx - routing_grid_xl) / routing_grids_size_x), 0);
      int py_grid_y_start = std::max(int((gcell_info.ly - routing_grid_yl) / routing_grids_size_y), 0);
      int py_grid_x_end = std::min(unsigned((gcell_info.ux - routing_grid_xl) / routing_grids_size_x) + 1, num_routing_grids_x);
      int py_grid_y_end = std::min(unsigned((gcell_info.uy - routing_grid_yl) / routing_grids_size_y) + 1, num_routing_grids_y);
      int old_x = gcell_info.grid_x;
      int old_y = gcell_info.grid_y;
      Box gcell_box(gcell_info.lx, gcell_info.ly, gcell_info.ux, gcell_info.uy);
      for (int py_y = py_grid_y_start; py_y < py_grid_y_end; py_y++) {
        for (int py_x = py_grid_x_start; py_x < py_grid_x_end; py_x++) {
          double intersect_ratio
              = intersectArea(gcell_box,
                              Box(routing_grid_xl + py_x * routing_grids_size_x, routing_grid_yl + py_y * routing_grids_size_y,
                                  routing_grid_xl + (py_x + 1) * routing_grids_size_x, routing_grid_yl + (py_y + 1) * routing_grids_size_y))
                / gcell_box.area();
          result_map_supply[py_y][py_x] += intersect_ratio * supply_matrix[old_y][old_x];
          result_map_demand[py_y][py_x]
              = std::max(result_map_demand[py_y][py_x], 1.f * (overflow_matrix[old_y][old_x] + supply_matrix[old_y][old_x]));
          // supply_matrix[py_x][py_y] = 0;
          // overflow_matrix[py_x][py_y] = 0;
        }
      }
    }

    for (int i = 0; i < new_size_y; i++) {
      for (int j = 0; j < new_size_x; j++) {
        float supply_val = result_map_supply[i][j];
        float demand_val = result_map_demand[i][j];
        assert(supply_val >= 0);
        if (method == "sum") {
          sum_supply_map[i][j] += supply_val;
          sum_demand_map[i][j] += demand_val;
        } else if (method == "max") {
          if (supply_val == 0) {
            float tmp_val = std::min(1 + 1. * demand_val / 2, 5.0);
            result_map[i][j] = std::max(result_map[i][j], 1.f * tmp_val);
          } else {
            result_map[i][j] = std::max(result_map[i][j], 1.f * demand_val / supply_val);
          }
          // if (result_map[i][j] > 5.0) {
          //   result_map[i][j] = 5.0;
          //   // printf("Warning: too large for congestion map\n");
          // }
        } else {
          std::cerr << "Error: unsupported method " << method << ", use sum instead." << std::endl;
          sum_supply_map[i][j] += supply_val;
          sum_demand_map[i][j] += demand_val;
        }
      }
    }
  }

  for (int i = 0; i < new_size_y; i++) {
    for (int j = 0; j < new_size_x; j++) {
      if (method == "max") {
        result_map[i][j] = std::max(result_map[i][j], 0.0f);  // cap max congestion to 5
        continue;
      }
      int supply = sum_supply_map[i][j];
      int demand = sum_demand_map[i][j];
      double new_val = 0;
      if (supply > 0) {
        new_val = std::max(static_cast<double>(demand) / supply, 0.0);
      } else if (demand > 0) {
        new_val = 5;  // inf overflow
      } else {
        new_val = 0;
      }
      result_map[i][j] = new_val;
    }
  }

  return result_map;
}
}  // namespace python_interface
