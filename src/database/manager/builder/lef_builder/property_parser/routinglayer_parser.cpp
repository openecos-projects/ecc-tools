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
#include "utility/logger/Logger.hpp"
#include "routinglayer_parser.h"

#include <memory>
#include <utility>

#include "absl/strings/match.h"
#include "db_property/IdbRoutingLayerLef58Property.h"
#include "lef58_property/layer_property_parser.h"
#include "lef58_property/routinglayer_property_parser.h"
#include "property_parser/lef58_property/routinglayer_property.h"

namespace idb {
bool RoutingLayerParser::parse(const std::string& name, const std::string& value, IdbLayerRouting* data)
{
  if (name == "LEF58_TYPE") {
    return parse_lef58_type(value, data);
  }
  if (name == "LEF58_BACKSIDE") {
    return parse_lef58_backside(value, data);
  }
  if (name == "LEF58_RECTONLY") {
    return parse_lef58_rectonly(value, data);
  }
  if (name == "LEF58_RIGHTWAYONGRIDONLY") {
    return parse_lef58_rightwayongridonly(value, data);
  }
  if (name == "LEF58_AREA") {
    return parse_lef58_area(value, data);
  }
  if (name == "LEF58_CORNERFILLSPACING") {
    return parse_lef58_conerfillspacing(value, data);
  }
  if (name == "LEF58_CORNERSPACING") {
    return parse_lef58_cornerspacing(value, data);
  }
  if (name == "LEF58_MINIMUMCUT") {
    return parse_lef58_minimuncut(value, data);
  }
  if (name == "LEF58_MINSTEP") {
    return parse_lef58_minstep(value, data);
  }
  if (name == "LEF58_SPACING") {
    return parse_lef58_spacing(value, data);
  }
  if (name == "LEF58_SPACINGTABLE") {
    return parse_lef58_spacingtable(value, data);
  }
  if (name == "LEF58_WIDTHTABLE") {
    return parse_lef58_widthtable(value, data);
  }
  ECCLOG.warn(ecc::Loc::current(), "Unhandled property: ", name, value);
  return false;
}

bool RoutingLayerParser::parse_lef58_type(const std::string& value, IdbLayerRouting* data)
{
  std::string type;
  const bool parse_ok = layer_property::parse_lef58_type(value.begin(), value.end(), type);
  if (!parse_ok) {
    return false;
  }
  data->set_lef58_type(std::move(type));
  return true;
}

bool RoutingLayerParser::parse_lef58_backside(const std::string& value, IdbLayerRouting* data)
{
  const bool parse_ok = layer_property::parse_lef58_backside(value.begin(), value.end());
  if (!parse_ok) {
    return false;
  }
  data->set_lef58_backside(true);
  return true;
}

bool RoutingLayerParser::parse_lef58_rectonly(const std::string& value, IdbLayerRouting* data)
{
  bool except_non_core_pins = false;
  const bool parse_ok = layer_property::parse_lef58_rectonly(value.begin(), value.end(), except_non_core_pins);
  if (!parse_ok) {
    return false;
  }
  data->set_lef58_rect_only(true);
  data->set_lef58_rect_only_except_non_core_pins(except_non_core_pins);
  return true;
}

bool RoutingLayerParser::parse_lef58_rightwayongridonly(const std::string& value, IdbLayerRouting* data)
{
  bool check_mask = false;
  const bool parse_ok = layer_property::parse_lef58_rightwayongridonly(value.begin(), value.end(), check_mask);
  if (!parse_ok) {
    return false;
  }
  data->set_lef58_right_way_on_grid_only(true);
  data->set_lef58_right_way_on_grid_only_check_mask(check_mask);
  return true;
}

bool RoutingLayerParser::parse_lef58_area(const std::string& value, IdbLayerRouting* data)
{
  std::vector<routinglayer_property::lef58_area> areas;
  bool parse_ok = routinglayer_property::parse_lef58_area(value.begin(), value.end(), areas);
  if (not parse_ok) {
    return false;
  }
  for (auto& area_data : areas) {
    auto area = std::make_shared<routinglayer::Lef58Area>(transAreaDB(area_data._min_area));

    if (area_data._mask_num) {
      area->set_mask(static_cast<uint32_t>(area_data._mask_num.value()));
    }
    if (area_data._except_min_width) {
      area->set_except_min_width(transUnitDB(area_data._except_min_width.value()));
    }
    if (area_data._exceptedgelength) {
      auto except_edge_length = std::make_shared<routinglayer::Lef58Area::ExceptEdgeLength>();
      auto& except_data = area_data._exceptedgelength.value();
      if (except_data._max_edge_length) {
        except_edge_length->set_min_edge_length(transUnitDB(except_data._min_edge_length));
        except_edge_length->set_max_edge_length(transUnitDB(except_data._max_edge_length.value()));
      } else {
        // excpet_length is max_edge_length
        except_edge_length->set_max_edge_length(transUnitDB(except_data._min_edge_length));
      }
      area->set_except_edge_length(except_edge_length);
    }
    for (auto& min_sz : area_data._except_min_size) {
      area->add_except_min_size(routinglayer::Lef58Area::ExceptMinSize(transUnitDB(min_sz.first), transUnitDB(min_sz.second)));
    }
    if (area_data._except_step) {
      area->set_except_step(
          routinglayer::Lef58Area::ExceptStep(transUnitDB(area_data._except_step->first), transUnitDB(area_data._except_step->second)));
    }
    if (area_data._rect_width) {
      area->set_rect_width(transUnitDB(area_data._rect_width.value()));
    }
    area->set_except_rectangle(!area_data._exceptrectangle.empty());
    if (!area_data._trim_layer.empty()) {
      area->set_trim_layer(std::move(area_data._trim_layer));
    }
    if (area_data._overlap) {
      area->set_overlap(static_cast<uint32_t>(area_data._overlap.value()));
    }

    data->add_lef58_area(area);
  }

  return true;
}
bool RoutingLayerParser::parse_lef58_conerfillspacing(const std::string& value, IdbLayerRouting* data)
{
  routinglayer_property::lef58_cornerfillspacing cornerspacing;
  bool parse_ok = routinglayer_property::parse_lef58_conerfillspacing(value.begin(), value.end(), cornerspacing);
  if (not parse_ok) {
    return false;
  }
  //
  auto cornerfill_spacing = std::make_shared<routinglayer::Lef58CornerFillSpacing>();
  data->set_lef58_cornerfill_spacing(cornerfill_spacing);
  cornerfill_spacing->set_spacing(transUnitDB(cornerspacing._spacing));
  cornerfill_spacing->set_length1(transUnitDB(cornerspacing._length1));
  cornerfill_spacing->set_length2(transUnitDB(cornerspacing._length2));
  cornerfill_spacing->set_eol_width(transUnitDB(cornerspacing._eol_width));
  return true;
}
bool RoutingLayerParser::parse_lef58_cornerspacing(const std::string& value, IdbLayerRouting* data)
{
  std::vector<routinglayer_property::lef58_cornerspacing> cornerspacings;
  bool parse_ok = routinglayer_property::parse_lef58_cornerspacing(value.begin(), value.end(), cornerspacings);
  if (not parse_ok) {
    return false;
  }

  for (const auto& cornerspacing : cornerspacings) {
    auto corner_spacing = std::make_shared<routinglayer::Lef58CornerSpacing>();
    data->add_lef58_corner_spacing(corner_spacing);

    if (cornerspacing._corner_type == "CONVEXCORNER") {
      corner_spacing->set_corner_type(routinglayer::Lef58CornerSpacing::CornerType::kConvexCorner);
    } else if (cornerspacing._corner_type == "CONCAVECORNER") {
      corner_spacing->set_corner_type(routinglayer::Lef58CornerSpacing::CornerType::kConcaveCorner);
    }

    if (cornerspacing._except_eol.has_value()) {
      corner_spacing->set_except_eol(transUnitDB(cornerspacing._except_eol.value()));
    }
    corner_spacing->set_corner_to_corner(!cornerspacing._corner_to_corner.empty());

    for (const auto& width_spacing : cornerspacing._width_spacings) {
      corner_spacing->add_width_spacing(transUnitDB(width_spacing._width), transUnitDB(width_spacing._spacing));
    }
  }
  return true;
}
bool RoutingLayerParser::parse_lef58_minimuncut(const std::string& value, IdbLayerRouting* data)
{
  std::vector<routinglayer_property::lef58_minimumcut> minimuncuts;
  bool parse_ok = routinglayer_property::parse_lef58_minimumcut(value.begin(), value.end(), minimuncuts);
  if (not parse_ok) {
    return false;
  }
  for (auto& item : minimuncuts) {
    auto minimum_cut = std::make_shared<routinglayer::Lef58MinimumCut>();
    data->add_lef58_minimum_cut(minimum_cut);
    if (item._num_cuts) {
      minimum_cut->set_num_cuts(item._num_cuts.value());
    } else {
      for (auto& cut : item._cuts) {
        minimum_cut->add_cutclass(routinglayer::Lef58MinimumCut::CutClass{std::move(cut._class_name), cut._num_cuts});
      }
    }
    minimum_cut->set_width(transUnitDB(item._width));
    if (item._cut_distance) {
      minimum_cut->set_within_cut_distance(transUnitDB(item._cut_distance.value()));
    }
    minimum_cut->set_orient(item._direction);
    if (item._length && item._length_within) {
      minimum_cut->set_length(
          routinglayer::Lef58MinimumCut::Length{transUnitDB(item._length.value()), transUnitDB(item._length_within.value())});
    }
    if (item._area) {
      routinglayer::Lef58MinimumCut::Area area(transAreaDB(item._area.value()));
      if (item._area_within) {
        area.set_within_distance(transUnitDB(item._area_within.value()));
      }
      minimum_cut->set_area(area);
    }
    minimum_cut->set_same_metal_overlap(not item._samemetal_overlap.empty());
    minimum_cut->set_fully_enclosed(not item._fully_enclosed.empty());
  }

  return true;
}
bool RoutingLayerParser::parse_lef58_minstep(const std::string& value, IdbLayerRouting* data)
{
  std::vector<routinglayer_property::lef58_minstep> minsteps;
  bool parse_ok = routinglayer_property::parse_lef58_minstep(value.begin(), value.end(), minsteps);
  if (not parse_ok) {
    return false;
  }
  //
  for (auto& item : minsteps) {
    auto minstep = std::make_shared<routinglayer::Lef58MinStep>(transUnitDB(item._min_step_length));
    data->add_lef58_min_step(minstep);
    minstep->set_type(item._type);
    if (item._max_length) {
      minstep->set_max_length_sum(transUnitDB(item._max_length.value()));
    }
    if (item._max_edges) {
      minstep->set_max_edges(item._max_edges.value());
    }
    minstep->set_except_rectangle(!item._except_rectangle.empty());
    if (item._min_adj_length || item._min_adj_length2 || !item._convex_corner.empty() || !item._concave_corner.empty()
        || item._except_within) {
      routinglayer::Lef58MinStep::MinAdjacentLength min_adj_len(item._min_adj_length ? transUnitDB(item._min_adj_length.value()) : 0);
      if (item._min_adj_length2) {
        min_adj_len.set_min_adj_length2(transUnitDB(item._min_adj_length2.value()));
      }
      min_adj_len.set_convex_corner(!item._convex_corner.empty());
      min_adj_len.set_concave_corner(!item._concave_corner.empty());
      if (item._except_within) {
        min_adj_len.set_except_within(transUnitDB(*item._except_within));
      }
      minstep->set_min_adjacent_length(min_adj_len);
    }
    minstep->set_three_concave_corners(!item._three_concave_corners.empty());
    if (item._center_width) {
      minstep->set_center_width(transUnitDB(item._center_width.value()));
    }
    if (item._min_between_length) {
      minstep->set_min_between_length(transUnitDB(item._min_between_length.value()));
    }
    minstep->set_except_same_corners(!item._except_same_corners.empty());
    if (item._no_adjacent_eol) {
      minstep->set_no_adjacent_eol_width(transUnitDB(item._no_adjacent_eol.value()));
    }
    if (item._except_adjacent_length) {
      minstep->set_except_adjacent_length(transUnitDB(item._except_adjacent_length.value()));
    }
    if (item._min_adjacent_length) {
      minstep->set_followup_min_adjacent_length(transUnitDB(item._min_adjacent_length.value()));
    }
    minstep->set_concave_corners(!item._concavecorners.empty());
    if (item._no_between_eol) {
      minstep->set_no_between_eol_width(transUnitDB(item._no_between_eol.value()));
    }
  }
  return true;
}

bool RoutingLayerParser::parse_lef58_widthtable(const std::string& value, IdbLayerRouting* data)
{
  std::vector<routinglayer_property::lef58_widthtable> widthtables;
  bool parse_ok = routinglayer_property::parse_lef58_widthtable(value.begin(), value.end(), widthtables);
  if (not parse_ok) {
    return false;
  }
  for (const auto& item : widthtables) {
    auto widthtable = std::make_shared<routinglayer::Lef58WidthTable>();
    for (const auto width : item._widths) {
      widthtable->add_width(transUnitDB(width));
    }
    widthtable->set_wrong_direction(!item._wrong_direction.empty());
    widthtable->set_orthogonal(!item._orthogonal.empty());
    data->add_lef58_width_table(std::move(widthtable));
  }
  return true;
}

bool RoutingLayerParser::parse_lef58_spacing_eol(const std::string& value, IdbLayerRouting* data)
{
  std::vector<routinglayer_property::lef58_spacing_eol> spacings;
  bool parse_ok = routinglayer_property::parse_lef58_spacing_eol(value.begin(), value.end(), spacings);
  if (not parse_ok) {
    return false;
  }
  for (auto& spacing : spacings) {
    auto spacing_eol = std::make_shared<routinglayer::Lef58SpacingEol>();
    spacing_eol->set_eol_space(transUnitDB(spacing._eol_space));
    spacing_eol->set_eol_width(transUnitDB(spacing._eol_width));
    spacing_eol->set_exact_width(!spacing._exact_width.empty());
    if (spacing._wrong_dir_space) {
      spacing_eol->set_wrong_dir_space(transUnitDB(spacing._wrong_dir_space.value()));
    }
    if (spacing._opposite_width) {
      spacing_eol->set_opposite_width(transUnitDB(spacing._opposite_width.value()));
    }
    if (spacing._eol_within) {
      spacing_eol->set_eol_within(transUnitDB(spacing._eol_within.value()));
    }
    if (spacing._wrong_dir_within) {
      spacing_eol->set_wrong_dir_within(transUnitDB(spacing._wrong_dir_within.value()));
    }
    spacing_eol->set_same_mask(!spacing._same_mask.empty());
    if (spacing._except_exact_width) {
      spacing_eol->set_except_exact_width(routinglayer::Lef58SpacingEol::ExceptExactWidth(
          transUnitDB(spacing._except_exact_width->first), transUnitDB(spacing._except_exact_width->second)));
    }
    if (spacing._fill_triangle) {
      spacing_eol->set_fill_concave_corner_width(transUnitDB(spacing._fill_triangle.value()));
    }
    if (spacing._withcut) {
      auto& withcut_ = spacing._withcut.value();
      routinglayer::Lef58SpacingEol::WithCut withcut;
      withcut.set_cutclass(std::move(withcut_._cutclass));
      withcut.set_above(!withcut_._above.empty());
      withcut.set_with_cut_space(transUnitDB(withcut_._with_cut_space));
      if (withcut_._enclosure_end_width) {
        withcut.set_enclosure_end_width(transUnitDB(withcut_._enclosure_end_width.value()));
      }
      if (withcut_._enclosure_end_within) {
        withcut.set_enclosure_end_within(transUnitDB(withcut_._enclosure_end_within.value()));
      }
      spacing_eol->set_with_cut(std::move(withcut));
    }
    if (spacing._end_prl_spacing) {
      spacing_eol->set_end_prl_spacing(routinglayer::Lef58SpacingEol::EndPrlSpacing(
          transUnitDB(spacing._end_prl_spacing->_end_prl_space), transUnitDB(spacing._end_prl_spacing->_end_prl)));
    }

    if (spacing._end_to_end) {
      auto& end2end_ = spacing._end_to_end.value();
      routinglayer::Lef58SpacingEol::EndToEnd end2end;
      end2end.set_end_to_end_space(transUnitDB(end2end_._end_to_end_space));
      if (end2end_._one_cut_space) {
        end2end.set_one_cut_space(transUnitDB(end2end_._one_cut_space.value()));
      }
      if (end2end_._two_cut_space) {
        end2end.set_two_cut_space(transUnitDB(end2end_._two_cut_space.value()));
      }
      if (end2end_._extension) {
        end2end.set_extionsion(transUnitDB(end2end_._extension.value()));
      }
      if (end2end_._wrong_dir_extension) {
        end2end.set_wrong_dir_extionsion(transUnitDB(end2end_._wrong_dir_extension.value()));
      }
      if (end2end_._other_end_width) {
        end2end.set_other_end_width(transUnitDB(end2end_._other_end_width.value()));
      }
      spacing_eol->set_end_to_end(end2end);
    }
    if (spacing._max_length || spacing._min_length) {
      routinglayer::Lef58SpacingEol::AdjEdgeLength adj_length;
      if (spacing._max_length) {
        adj_length.set_max_length(transUnitDB(spacing._max_length.value()));
      }
      if (spacing._min_length) {
        adj_length.set_min_length(transUnitDB(spacing._min_length.value()));
      }
      adj_length.set_two_sides(!spacing._two_sides.empty());
      spacing_eol->set_adj_edge_length(adj_length);
    }
    spacing_eol->set_equal_rect_width(!spacing._equal_rect_width.empty());

    if (spacing._parallel_edge) {
      auto& parallel_edge_ = spacing._parallel_edge.value();
      routinglayer::Lef58SpacingEol::ParallelEdge par_edge;
      par_edge.set_par_space(transUnitDB(parallel_edge_._par_space));
      par_edge.set_subtract_eol_width(!parallel_edge_._subtract_eol_width.empty());
      par_edge.set_par_within(transUnitDB(parallel_edge_._par_within));
      if (parallel_edge_._prl) {
        par_edge.set_prl(transUnitDB(parallel_edge_._prl.value()));
      }
      if (parallel_edge_._min_length) {
        par_edge.set_min_length(transUnitDB(parallel_edge_._min_length.value()));
      }
      par_edge.set_two_edges(!parallel_edge_._two_edgs.empty());
      par_edge.set_same_metal(!parallel_edge_._same_metal.empty());
      par_edge.set_non_eol_corner_only(!parallel_edge_._non_eol_corner_only.empty());
      par_edge.set_parallel_same_mask(!parallel_edge_._parallel_same_mask.empty());

      spacing_eol->set_parallel_edge(par_edge);
    }
    if (spacing._enclose_cut) {
      auto& cut_ = spacing._enclose_cut.value();
      routinglayer::Lef58SpacingEol::EncloseCut cut;

      cut.set_direction(cut_._direction);
      // cut.set_enclose_dist(dist);
      // int32_t t = transUnitDB(cut_._cut_to_metal_space);
      // cut.set_cut_to_metal_space(t);
      // cut.set_cut_to_metal_space(transUnitDB(cut_._cut_to_metal_space));
      cut.set_all_cuts(!cut_._all_cuts.empty());
      cut.set_enclose_dist(transUnitDB(cut_._enclose_dist));
      cut.set_cut_to_metal_space(transUnitDB(cut_._cut_to_metal_space));
      spacing_eol->set_enclose_cut(cut);
    }
    if (spacing._toconcavecorner) {
      auto& corner_ = spacing._toconcavecorner.value();
      routinglayer::Lef58SpacingEol::ToConcaveCorner corner;
      if (corner_._min_length) {
        corner.set_min_length(transUnitDB(corner_._min_length.value()));
      }
      if (corner_._min_adj_length1) {
        corner.set_min_adj_length1(transUnitDB(corner_._min_adj_length1.value()));
      }
      if (corner_._min_adj_length2) {
        corner.set_min_adj_length2(transUnitDB(corner_._min_adj_length2.value()));
      }
      spacing_eol->set_to_concave_corner(corner);
    }
    if (spacing._notch_length) {
      spacing_eol->set_notch_length(transUnitDB(spacing._notch_length.value()));
    }

    data->add_spacing_eol(spacing_eol);
  }
  //
  return true;
}

bool RoutingLayerParser::parse_lef58_spacing_notchlength(const std::string& value, IdbLayerRouting* data)
{
  routinglayer_property::lef58_spacing_notchlength spacing_notchlen;
  bool parse_ok = routinglayer_property::parse_lef58_spacing_notchlength(value.begin(), value.end(), spacing_notchlen);
  if (not parse_ok) {
    return false;
  }
  auto spacing = std::make_shared<routinglayer::Lef58SpacingNotchlength>(transUnitDB(spacing_notchlen._min_spacing),
                                                                         transUnitDB(spacing_notchlen._min_notch_length));
  data->set_lef58_spacing_notchlength(spacing);
  if (spacing_notchlen._side_type == "CONCAVEENDS") {
    spacing->set_concave_ends_side_of_notch_width(transUnitDB(*spacing_notchlen._side_of_notch_width));
  }
  return true;
}

bool RoutingLayerParser::parse_lef58_spacingtable_jogtojog(const std::string& value, IdbLayerRouting* data)
{
  routinglayer_property::lef58_spacingtable_jogtojog stbl;
  bool ok = routinglayer_property::parse_lef58_spacingtable_jogtojog(value.begin(), value.end(), stbl);
  if (not ok) {
    return false;
  }
  //
  auto spacingtable_ptr = std::make_shared<routinglayer::Lef58SpacingTableJogToJog>(
      transUnitDB(stbl._jog2jog_spacing), transUnitDB(stbl._jog_width), transUnitDB(stbl._short_jog_spacing));
  data->set_lef58_spacingtable_jogtojog(spacingtable_ptr);
  for (const auto& width : stbl._width) {
    spacingtable_ptr->add_width(transUnitDB(width._width), transUnitDB(width._par_length), transUnitDB(width._par_within),
                                transUnitDB(width._long_jog_spacing));
  }
  return true;
}

bool RoutingLayerParser::parse_lef58_spacingtable_prl(const std::string& value, IdbLayerRouting* data)
{
  routinglayer_property::lef58_spacingtable_prl stbl;
  bool ok = routinglayer_property::parse_lef58_spacingtable_prl(value.begin(), value.end(), stbl);
  if (not ok) {
    return false;
  }

  auto spacingtable = std::make_shared<routinglayer::Lef58SpacingTablePrl>();
  spacingtable->set_wrong_direction(!stbl._wrong_direction.empty());
  spacingtable->set_same_mask(!stbl._same_mask.empty());
  if (stbl._except_eol_width) {
    spacingtable->set_except_eol_width(transUnitDB(stbl._except_eol_width.value()));
  }
  for (const auto length : stbl._parallel_run_lengths) {
    spacingtable->add_parallel_run_length(transUnitDB(length));
  }
  for (const auto& source_width : stbl._widths) {
    std::vector<int32_t> spacings;
    spacings.reserve(source_width._spacings.size());
    for (const auto spacing : source_width._spacings) {
      spacings.push_back(transUnitDB(spacing));
    }
    routinglayer::Lef58SpacingTablePrl::Width width(transUnitDB(source_width._width), std::move(spacings));
    if (source_width._except_within) {
      width.set_except_within(transUnitDB(source_width._except_within->first), transUnitDB(source_width._except_within->second));
    }
    spacingtable->add_width(std::move(width));
  }
  if (stbl._influences) {
    for (const auto& influence : stbl._influences.value()) {
      spacingtable->add_influence(routinglayer::Lef58SpacingTablePrl::Influence(
          transUnitDB(influence._width), transUnitDB(influence._within), transUnitDB(influence._spacing)));
    }
  }
  data->add_lef58_spacingtable_prl(std::move(spacingtable));
  return true;
}

bool RoutingLayerParser::parse_lef58_spacing(const std::string& value, IdbLayerRouting* data)
{
  if (absl::StrContains(value, "NOTCHLENGTH")) {
    return parse_lef58_spacing_notchlength(value, data);
  }
  if (absl::StrContains(value, "ENDOFLINE")) {
    return parse_lef58_spacing_eol(value, data);
  }
  ECCLOG.warn(ecc::Loc::current(), "Unhandled LEF58_SPACING value: ", value);
  return false;
}

bool RoutingLayerParser::parse_lef58_spacingtable(const std::string& value, IdbLayerRouting* data)
{
  if (absl::StrContains(value, "PARALLELRUNLENGTH")) {
    return parse_lef58_spacingtable_prl(value, data);
  }
  if (absl::StrContains(value, "JOGTOJOGSPACING")) {
    return parse_lef58_spacingtable_jogtojog(value, data);
  }
  ECCLOG.warn(ecc::Loc::current(), "Unhandled LEF58_SPACINGTABLE value: ", value);
  return false;
}

}  // namespace idb
