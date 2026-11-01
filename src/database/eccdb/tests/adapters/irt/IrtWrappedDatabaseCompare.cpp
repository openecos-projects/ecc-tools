// SPDX-License-Identifier: MulanPSL-2.0

#include "IrtWrappedDatabaseCompare.h"

#include <algorithm>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "CutLayer.hpp"
#include "Database.hpp"
#include "LayerRect.hpp"
#include "Macro.hpp"
#include "Net.hpp"
#include "Pin.hpp"
#include "RoutingLayer.hpp"
#include "ScaleAxis.hpp"
#include "SpacingTable.hpp"
#include "ViaMaster.hpp"

namespace eccdb::test {
namespace {

using namespace irt;

std::string layerName(std::vector<RoutingLayer>& layers, int32_t idx)
{
  if (idx < 0 || idx >= static_cast<int32_t>(layers.size())) {
    return "<invalid_routing:" + std::to_string(idx) + ">";
  }
  return layers[static_cast<size_t>(idx)].get_layer_name();
}

std::string cutLayerName(std::vector<CutLayer>& layers, int32_t idx)
{
  if (idx < 0 || idx >= static_cast<int32_t>(layers.size())) {
    return "<invalid_cut:" + std::to_string(idx) + ">";
  }
  return layers[static_cast<size_t>(idx)].get_layer_name();
}

bool equalGridMap(GridMap<int32_t>& left, GridMap<int32_t>& right)
{
  if (left.get_x_size() != right.get_x_size() || left.get_y_size() != right.get_y_size()) {
    return false;
  }
  for (int32_t x = 0; x < left.get_x_size(); ++x) {
    for (int32_t y = 0; y < left.get_y_size(); ++y) {
      if (left[x][y] != right[x][y]) {
        return false;
      }
    }
  }
  return true;
}

bool equalTrackAxis(ScaleAxis& left, ScaleAxis& right)
{
  auto equal_grids = [](const std::vector<ScaleGrid>& lhs, const std::vector<ScaleGrid>& rhs) {
    if (lhs.size() != rhs.size()) {
      return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
      if (lhs[i].get_start_line() != rhs[i].get_start_line() || lhs[i].get_step_length() != rhs[i].get_step_length()) {
        return false;
      }
    }
    return true;
  };
  return equal_grids(left.get_x_grid_list(), right.get_x_grid_list()) && equal_grids(left.get_y_grid_list(), right.get_y_grid_list());
}

bool equalSpacingTable(SpacingTable& left, SpacingTable& right)
{
  return left.get_width_list() == right.get_width_list() && left.get_parallel_length_list() == right.get_parallel_length_list()
         && equalGridMap(left.get_width_parallel_length_map(), right.get_width_parallel_length_map());
}

int32_t mappedLayerIdx(const std::map<int32_t, int32_t>& id_to_idx, int32_t layer_idx)
{
  const auto it = id_to_idx.find(layer_idx);
  return it == id_to_idx.end() ? -1 : it->second;
}

template <typename Rect>
void sortRectsByMappedLayer(std::vector<Rect>& rects, const std::map<int32_t, int32_t>& id_to_idx)
{
  std::sort(rects.begin(), rects.end(), [&](Rect& lhs, Rect& rhs) {
    return std::make_tuple(mappedLayerIdx(id_to_idx, lhs.get_layer_idx()), lhs.get_real_ll_x(), lhs.get_real_ll_y(), lhs.get_real_ur_x(),
                           lhs.get_real_ur_y())
           < std::make_tuple(mappedLayerIdx(id_to_idx, rhs.get_layer_idx()), rhs.get_real_ll_x(), rhs.get_real_ll_y(), rhs.get_real_ur_x(),
                             rhs.get_real_ur_y());
  });
}

template <typename Rect>
std::string firstRectMismatch(const std::string& tag, std::vector<Rect>& left, std::vector<Rect>& right,
                              const std::map<int32_t, int32_t>& left_id_to_idx, const std::map<int32_t, int32_t>& right_id_to_idx,
                              const std::function<std::string(int32_t)>& left_name, const std::function<std::string(int32_t)>& right_name)
{
  sortRectsByMappedLayer(left, left_id_to_idx);
  sortRectsByMappedLayer(right, right_id_to_idx);
  if (left.size() != right.size()) {
    std::ostringstream out;
    out << tag << " " << left.size() << " vs " << right.size();
    const size_t n = std::min(left.size(), right.size());
    size_t first = 0;
    for (; first < n; ++first) {
      const int32_t left_idx = mappedLayerIdx(left_id_to_idx, left[first].get_layer_idx());
      const int32_t right_idx = mappedLayerIdx(right_id_to_idx, right[first].get_layer_idx());
      if (left_idx != right_idx || left[first].get_real_ll_x() != right[first].get_real_ll_x()
          || left[first].get_real_ll_y() != right[first].get_real_ll_y() || left[first].get_real_ur_x() != right[first].get_real_ur_x()
          || left[first].get_real_ur_y() != right[first].get_real_ur_y()) {
        break;
      }
    }
    out << " first_diff=" << first;
    if (first < left.size()) {
      out << " left[" << first << "]=" << left_name(left[first].get_layer_idx()) << ' ' << left[first].get_real_ll_x() << ' '
          << left[first].get_real_ll_y() << ' ' << left[first].get_real_ur_x() << ' ' << left[first].get_real_ur_y();
    }
    if (first < right.size()) {
      out << " right[" << first << "]=" << right_name(right[first].get_layer_idx()) << ' ' << right[first].get_real_ll_x() << ' '
          << right[first].get_real_ll_y() << ' ' << right[first].get_real_ur_x() << ' ' << right[first].get_real_ur_y();
    }
    return out.str();
  }
  for (size_t i = 0; i < left.size(); ++i) {
    const int32_t left_idx = mappedLayerIdx(left_id_to_idx, left[i].get_layer_idx());
    const int32_t right_idx = mappedLayerIdx(right_id_to_idx, right[i].get_layer_idx());
    if (left_idx != right_idx || left[i].get_real_ll_x() != right[i].get_real_ll_x() || left[i].get_real_ll_y() != right[i].get_real_ll_y()
        || left[i].get_real_ur_x() != right[i].get_real_ur_x() || left[i].get_real_ur_y() != right[i].get_real_ur_y()) {
      auto left_key = [&](size_t index) {
        return std::make_tuple(mappedLayerIdx(left_id_to_idx, left[index].get_layer_idx()), left[index].get_real_ll_x(),
                               left[index].get_real_ll_y(), left[index].get_real_ur_x(), left[index].get_real_ur_y());
      };
      auto right_key = [&](size_t index) {
        return std::make_tuple(mappedLayerIdx(right_id_to_idx, right[index].get_layer_idx()), right[index].get_real_ll_x(),
                               right[index].get_real_ll_y(), right[index].get_real_ur_x(), right[index].get_real_ur_y());
      };
      size_t left_cursor = i;
      size_t right_cursor = i;
      size_t left_only = left.size();
      size_t right_only = right.size();
      while (left_cursor < left.size() && right_cursor < right.size()
             && (left_only == left.size() || right_only == right.size())) {
        const auto lhs = left_key(left_cursor);
        const auto rhs = right_key(right_cursor);
        if (lhs == rhs) {
          ++left_cursor;
          ++right_cursor;
        } else if (lhs < rhs) {
          if (left_only == left.size()) {
            left_only = left_cursor;
          }
          ++left_cursor;
        } else {
          if (right_only == right.size()) {
            right_only = right_cursor;
          }
          ++right_cursor;
        }
      }
      if (left_only == left.size() && left_cursor < left.size()) {
        left_only = left_cursor;
      }
      if (right_only == right.size() && right_cursor < right.size()) {
        right_only = right_cursor;
      }
      std::ostringstream out;
      out << tag << "[" << i << "] " << left_name(left[i].get_layer_idx()) << ' ' << left[i].get_real_ll_x() << ' '
          << left[i].get_real_ll_y() << ' ' << left[i].get_real_ur_x() << ' ' << left[i].get_real_ur_y() << " vs "
          << right_name(right[i].get_layer_idx()) << ' ' << right[i].get_real_ll_x() << ' ' << right[i].get_real_ll_y() << ' '
          << right[i].get_real_ur_x() << ' ' << right[i].get_real_ur_y();
      if (left_only < left.size()) {
        out << " left_only[" << left_only << "]=" << left_name(left[left_only].get_layer_idx()) << ' '
            << left[left_only].get_real_ll_x() << ' ' << left[left_only].get_real_ll_y() << ' ' << left[left_only].get_real_ur_x() << ' '
            << left[left_only].get_real_ur_y();
      }
      if (right_only < right.size()) {
        out << " right_only[" << right_only << "]=" << right_name(right[right_only].get_layer_idx()) << ' '
            << right[right_only].get_real_ll_x() << ' ' << right[right_only].get_real_ll_y() << ' ' << right[right_only].get_real_ur_x()
            << ' ' << right[right_only].get_real_ur_y();
      }
      return out.str();
    }
  }
  return {};
}

}  // namespace

std::string compareIrtWrappedDatabase(irt::Database& left, irt::Database& right)
{
  auto fail = [](const std::string& message) { return message; };

  if (left.get_design_name() != right.get_design_name()) {
    return fail("design " + left.get_design_name() + " vs " + right.get_design_name());
  }
  if (left.get_micron_dbu() != right.get_micron_dbu()) {
    return fail("micron_dbu " + std::to_string(left.get_micron_dbu()) + " vs " + std::to_string(right.get_micron_dbu()));
  }
  if (left.get_manufacture_grid() != right.get_manufacture_grid()) {
    return fail("manufacture_grid " + std::to_string(left.get_manufacture_grid()) + " vs " + std::to_string(right.get_manufacture_grid()));
  }

  irt::Die& left_die = left.get_die();
  irt::Die& right_die = right.get_die();
  if (left_die.get_real_ll_x() != right_die.get_real_ll_x() || left_die.get_real_ll_y() != right_die.get_real_ll_y()
      || left_die.get_real_ur_x() != right_die.get_real_ur_x() || left_die.get_real_ur_y() != right_die.get_real_ur_y()) {
    std::ostringstream out;
    out << "die " << left_die.get_real_ll_x() << ' ' << left_die.get_real_ll_y() << ' ' << left_die.get_real_ur_x() << ' '
        << left_die.get_real_ur_y() << " vs " << right_die.get_real_ll_x() << ' ' << right_die.get_real_ll_y() << ' '
        << right_die.get_real_ur_x() << ' ' << right_die.get_real_ur_y();
    return fail(out.str());
  }

  auto& left_macros = left.get_macro_list();
  auto& right_macros = right.get_macro_list();
  std::sort(left_macros.begin(), left_macros.end(), [](irt::Macro& lhs, irt::Macro& rhs) { return lhs.get_inst_name() < rhs.get_inst_name(); });
  std::sort(right_macros.begin(), right_macros.end(), [](irt::Macro& lhs, irt::Macro& rhs) { return lhs.get_inst_name() < rhs.get_inst_name(); });
  if (left_macros.size() != right_macros.size()) {
    return fail("macro_count " + std::to_string(left_macros.size()) + " vs " + std::to_string(right_macros.size()));
  }
  for (size_t i = 0; i < left_macros.size(); ++i) {
    irt::Macro& lhs = left_macros[i];
    irt::Macro& rhs = right_macros[i];
    if (lhs.get_inst_name() != rhs.get_inst_name() || lhs.get_body_rect() != rhs.get_body_rect()) {
      return fail("macro[" + std::to_string(i) + "] " + lhs.get_inst_name() + " fields differ");
    }
  }

  irt::Row& left_row = left.get_row();
  irt::Row& right_row = right.get_row();
  if (left_row.get_start_x() != right_row.get_start_x() || left_row.get_start_y() != right_row.get_start_y()
      || left_row.get_height() != right_row.get_height()) {
    std::ostringstream out;
    out << "row " << left_row.get_start_x() << ' ' << left_row.get_start_y() << ' ' << left_row.get_height() << " vs "
        << right_row.get_start_x() << ' ' << right_row.get_start_y() << ' ' << right_row.get_height();
    return fail(out.str());
  }
  if (!equalTrackAxis(left.get_gcell_axis(), right.get_gcell_axis())) {
    return fail("gcell axis differs");
  }

  std::vector<irt::RoutingLayer>& left_routing = left.get_routing_layer_list();
  std::vector<irt::RoutingLayer>& right_routing = right.get_routing_layer_list();
  if (left_routing.size() != right_routing.size()) {
    return fail("routing_layer_count " + std::to_string(left_routing.size()) + " vs " + std::to_string(right_routing.size()));
  }
  for (size_t i = 0; i < left_routing.size(); ++i) {
    irt::RoutingLayer& lhs = left_routing[i];
    irt::RoutingLayer& rhs = right_routing[i];
    if (lhs.get_layer_name() != rhs.get_layer_name()) {
      return fail("routing_layer[" + std::to_string(i) + "] " + lhs.get_layer_name() + " vs " + rhs.get_layer_name());
    }
    if (lhs.get_prefer_direction() != rhs.get_prefer_direction() || lhs.get_min_width() != rhs.get_min_width()
        || lhs.get_min_area() != rhs.get_min_area() || lhs.get_notch_spacing() != rhs.get_notch_spacing()
        || lhs.get_eol_spacing() != rhs.get_eol_spacing() || lhs.get_eol_ete() != rhs.get_eol_ete()
        || lhs.get_eol_within() != rhs.get_eol_within()) {
      return fail("routing_layer " + lhs.get_layer_name() + " fields differ");
    }
    if (!equalTrackAxis(lhs.get_track_axis(), rhs.get_track_axis())) {
      return fail("routing_layer " + lhs.get_layer_name() + " track axis differs");
    }
    if (!equalSpacingTable(lhs.get_prl_spacing_table(), rhs.get_prl_spacing_table())) {
      return fail("routing_layer " + lhs.get_layer_name() + " prl table differs");
    }
  }

  std::vector<irt::CutLayer>& left_cuts = left.get_cut_layer_list();
  std::vector<irt::CutLayer>& right_cuts = right.get_cut_layer_list();
  if (left_cuts.size() != right_cuts.size()) {
    return fail("cut_layer_count " + std::to_string(left_cuts.size()) + " vs " + std::to_string(right_cuts.size()));
  }
  for (size_t i = 0; i < left_cuts.size(); ++i) {
    irt::CutLayer& lhs = left_cuts[i];
    irt::CutLayer& rhs = right_cuts[i];
    if (lhs.get_layer_name() != rhs.get_layer_name()) {
      return fail("cut_layer[" + std::to_string(i) + "] " + lhs.get_layer_name() + " vs " + rhs.get_layer_name());
    }
    if (lhs.get_curr_spacing() != rhs.get_curr_spacing() || lhs.get_curr_prl() != rhs.get_curr_prl()
        || lhs.get_curr_prl_spacing() != rhs.get_curr_prl_spacing() || lhs.get_curr_eol_spacing() != rhs.get_curr_eol_spacing()
        || lhs.get_curr_eol_prl() != rhs.get_curr_eol_prl() || lhs.get_curr_eol_prl_spacing() != rhs.get_curr_eol_prl_spacing()
        || lhs.get_above_spacing() != rhs.get_above_spacing() || lhs.get_above_prl() != rhs.get_above_prl()
        || lhs.get_above_prl_spacing() != rhs.get_above_prl_spacing() || lhs.get_below_spacing() != rhs.get_below_spacing()
        || lhs.get_below_prl() != rhs.get_below_prl() || lhs.get_below_prl_spacing() != rhs.get_below_prl_spacing()) {
      return fail("cut_layer " + lhs.get_layer_name() + " fields differ");
    }
  }

  std::vector<irt::ViaMaster> left_vias;
  std::vector<irt::ViaMaster> right_vias;
  for (std::vector<irt::ViaMaster>& layer_vias : left.get_layer_via_master_list()) {
    left_vias.insert(left_vias.end(), layer_vias.begin(), layer_vias.end());
  }
  for (std::vector<irt::ViaMaster>& layer_vias : right.get_layer_via_master_list()) {
    right_vias.insert(right_vias.end(), layer_vias.begin(), layer_vias.end());
  }
  std::sort(left_vias.begin(), left_vias.end(), [](irt::ViaMaster& a, irt::ViaMaster& b) { return a.get_via_name() < b.get_via_name(); });
  std::sort(right_vias.begin(), right_vias.end(), [](irt::ViaMaster& a, irt::ViaMaster& b) { return a.get_via_name() < b.get_via_name(); });
  if (left_vias.size() != right_vias.size()) {
    return fail("via_master_count " + std::to_string(left_vias.size()) + " vs " + std::to_string(right_vias.size()));
  }
  for (size_t i = 0; i < left_vias.size(); ++i) {
    irt::ViaMaster& lhs = left_vias[i];
    irt::ViaMaster& rhs = right_vias[i];
    if (lhs.get_via_name() != rhs.get_via_name()) {
      return fail("via[" + std::to_string(i) + "] " + lhs.get_via_name() + " vs " + rhs.get_via_name());
    }
    irt::LayerRect& left_above = lhs.get_above_enclosure();
    irt::LayerRect& right_above = rhs.get_above_enclosure();
    irt::LayerRect& left_below = lhs.get_below_enclosure();
    irt::LayerRect& right_below = rhs.get_below_enclosure();
    if (layerName(left_routing, left_above.get_layer_idx()) != layerName(right_routing, right_above.get_layer_idx())
        || left_above.get_ll_x() != right_above.get_ll_x() || left_above.get_ll_y() != right_above.get_ll_y()
        || left_above.get_ur_x() != right_above.get_ur_x() || left_above.get_ur_y() != right_above.get_ur_y()
        || layerName(left_routing, left_below.get_layer_idx()) != layerName(right_routing, right_below.get_layer_idx())
        || left_below.get_ll_x() != right_below.get_ll_x() || left_below.get_ll_y() != right_below.get_ll_y()
        || left_below.get_ur_x() != right_below.get_ur_x() || left_below.get_ur_y() != right_below.get_ur_y()
        || cutLayerName(left_cuts, lhs.get_cut_layer_idx()) != cutLayerName(right_cuts, rhs.get_cut_layer_idx())
        || lhs.get_above_direction() != rhs.get_above_direction() || lhs.get_below_direction() != rhs.get_below_direction()
        || lhs.get_cut_shape_list() != rhs.get_cut_shape_list()) {
      return fail("via " + lhs.get_via_name() + " fields differ");
    }
  }

  if (const auto mismatch = firstRectMismatch(
          "routing_obstacles", left.get_routing_obstacle_list(), right.get_routing_obstacle_list(),
          left.get_routing_idb_layer_id_to_idx_map(), right.get_routing_idb_layer_id_to_idx_map(),
          [&](int32_t idx) { return layerName(left_routing, mappedLayerIdx(left.get_routing_idb_layer_id_to_idx_map(), idx)); },
          [&](int32_t idx) { return layerName(right_routing, mappedLayerIdx(right.get_routing_idb_layer_id_to_idx_map(), idx)); });
      !mismatch.empty()) {
    return fail(mismatch);
  }
  if (const auto mismatch = firstRectMismatch(
          "cut_obstacles", left.get_cut_obstacle_list(), right.get_cut_obstacle_list(), left.get_cut_idb_layer_id_to_idx_map(),
          right.get_cut_idb_layer_id_to_idx_map(),
          [&](int32_t idx) { return cutLayerName(left_cuts, mappedLayerIdx(left.get_cut_idb_layer_id_to_idx_map(), idx)); },
          [&](int32_t idx) { return cutLayerName(right_cuts, mappedLayerIdx(right.get_cut_idb_layer_id_to_idx_map(), idx)); });
      !mismatch.empty()) {
    return fail(mismatch);
  }

  std::vector<irt::Net>& left_nets = left.get_net_list();
  std::vector<irt::Net>& right_nets = right.get_net_list();
  std::sort(left_nets.begin(), left_nets.end(), [](irt::Net& a, irt::Net& b) { return a.get_net_name() < b.get_net_name(); });
  std::sort(right_nets.begin(), right_nets.end(), [](irt::Net& a, irt::Net& b) { return a.get_net_name() < b.get_net_name(); });
  if (left_nets.size() != right_nets.size()) {
    return fail("net_count " + std::to_string(left_nets.size()) + " vs " + std::to_string(right_nets.size()));
  }
  for (size_t i = 0; i < left_nets.size(); ++i) {
    irt::Net& lhs = left_nets[i];
    irt::Net& rhs = right_nets[i];
    if (lhs.get_net_name() != rhs.get_net_name()) {
      return fail("net[" + std::to_string(i) + "] " + lhs.get_net_name() + " vs " + rhs.get_net_name());
    }
    if (lhs.get_connect_type() != rhs.get_connect_type()) {
      return fail("net " + lhs.get_net_name() + " connect_type differs");
    }
    std::vector<irt::Pin>& left_pins = lhs.get_pin_list();
    std::vector<irt::Pin>& right_pins = rhs.get_pin_list();
    std::sort(left_pins.begin(), left_pins.end(), [](irt::Pin& a, irt::Pin& b) { return a.get_pin_name() < b.get_pin_name(); });
    std::sort(right_pins.begin(), right_pins.end(), [](irt::Pin& a, irt::Pin& b) { return a.get_pin_name() < b.get_pin_name(); });
    if (left_pins.size() != right_pins.size()) {
      return fail("net " + lhs.get_net_name() + " pin_count " + std::to_string(left_pins.size()) + " vs "
                  + std::to_string(right_pins.size()));
    }
    for (size_t pin_idx = 0; pin_idx < left_pins.size(); ++pin_idx) {
      irt::Pin& left_pin = left_pins[pin_idx];
      irt::Pin& right_pin = right_pins[pin_idx];
      if (left_pin.get_pin_name() != right_pin.get_pin_name() || left_pin.get_is_core() != right_pin.get_is_core()
          || left_pin.get_is_driven() != right_pin.get_is_driven() || left_pin.get_inst_name() != right_pin.get_inst_name()
          || left_pin.get_cell_master_name() != right_pin.get_cell_master_name() || left_pin.get_orient() != right_pin.get_orient()
          || left_pin.get_inst_origin() != right_pin.get_inst_origin()
          || left_pin.get_local_pin_name() != right_pin.get_local_pin_name() || left_pin.get_is_macro() != right_pin.get_is_macro()
          || left_pin.get_is_pad() != right_pin.get_is_pad() || left_pin.get_inst_bbox() != right_pin.get_inst_bbox()
          || left_pin.get_macro_pin_edge() != right_pin.get_macro_pin_edge()
          || left_pin.get_preferred_escape_direction() != right_pin.get_preferred_escape_direction()
          || layerName(left_routing, left_pin.get_preferred_conn_layer_idx())
                 != layerName(right_routing, right_pin.get_preferred_conn_layer_idx())) {
        return fail("net " + lhs.get_net_name() + " pin " + left_pin.get_pin_name() + " vs " + right_pin.get_pin_name());
      }
      if (const auto mismatch = firstRectMismatch(
              "net " + lhs.get_net_name() + " pin " + left_pin.get_pin_name() + " routing_shapes", left_pin.get_routing_shape_list(),
              right_pin.get_routing_shape_list(), left.get_routing_idb_layer_id_to_idx_map(), right.get_routing_idb_layer_id_to_idx_map(),
              [&](int32_t idx) { return layerName(left_routing, mappedLayerIdx(left.get_routing_idb_layer_id_to_idx_map(), idx)); },
              [&](int32_t idx) { return layerName(right_routing, mappedLayerIdx(right.get_routing_idb_layer_id_to_idx_map(), idx)); });
          !mismatch.empty()) {
        return fail(mismatch);
      }
      if (const auto mismatch = firstRectMismatch(
              "net " + lhs.get_net_name() + " pin " + left_pin.get_pin_name() + " cut_shapes", left_pin.get_cut_shape_list(),
              right_pin.get_cut_shape_list(), left.get_cut_idb_layer_id_to_idx_map(), right.get_cut_idb_layer_id_to_idx_map(),
              [&](int32_t idx) { return cutLayerName(left_cuts, mappedLayerIdx(left.get_cut_idb_layer_id_to_idx_map(), idx)); },
              [&](int32_t idx) { return cutLayerName(right_cuts, mappedLayerIdx(right.get_cut_idb_layer_id_to_idx_map(), idx)); });
          !mismatch.empty()) {
        return fail(mismatch);
      }
    }
  }
  return {};
}

}  // namespace eccdb::test
