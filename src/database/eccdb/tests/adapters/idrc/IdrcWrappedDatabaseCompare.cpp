// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#include "IdrcWrappedDatabaseCompare.h"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../../../../../operation/iDRC/source/data_manager/advance/CutLayer.hpp"
#include "../../../../../operation/iDRC/source/data_manager/advance/DRCShape.hpp"
#include "../../../../../operation/iDRC/source/data_manager/advance/Database.hpp"
#include "../../../../../operation/iDRC/source/data_manager/advance/MacroObs.hpp"
#include "../../../../../operation/iDRC/source/data_manager/advance/RoutingLayer.hpp"
#include "../../../../../operation/iDRC/source/data_manager/basic/GridMap.hpp"
#include "../../../../../operation/iDRC/source/data_manager/design_rule/AdjacentCutSpacingRule.hpp"
#include "../../../../../operation/iDRC/source/data_manager/design_rule/EndOfLineSpacingRule.hpp"
#include "../../../../../operation/iDRC/interface/DRCInterface.hpp"

namespace eccdb::test {
namespace {

std::string routingLayerName(std::vector<idrc::RoutingLayer>& layers, int32_t index)
{
  if (index < 0 || index >= static_cast<int32_t>(layers.size())) {
    return "<invalid_routing:" + std::to_string(index) + ">";
  }
  return layers[static_cast<size_t>(index)].get_layer_name();
}

std::string cutLayerName(std::vector<idrc::CutLayer>& layers, int32_t index)
{
  if (index < 0 || index >= static_cast<int32_t>(layers.size())) {
    return "<invalid_cut:" + std::to_string(index) + ">";
  }
  return layers[static_cast<size_t>(index)].get_layer_name();
}

bool equalGridMap(idrc::GridMap<int32_t>& left, idrc::GridMap<int32_t>& right)
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

bool equalEol(const idrc::EndOfLineSpacingRule& left, const idrc::EndOfLineSpacingRule& right)
{
  return left.eol_spacing == right.eol_spacing && left.eol_width == right.eol_width && left.eol_within == right.eol_within
         && left.has_ete == right.has_ete && left.ete_spacing == right.ete_spacing && left.has_par == right.has_par
         && left.has_subtrace_eol_width == right.has_subtrace_eol_width && left.par_spacing == right.par_spacing
         && left.par_within == right.par_within && left.has_two_edges == right.has_two_edges && left.has_min_length == right.has_min_length
         && left.min_length == right.min_length && left.has_same_metal == right.has_same_metal
         && left.has_enclose_cut == right.has_enclose_cut && left.has_below == right.has_below && left.has_above == right.has_above
         && left.enclosed_dist == right.enclosed_dist && left.cut_to_metal_spacing == right.cut_to_metal_spacing
         && left.has_all_cuts == right.has_all_cuts;
}

std::string rectKey(const idrc::PlanarRect& rect)
{
  std::ostringstream out;
  out << rect.get_ll_x() << ',' << rect.get_ll_y() << ',' << rect.get_ur_x() << ',' << rect.get_ur_y();
  return out.str();
}

std::string rectMultisetKey(const std::vector<idrc::PlanarRect>& rectangles)
{
  std::vector<std::string> keys;
  keys.reserve(rectangles.size());
  for (const auto& rectangle : rectangles) {
    keys.push_back(rectKey(rectangle));
  }
  std::sort(keys.begin(), keys.end());

  std::ostringstream out;
  for (const auto& key : keys) {
    out << '[' << key << ']';
  }
  return out.str();
}

std::string macroObsLayerKey(idrc::Database& database, const idrc::MacroObsLayer& layer)
{
  const auto layer_name = layer.is_routing ? routingLayerName(database.get_routing_layer_list(), layer.layer_idx)
                                           : cutLayerName(database.get_cut_layer_list(), layer.layer_idx);
  std::ostringstream out;
  out << (layer.is_routing ? 'R' : 'C') << ':' << layer_name << " spacing=" << layer.min_spacing
      << " bbox=" << rectKey(layer.bounding_box) << " pins=" << rectMultisetKey(layer.pin_rect_list)
      << " obs=" << rectMultisetKey(layer.obs_rect_list);
  return out.str();
}

std::vector<std::string> macroObsGroupKeys(idrc::Database& database)
{
  std::vector<std::string> groups;
  const auto& macro_obs_list = database.get_macro_obs_list();
  groups.reserve(macro_obs_list.size());
  for (const auto& macro_obs : macro_obs_list) {
    std::vector<std::string> layer_keys;
    layer_keys.reserve(macro_obs.obs_layer_list.size());
    for (const auto& layer : macro_obs.obs_layer_list) {
      layer_keys.push_back(macroObsLayerKey(database, layer));
    }
    std::sort(layer_keys.begin(), layer_keys.end());
    std::ostringstream group;
    for (const auto& layer_key : layer_keys) {
      group << '{' << layer_key << '}';
    }
    groups.push_back(group.str());
  }
  std::sort(groups.begin(), groups.end());
  return groups;
}

std::string describeFirstMismatch(const std::vector<std::string>& left, const std::vector<std::string>& right)
{
  const auto common = std::min(left.size(), right.size());
  for (size_t index = 0; index < common; ++index) {
    if (left[index] != right[index]) {
      return "macro_obs[" + std::to_string(index) + "] " + left[index] + " vs " + right[index];
    }
  }
  return "macro_obs_count " + std::to_string(left.size()) + " vs " + std::to_string(right.size());
}

}  // namespace

std::vector<ids::Shape> toIdsShapes(const std::vector<idrc::DRCShape>& shapes)
{
  std::vector<ids::Shape> result;
  result.reserve(shapes.size());
  for (const auto& shape : shapes) {
    result.push_back(ids::Shape{.net_idx = shape.get_net_idx(),
                                .ll_x = shape.get_ll_x(),
                                .ll_y = shape.get_ll_y(),
                                .ur_x = shape.get_ur_x(),
                                .ur_y = shape.get_ur_y(),
                                .layer_idx = shape.get_layer_idx(),
                                .is_routing = shape.get_is_routing(),
                                .source_type = shape.get_source_type()});
  }
  return result;
}

std::vector<ids::Shape> legacyIdrcEnvironmentShapes(idrc::DRCInterface& interface)
{
  return toIdsShapes(interface.buildEnvShapeList());
}

std::vector<ids::Shape> legacyIdrcResultShapes(idrc::DRCInterface& interface)
{
  return toIdsShapes(interface.buildResultShapeList());
}

std::string compareIdrcWrappedDatabase(idrc::Database& left, idrc::Database& right)
{
  const auto fail = [](const std::string& message) { return message; };

  if (left.get_design_name() != right.get_design_name()) {
    return fail("design " + left.get_design_name() + " vs " + right.get_design_name());
  }
  if (left.get_micron_dbu() != right.get_micron_dbu()) {
    return fail("micron_dbu " + std::to_string(left.get_micron_dbu()) + " vs " + std::to_string(right.get_micron_dbu()));
  }
  if (left.get_manufacture_grid() != right.get_manufacture_grid()) {
    return fail("manufacture_grid " + std::to_string(left.get_manufacture_grid()) + " vs "
                + std::to_string(right.get_manufacture_grid()));
  }

  idrc::Die& left_die = left.get_die();
  idrc::Die& right_die = right.get_die();
  if (left_die.get_ll_x() != right_die.get_ll_x() || left_die.get_ll_y() != right_die.get_ll_y()
      || left_die.get_ur_x() != right_die.get_ur_x() || left_die.get_ur_y() != right_die.get_ur_y()) {
    std::ostringstream out;
    out << "die " << left_die.get_ll_x() << ' ' << left_die.get_ll_y() << ' ' << left_die.get_ur_x() << ' '
        << left_die.get_ur_y() << " vs " << right_die.get_ll_x() << ' ' << right_die.get_ll_y() << ' '
        << right_die.get_ur_x() << ' ' << right_die.get_ur_y();
    return fail(out.str());
  }

  if (left.get_off_grid_or_wrong_way_rule().manufacture_grid != right.get_off_grid_or_wrong_way_rule().manufacture_grid) {
    return fail("off_grid " + std::to_string(left.get_off_grid_or_wrong_way_rule().manufacture_grid) + " vs "
                + std::to_string(right.get_off_grid_or_wrong_way_rule().manufacture_grid));
  }

  auto& left_routing = left.get_routing_layer_list();
  auto& right_routing = right.get_routing_layer_list();
  auto& left_stack = left.get_max_via_stack_rule();
  auto& right_stack = right.get_max_via_stack_rule();
  if (left_stack.max_via_stack_num != right_stack.max_via_stack_num
      || routingLayerName(left_routing, left_stack.bottom_routing_layer_idx)
             != routingLayerName(right_routing, right_stack.bottom_routing_layer_idx)
      || routingLayerName(left_routing, left_stack.top_routing_layer_idx)
             != routingLayerName(right_routing, right_stack.top_routing_layer_idx)) {
    return fail("max_via_stack differs");
  }

  if (left.get_exist_rule_set() != right.get_exist_rule_set()) {
    return fail("exist_rule_set differs");
  }

  if (left_routing.size() != right_routing.size()) {
    return fail("routing_layer_count " + std::to_string(left_routing.size()) + " vs " + std::to_string(right_routing.size()));
  }
  for (size_t index = 0; index < left_routing.size(); ++index) {
    auto& lhs = left_routing[index];
    auto& rhs = right_routing[index];
    if (lhs.get_layer_name() != rhs.get_layer_name()) {
      return fail("routing_layer[" + std::to_string(index) + "] " + lhs.get_layer_name() + " vs " + rhs.get_layer_name());
    }
    if (lhs.get_prefer_direction() != rhs.get_prefer_direction() || lhs.get_pitch() != rhs.get_pitch()
        || lhs.get_minimum_width_rule().min_width != rhs.get_minimum_width_rule().min_width
        || lhs.get_minimum_area_rule().min_area != rhs.get_minimum_area_rule().min_area
        || lhs.get_notch_spacing_rule().notch_spacing != rhs.get_notch_spacing_rule().notch_spacing
        || lhs.get_notch_spacing_rule().notch_length != rhs.get_notch_spacing_rule().notch_length
        || lhs.get_notch_spacing_rule().concave_ends != rhs.get_notch_spacing_rule().concave_ends) {
      return fail("routing_layer " + lhs.get_layer_name() + " fields differ");
    }
    auto& left_prl = lhs.get_parallel_run_length_spacing_rule();
    auto& right_prl = rhs.get_parallel_run_length_spacing_rule();
    const auto layer_spacing_equal = [](const idrc::LayerSpacing& left_spacing, const idrc::LayerSpacing& right_spacing) {
      return left_spacing.spacing_type == right_spacing.spacing_type && left_spacing.min_spacing == right_spacing.min_spacing
             && left_spacing.min_width == right_spacing.min_width && left_spacing.max_width == right_spacing.max_width;
    };
    const std::string prl_prefix = "routing_layer " + lhs.get_layer_name() + " prl ";
    if (left_prl.width_list != right_prl.width_list) {
      return fail(prl_prefix + "width_list differs");
    }
    if (left_prl.parallel_length_list != right_prl.parallel_length_list) {
      return fail(prl_prefix + "parallel_length_list differs");
    }
    if (!equalGridMap(left_prl.width_parallel_length_map, right_prl.width_parallel_length_map)) {
      return fail(prl_prefix + "spacing_table cells differ");
    }
    if (left_prl.has_spacing_table != right_prl.has_spacing_table || left_prl.has_spacing_list != right_prl.has_spacing_list) {
      return fail(prl_prefix + "presence flags differ");
    }
    if (left_prl.spacing_list.size() != right_prl.spacing_list.size()) {
      return fail(prl_prefix + "spacing_count " + std::to_string(left_prl.spacing_list.size()) + " vs "
                  + std::to_string(right_prl.spacing_list.size()));
    }
    for (size_t spacing_index = 0; spacing_index < left_prl.spacing_list.size(); ++spacing_index) {
      if (!layer_spacing_equal(left_prl.spacing_list[spacing_index], right_prl.spacing_list[spacing_index])) {
        return fail(prl_prefix + "spacing[" + std::to_string(spacing_index) + "] differs");
      }
    }
    auto& left_eols = lhs.get_end_of_line_spacing_rule_list();
    auto& right_eols = rhs.get_end_of_line_spacing_rule_list();
    if (left_eols.size() != right_eols.size()) {
      return fail("routing_layer " + lhs.get_layer_name() + " eol_count " + std::to_string(left_eols.size()) + " vs "
                  + std::to_string(right_eols.size()));
    }
    for (size_t eol_index = 0; eol_index < left_eols.size(); ++eol_index) {
      if (!equalEol(left_eols[eol_index], right_eols[eol_index])) {
        return fail("routing_layer " + lhs.get_layer_name() + " eol[" + std::to_string(eol_index) + "] differs");
      }
    }
    auto& left_corners = lhs.get_corner_spacing_rule_list();
    auto& right_corners = rhs.get_corner_spacing_rule_list();
    if (left_corners.size() != right_corners.size()) {
      return fail("routing_layer " + lhs.get_layer_name() + " corner_spacing_count " + std::to_string(left_corners.size())
                  + " vs " + std::to_string(right_corners.size()));
    }
    for (size_t corner_index = 0; corner_index < left_corners.size(); ++corner_index) {
      const auto& left_corner = left_corners[corner_index];
      const auto& right_corner = right_corners[corner_index];
      if (left_corner.has_convex_corner != right_corner.has_convex_corner
          || left_corner.has_concave_corner != right_corner.has_concave_corner
          || left_corner.has_except_eol != right_corner.has_except_eol || left_corner.except_eol != right_corner.except_eol
          || left_corner.width_spacing_list != right_corner.width_spacing_list) {
        return fail("routing_layer " + lhs.get_layer_name() + " corner_spacing[" + std::to_string(corner_index) + "] differs");
      }
    }
  }

  auto& left_cuts = left.get_cut_layer_list();
  auto& right_cuts = right.get_cut_layer_list();
  if (left_cuts.size() != right_cuts.size()) {
    return fail("cut_layer_count " + std::to_string(left_cuts.size()) + " vs " + std::to_string(right_cuts.size()));
  }
  for (size_t index = 0; index < left_cuts.size(); ++index) {
    auto& lhs = left_cuts[index];
    auto& rhs = right_cuts[index];
    if (lhs.get_layer_name() != rhs.get_layer_name()) {
      return fail("cut_layer[" + std::to_string(index) + "] " + lhs.get_layer_name() + " vs " + rhs.get_layer_name());
    }
    auto& left_same = lhs.get_same_layer_cut_spacing_rule();
    auto& right_same = rhs.get_same_layer_cut_spacing_rule();
    auto& left_diff = lhs.get_different_layer_cut_spacing_rule();
    auto& right_diff = rhs.get_different_layer_cut_spacing_rule();
    auto& left_adjacent = lhs.get_adjacent_cut_rule();
    auto& right_adjacent = rhs.get_adjacent_cut_rule();
    const auto same_spacing_equal = [](const idrc::SameLayerCutSpacing& left_spacing,
                                       const idrc::SameLayerCutSpacing& right_spacing) {
      return left_spacing.curr_spacing == right_spacing.curr_spacing && left_spacing.curr_prl == right_spacing.curr_prl
             && left_spacing.curr_prl_spacing == right_spacing.curr_prl_spacing
             && left_spacing.has_same_net == right_spacing.has_same_net;
    };
    if (left_same.spacings.size() != right_same.spacings.size()
        || !std::equal(left_same.spacings.begin(), left_same.spacings.end(), right_same.spacings.begin(), same_spacing_equal)
        || left_diff.above_spacing != right_diff.above_spacing || left_diff.above_prl != right_diff.above_prl
        || left_diff.above_prl_spacing != right_diff.above_prl_spacing || left_diff.below_spacing != right_diff.below_spacing
        || left_diff.below_prl != right_diff.below_prl || left_diff.below_prl_spacing != right_diff.below_prl_spacing
        || left_adjacent.cut_spacing != right_adjacent.cut_spacing || left_adjacent.adjacnet_cuts != right_adjacent.adjacnet_cuts
        || left_adjacent.cut_within != right_adjacent.cut_within) {
      return fail("cut_layer " + lhs.get_layer_name() + " fields differ");
    }
  }

  const auto left_macro_obs = macroObsGroupKeys(left);
  const auto right_macro_obs = macroObsGroupKeys(right);
  if (left_macro_obs != right_macro_obs) {
    return fail(describeFirstMismatch(left_macro_obs, right_macro_obs));
  }
  return {};
}

}  // namespace eccdb::test
