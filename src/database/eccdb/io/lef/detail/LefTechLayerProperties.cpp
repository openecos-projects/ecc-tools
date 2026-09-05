#include "lef/detail/LefTechLayerProperties.h"

#include <algorithm>
#include <boost/spirit/include/qi.hpp>
#include <boost/variant/get.hpp>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "property_parser/lef58_property/cutlayer_property_parser.h"
#include "property_parser/lef58_property/layer_property_parser.h"
#include "property_parser/lef58_property/routinglayer_property_parser.h"
#include "tech/TechStore.h"
#include "tech/cut_layer/storage/CutLayerStorage.h"
#include "tech/masterslice_layer/storage/MastersliceLayerStorage.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/routing_layer/storage/RoutingLayerStorage.h"

namespace eccdb::lef_detail {

namespace cutlayer_property = idb::cutlayer_property;
namespace layer_property = idb::layer_property;
namespace routinglayer_property = idb::routinglayer_property;

namespace {

std::string upper(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  return value;
}

int32_t toDatabaseUnits(double value, int32_t units, const char* field)
{
  if (units <= 0) {
    throw std::invalid_argument("database units per micron must be positive");
  }
  if (!std::isfinite(value) || value < 0.0) {
    throw std::runtime_error(std::string(field) + " must be non-negative");
  }
  const auto scaled = value * static_cast<double>(units);
  if (!std::isfinite(scaled) || scaled > static_cast<double>(std::numeric_limits<int32_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside int32 DBU range");
  }
  return static_cast<int32_t>(std::llround(scaled));
}

int32_t toSignedDatabaseUnits(double value, int32_t units, const char* field)
{
  if (units <= 0) {
    throw std::invalid_argument("database units per micron must be positive");
  }
  const auto scaled = value * static_cast<double>(units);
  if (!std::isfinite(scaled) || scaled < static_cast<double>(std::numeric_limits<int32_t>::min())
      || scaled > static_cast<double>(std::numeric_limits<int32_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside int32 DBU range");
  }
  return static_cast<int32_t>(std::llround(scaled));
}

int64_t toDatabaseArea(double value, int32_t units, const char* field)
{
  if (units <= 0) {
    throw std::invalid_argument("database units per micron must be positive");
  }
  if (!std::isfinite(value) || value < 0.0) {
    throw std::runtime_error(std::string(field) + " must be non-negative");
  }
  const auto scaled = value * static_cast<double>(units) * static_cast<double>(units);
  if (!std::isfinite(scaled) || scaled > static_cast<double>(std::numeric_limits<int64_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside int64 DBU area range");
  }
  return static_cast<int64_t>(std::llround(scaled));
}

template <typename Parser, typename Value>
void requireParsed(const TechProperty& property, Parser&& parser, Value& value)
{
  auto text = property.value;
  const auto last = text.find_last_not_of(" \t\r\n");
  if (last == std::string::npos || text[last] != ';') {
    text += " ;";
  }
  if (!parser(text.cbegin(), text.cend(), value)) {
    throw std::runtime_error("failed to parse " + property.name + " property");
  }
}

CutLayerSide cutSide(std::string_view value) noexcept
{
  if (value == "ABOVE") {
    return CutLayerSide::kAbove;
  }
  if (value == "BELOW") {
    return CutLayerSide::kBelow;
  }
  return CutLayerSide::kUnknown;
}

CutDirection cutDirection(std::string_view value) noexcept
{
  if (value == "HORIZONTAL") {
    return CutDirection::kHorizontal;
  }
  if (value == "VERTICAL") {
    return CutDirection::kVertical;
  }
  return CutDirection::kUnknown;
}

CutClassEdge cutClassEdge(std::string_view value) noexcept
{
  if (value == "SIDE") return CutClassEdge::kSide;
  if (value == "END") return CutClassEdge::kEnd;
  return CutClassEdge::kUnspecified;
}

TechRoutingMinStepType minStepType(std::string_view value) noexcept
{
  if (value == "INSIDECORNER") {
    return TechRoutingMinStepType::kInsideCorner;
  }
  if (value == "OUTSIDECORNER") {
    return TechRoutingMinStepType::kOutsideCorner;
  }
  if (value == "STEP") {
    return TechRoutingMinStepType::kStep;
  }
  return TechRoutingMinStepType::kNone;
}

TechRoutingMinimumCutOrient minimumCutOrient(std::string_view value) noexcept
{
  if (value == "FROMABOVE") {
    return TechRoutingMinimumCutOrient::kFromAbove;
  }
  if (value == "FROMBELOW") {
    return TechRoutingMinimumCutOrient::kFromBelow;
  }
  return TechRoutingMinimumCutOrient::kNone;
}

void prepareRoutingArea(const TechProperty& property, int32_t units, PreparedRoutingLef58Properties& result)
{
  std::vector<routinglayer_property::lef58_area> parsed;
  requireParsed(property, routinglayer_property::parse_lef58_area<std::string::const_iterator>, parsed);
  for (const auto& source : parsed) {
    TechRoutingLef58AreaRule rule{.min_area = toDatabaseArea(source._min_area, units, "LEF58 AREA")};
    if (source._mask_num) {
      if (*source._mask_num <= 0) {
        throw std::runtime_error("LEF58 AREA MASK must be positive");
      }
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasMask;
      rule.mask = static_cast<uint32_t>(*source._mask_num);
    }
    if (source._except_min_width) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasExceptMinWidth;
      rule.except_min_width = toDatabaseUnits(*source._except_min_width, units, "LEF58 AREA EXCEPTMINWIDTH");
    }
    if (source._exceptedgelength) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasExceptEdgeLength;
      if (source._exceptedgelength->_max_edge_length) {
        rule.flags |= TechRoutingLef58AreaRuleFlag::kHasExceptMinEdgeLength;
        rule.except_min_edge_length
            = toDatabaseUnits(source._exceptedgelength->_min_edge_length, units, "LEF58 AREA EXCEPTEDGELENGTH minimum");
        rule.except_max_edge_length
            = toDatabaseUnits(*source._exceptedgelength->_max_edge_length, units, "LEF58 AREA EXCEPTEDGELENGTH maximum");
      } else {
        rule.except_max_edge_length
            = toDatabaseUnits(source._exceptedgelength->_min_edge_length, units, "LEF58 AREA EXCEPTEDGELENGTH maximum");
      }
    }
    for (const auto& size : source._except_min_size) {
      rule.except_min_sizes.push_back(
          TechRoutingLef58AreaExceptMinSize{.min_width = toDatabaseUnits(size.first, units, "LEF58 AREA EXCEPTMINSIZE width"),
                                            .min_length = toDatabaseUnits(size.second, units, "LEF58 AREA EXCEPTMINSIZE length")});
    }
    if (source._except_step) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasExceptStep;
      rule.except_step_x = toDatabaseUnits(source._except_step->first, units, "LEF58 AREA EXCEPTSTEP X");
      rule.except_step_y = toDatabaseUnits(source._except_step->second, units, "LEF58 AREA EXCEPTSTEP Y");
    }
    if (source._rect_width) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasRectWidth;
      rule.rect_width = toDatabaseUnits(*source._rect_width, units, "LEF58 AREA RECTWIDTH");
    }
    if (!source._exceptrectangle.empty()) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kExceptRectangle;
    }
    if (!source._trim_layer.empty()) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasTrimLayer;
      rule.trim_layer_name = source._trim_layer;
    }
    if (source._overlap) {
      if (*source._overlap < 0) {
        throw std::runtime_error("LEF58 AREA OVERLAP must be non-negative");
      }
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasOverlap;
      rule.overlap = static_cast<uint32_t>(*source._overlap);
    }
    result.area_rules.push_back(std::move(rule));
  }
}

void prepareRoutingMinimumCut(const TechProperty& property, int32_t units, PreparedRoutingLef58Properties& result)
{
  std::vector<routinglayer_property::lef58_minimumcut> parsed;
  requireParsed(property, routinglayer_property::parse_lef58_minimumcut<std::string::const_iterator>, parsed);
  for (const auto& source : parsed) {
    TechRoutingLef58MinimumCutRule rule{.width = toDatabaseUnits(source._width, units, "LEF58 MINIMUMCUT WIDTH"),
                                        .orient = minimumCutOrient(source._direction)};
    if (source._num_cuts) {
      if (*source._num_cuts <= 0) {
        throw std::runtime_error("LEF58 MINIMUMCUT count must be positive");
      }
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasNumCuts;
      rule.num_cuts = *source._num_cuts;
    } else {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasCutClasses;
      for (const auto& cut : source._cuts) {
        if (cut._num_cuts <= 0) {
          throw std::runtime_error("LEF58 MINIMUMCUT CUTCLASS count must be positive");
        }
        rule.cutclasses.push_back(TechRoutingLef58MinimumCutClass{cut._class_name, cut._num_cuts});
      }
    }
    if (source._cut_distance) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasWithinCutDistance;
      rule.within_cut_distance = toDatabaseUnits(*source._cut_distance, units, "LEF58 MINIMUMCUT WITHIN");
    }
    if (source._length) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasLength;
      rule.length = toDatabaseUnits(*source._length, units, "LEF58 MINIMUMCUT LENGTH");
      rule.length_distance = toDatabaseUnits(*source._length_within, units, "LEF58 MINIMUMCUT LENGTH WITHIN");
    }
    if (source._area) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasArea;
      rule.area = toDatabaseArea(*source._area, units, "LEF58 MINIMUMCUT AREA");
    }
    if (source._area_within) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasAreaWithinDistance;
      rule.area_within_distance = toDatabaseUnits(*source._area_within, units, "LEF58 MINIMUMCUT AREA WITHIN");
    }
    if (!source._samemetal_overlap.empty()) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kSameMetalOverlap;
    }
    if (!source._fully_enclosed.empty()) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kFullyEnclosed;
    }
    result.minimum_cut_rules.push_back(std::move(rule));
  }
}

void prepareRoutingMinStep(const TechProperty& property, int32_t units, PreparedRoutingLef58Properties& result)
{
  std::vector<routinglayer_property::lef58_minstep> parsed;
  requireParsed(property, routinglayer_property::parse_lef58_minstep<std::string::const_iterator>, parsed);
  for (const auto& source : parsed) {
    TechRoutingLef58MinStepRule rule{.type = minStepType(source._type),
                                     .min_step_length = toDatabaseUnits(source._min_step_length, units, "LEF58 MINSTEP")};
    if (!source._type.empty()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasType;
    }
    if (source._max_length) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasMaxLengthSum;
      rule.max_length_sum = toDatabaseUnits(*source._max_length, units, "LEF58 MINSTEP LENGTHSUM");
    }
    if (source._max_edges) {
      if (*source._max_edges < 0) {
        throw std::runtime_error("LEF58 MINSTEP MAXEDGES must be non-negative");
      }
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasMaxEdges;
      rule.max_edges = static_cast<uint32_t>(*source._max_edges);
    }
    if (!source._except_rectangle.empty()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kExceptRectangle;
    }
    if (source._min_adj_length) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasMinAdjacentLength;
      rule.min_adjacent_length = toDatabaseUnits(*source._min_adj_length, units, "LEF58 MINSTEP MINADJACENTLENGTH");
    }
    if (source._min_adj_length2) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasSecondMinAdjacentLength;
      rule.min_adjacent_length2 = toDatabaseUnits(*source._min_adj_length2, units, "LEF58 MINSTEP second MINADJACENTLENGTH");
    }
    if (!source._convex_corner.empty()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kConvexCorner;
    }
    if (source._except_within) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasExceptWithin;
      rule.except_within = toDatabaseUnits(*source._except_within, units, "LEF58 MINSTEP EXCEPTWITHIN");
    }
    if (!source._concave_corner.empty()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kConcaveCorner;
    }
    if (!source._three_concave_corners.empty()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kThreeConcaveCorners;
    }
    if (source._center_width) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasCenterWidth;
      rule.center_width = toDatabaseUnits(*source._center_width, units, "LEF58 MINSTEP CENTERWIDTH");
    }
    if (source._min_between_length) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasMinBetweenLength;
      rule.min_between_length = toDatabaseUnits(*source._min_between_length, units, "LEF58 MINSTEP MINBETWEENLENGTH");
    }
    if (!source._except_same_corners.empty()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kExceptSameCorners;
    }
    if (source._no_adjacent_eol) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasNoAdjacentEol;
      rule.no_adjacent_eol_width = toDatabaseUnits(*source._no_adjacent_eol, units, "LEF58 MINSTEP NOADJACENTEOL");
    }
    if (source._except_adjacent_length) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasExceptAdjacentLength;
      rule.except_adjacent_length = toDatabaseUnits(*source._except_adjacent_length, units, "LEF58 MINSTEP EXCEPTADJACENTLENGTH");
    }
    if (source._min_adjacent_length) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasFollowupMinAdjacentLength;
      rule.followup_min_adjacent_length = toDatabaseUnits(*source._min_adjacent_length, units, "LEF58 MINSTEP followup MINADJACENTLENGTH");
    }
    if (!source._concavecorners.empty()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kConcaveCorners;
    }
    if (source._no_between_eol) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasNoBetweenEol;
      rule.no_between_eol_width = toDatabaseUnits(*source._no_between_eol, units, "LEF58 MINSTEP NOBETWEENEOL");
    }
    result.min_step_rules.push_back(std::move(rule));
  }
}

void prepareRoutingSpacingEol(const TechProperty& property, int32_t units, PreparedRoutingLef58Properties& result)
{
  std::vector<routinglayer_property::lef58_spacing_eol> parsed;
  requireParsed(property, routinglayer_property::parse_lef58_spacing_eol<std::string::const_iterator>, parsed);
  for (const auto& source : parsed) {
    TechRoutingLef58SpacingEolRule rule{.eol_space = toDatabaseUnits(source._eol_space, units, "LEF58 SPACING ENDOFLINE spacing"),
                                        .eol_width = toDatabaseUnits(source._eol_width, units, "LEF58 SPACING ENDOFLINE width")};
    if (!source._exact_width.empty())
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kExactWidth;
    if (source._wrong_dir_space) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasWrongDirSpace;
      rule.wrong_dir_space = toDatabaseUnits(*source._wrong_dir_space, units, "LEF58 SPACING WRONGDIRSPACING");
    }
    if (source._opposite_width) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasOppositeWidth;
      rule.opposite_width = toDatabaseUnits(*source._opposite_width, units, "LEF58 SPACING OPPOSITEWIDTH");
    }
    if (source._eol_within) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEolWithin;
      rule.eol_within = toDatabaseUnits(*source._eol_within, units, "LEF58 SPACING WITHIN");
    }
    if (source._wrong_dir_within) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasWrongDirWithin;
      rule.wrong_dir_within = toDatabaseUnits(*source._wrong_dir_within, units, "LEF58 SPACING wrong-direction WITHIN");
    }
    if (!source._same_mask.empty())
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kSameMask;
    if (source._except_exact_width) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasExceptExactWidth;
      rule.except_exact_width1 = toDatabaseUnits(source._except_exact_width->first, units, "LEF58 SPACING EXCEPTEXACTWIDTH");
      rule.except_exact_width2 = toDatabaseUnits(source._except_exact_width->second, units, "LEF58 SPACING EXCEPTEXACTWIDTH");
    }
    if (source._fill_triangle) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasFillConcaveCorner;
      rule.fill_concave_corner_width = toDatabaseUnits(*source._fill_triangle, units, "LEF58 SPACING FILLCONCAVECORNER");
    }
    if (source._withcut) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasWithCut;
      if (!source._withcut->_cutclass.empty()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasWithCutClass;
        rule.with_cut_class_name = source._withcut->_cutclass;
      }
      if (!source._withcut->_above.empty())
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kWithCutAbove;
      rule.with_cut_space = toDatabaseUnits(source._withcut->_with_cut_space, units, "LEF58 SPACING WITHCUT");
      if (source._withcut->_enclosure_end_width) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEnclosureEndWidth;
        rule.enclosure_end_width = toDatabaseUnits(*source._withcut->_enclosure_end_width, units, "LEF58 SPACING ENCLOSUREEND");
      }
      if (source._withcut->_enclosure_end_within) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEnclosureEndWithin;
        rule.enclosure_end_within = toDatabaseUnits(*source._withcut->_enclosure_end_within, units, "LEF58 SPACING ENCLOSUREEND WITHIN");
      }
    }
    if (source._end_prl_spacing) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEndPrlSpacing;
      rule.end_prl_space = toDatabaseUnits(source._end_prl_spacing->_end_prl_space, units, "LEF58 SPACING ENDPRLSPACING");
      rule.end_prl = toDatabaseUnits(source._end_prl_spacing->_end_prl, units, "LEF58 SPACING ENDPRLSPACING PRL");
    }
    if (source._end_to_end) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEndToEnd;
      rule.end_to_end_space = toDatabaseUnits(source._end_to_end->_end_to_end_space, units, "LEF58 SPACING ENDTOEND");
      if (source._end_to_end->_one_cut_space) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasOneCutSpace;
        rule.one_cut_space = toDatabaseUnits(*source._end_to_end->_one_cut_space, units, "LEF58 SPACING ENDTOEND one-cut");
      }
      if (source._end_to_end->_two_cut_space) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasTwoCutSpace;
        rule.two_cut_space = toDatabaseUnits(*source._end_to_end->_two_cut_space, units, "LEF58 SPACING ENDTOEND two-cut");
      }
      if (source._end_to_end->_extension) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasExtension;
        rule.extension = toDatabaseUnits(*source._end_to_end->_extension, units, "LEF58 SPACING ENDTOEND EXTENSION");
      }
      if (source._end_to_end->_wrong_dir_extension) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasWrongDirExtension;
        rule.wrong_dir_extension
            = toDatabaseUnits(*source._end_to_end->_wrong_dir_extension, units, "LEF58 SPACING ENDTOEND wrong-dir extension");
      }
      if (source._end_to_end->_other_end_width) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasOtherEndWidth;
        rule.other_end_width = toDatabaseUnits(*source._end_to_end->_other_end_width, units, "LEF58 SPACING OTHERENDWIDTH");
      }
    }
    if (source._max_length || source._min_length) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasAdjacentEdgeLength;
      if (source._max_length) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasAdjacentMaxLength;
        rule.adjacent_max_length = toDatabaseUnits(*source._max_length, units, "LEF58 SPACING MAXLENGTH");
      }
      if (source._min_length) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasAdjacentMinLength;
        rule.adjacent_min_length = toSignedDatabaseUnits(*source._min_length, units, "LEF58 SPACING MINLENGTH");
      }
    }
    if (!source._two_sides.empty())
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kTwoSides;
    if (!source._equal_rect_width.empty())
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kEqualRectWidth;
    if (source._parallel_edge) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasParallelEdge;
      if (!source._parallel_edge->_subtract_eol_width.empty())
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kSubtractEolWidth;
      rule.parallel_space = toDatabaseUnits(source._parallel_edge->_par_space, units, "LEF58 SPACING PARALLELEDGE");
      rule.parallel_within = toDatabaseUnits(source._parallel_edge->_par_within, units, "LEF58 SPACING PARALLELEDGE WITHIN");
      if (source._parallel_edge->_prl) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasParallelRunLength;
        rule.parallel_run_length = toSignedDatabaseUnits(*source._parallel_edge->_prl, units, "LEF58 SPACING PARALLELEDGE PRL");
      }
      if (source._parallel_edge->_min_length) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasParallelMinLength;
        rule.parallel_min_length
            = toSignedDatabaseUnits(*source._parallel_edge->_min_length, units, "LEF58 SPACING PARALLELEDGE MINLENGTH");
      }
      if (!source._parallel_edge->_two_edgs.empty())
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kTwoEdges;
      if (!source._parallel_edge->_same_metal.empty())
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kSameMetal;
      if (!source._parallel_edge->_non_eol_corner_only.empty())
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kNonEolCornerOnly;
      if (!source._parallel_edge->_parallel_same_mask.empty())
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kParallelSameMask;
    }
    if (source._enclose_cut) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEncloseCut;
      rule.enclose_cut_side = cutSide(source._enclose_cut->_direction);
      rule.enclose_distance = toDatabaseUnits(source._enclose_cut->_enclose_dist, units, "LEF58 SPACING ENCLOSECUT");
      rule.cut_to_metal_spacing = toDatabaseUnits(source._enclose_cut->_cut_to_metal_space, units, "LEF58 SPACING ENCLOSECUT CUTSPACING");
      if (!source._enclose_cut->_all_cuts.empty())
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kAllCuts;
    }
    if (source._toconcavecorner) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasToConcaveCorner;
      if (source._toconcavecorner->_min_length) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasToConcaveCornerMinLength;
        rule.to_concave_corner_min_length
            = toDatabaseUnits(*source._toconcavecorner->_min_length, units, "LEF58 SPACING TOCONCAVECORNER MINLENGTH");
      }
      if (source._toconcavecorner->_min_adj_length1) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasToConcaveCornerMinAdjacentLength;
        rule.to_concave_corner_min_adjacent_length1
            = toDatabaseUnits(*source._toconcavecorner->_min_adj_length1, units, "LEF58 SPACING TOCONCAVECORNER MINADJACENTLENGTH");
      }
      if (source._toconcavecorner->_min_adj_length2) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasToConcaveCornerTwoMinAdjacentLengths;
        rule.to_concave_corner_min_adjacent_length2
            = toDatabaseUnits(*source._toconcavecorner->_min_adj_length2, units, "LEF58 SPACING TOCONCAVECORNER second MINADJACENTLENGTH");
      }
    }
    if (source._notch_length) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasToNotchLength;
      rule.notch_length = toDatabaseUnits(*source._notch_length, units, "LEF58 SPACING TONOTCHLENGTH");
    }
    result.spacing_eol_rules.push_back(std::move(rule));
  }
}

void prepareRoutingSpacingTable(const TechProperty& property, int32_t units, PreparedRoutingLef58Properties& result)
{
  const auto value = upper(property.value);
  if (value.find("JOGTOJOGSPACING") != std::string::npos) {
    routinglayer_property::lef58_spacingtable_jogtojog source;
    requireParsed(property, routinglayer_property::parse_lef58_spacingtable_jogtojog<std::string::const_iterator>, source);
    TechRoutingLef58SpacingTableJogToJogRule rule{
        .jog_to_jog_spacing = toDatabaseUnits(source._jog2jog_spacing, units, "LEF58 JOGTOJOGSPACING"),
        .jog_width = toDatabaseUnits(source._jog_width, units, "LEF58 JOGWIDTH"),
        .short_jog_spacing = toDatabaseUnits(source._short_jog_spacing, units, "LEF58 SHORTJOGSPACING")};
    for (const auto& width : source._width) {
      rule.widths.push_back(
          TechRoutingLef58JogToJogWidth{.width = toDatabaseUnits(width._width, units, "LEF58 JOGTOJOG WIDTH"),
                                        .parallel_length = toDatabaseUnits(width._par_length, units, "LEF58 JOGTOJOG PARALLEL"),
                                        .parallel_within = toDatabaseUnits(width._par_within, units, "LEF58 JOGTOJOG WITHIN"),
                                        .long_jog_spacing = toDatabaseUnits(width._long_jog_spacing, units, "LEF58 LONGJOGSPACING")});
    }
    result.jog_to_jog_rules.push_back(std::move(rule));
    return;
  }
  if (value.find("PARALLELRUNLENGTH") != std::string::npos) {
    routinglayer_property::lef58_spacingtable_prl source;
    requireParsed(property, routinglayer_property::parse_lef58_spacingtable_prl<std::string::const_iterator>, source);
    TechRoutingPrlSpacingTableRule rule;
    if (!source._wrong_direction.empty())
      rule.flags |= TechRoutingPrlSpacingTableRuleFlag::kWrongDirection;
    if (!source._same_mask.empty())
      rule.flags |= TechRoutingPrlSpacingTableRuleFlag::kSameMask;
    if (source._except_eol_width) {
      rule.flags |= TechRoutingPrlSpacingTableRuleFlag::kExceptEol | TechRoutingPrlSpacingTableRuleFlag::kHasEolWidth;
      rule.eol_width = toDatabaseUnits(*source._except_eol_width, units, "LEF58 SPACINGTABLE EXCEPTEOL");
    }
    for (double length : source._parallel_run_lengths) {
      rule.parallel_run_lengths.push_back(toDatabaseUnits(length, units, "LEF58 SPACINGTABLE PARALLELRUNLENGTH"));
    }
    for (uint32_t index = 0; index < source._widths.size(); ++index) {
      const auto& width = source._widths[index];
      rule.widths.push_back(toDatabaseUnits(width._width, units, "LEF58 SPACINGTABLE WIDTH"));
      if (width._except_within) {
        rule.except_withins.push_back(TechRoutingPrlSpacingTableExceptWithin{
            .width_index = index,
            .low = toDatabaseUnits(width._except_within->first, units, "LEF58 SPACINGTABLE EXCEPTWITHIN low"),
            .high = toDatabaseUnits(width._except_within->second, units, "LEF58 SPACINGTABLE EXCEPTWITHIN high")});
      }
      for (double spacing : width._spacings) {
        rule.cells.push_back(toDatabaseUnits(spacing, units, "LEF58 SPACINGTABLE spacing"));
      }
    }
    if (source._influences) {
      for (const auto& influence : *source._influences) {
        rule.influences.push_back(TechRoutingPrlSpacingTableInfluence{
            .width = toDatabaseUnits(influence._width, units, "LEF58 SPACINGTABLE INFLUENCE WIDTH"),
            .within = toDatabaseUnits(influence._within, units, "LEF58 SPACINGTABLE INFLUENCE WITHIN"),
            .spacing = toDatabaseUnits(influence._spacing, units, "LEF58 SPACINGTABLE INFLUENCE SPACING")});
      }
    }
    result.prl_spacing_tables.push_back(std::move(rule));
    return;
  }
  throw std::runtime_error("unsupported LEF58_SPACINGTABLE value");
}

void prepareCutClass(const TechProperty& property, int32_t units, PreparedCutLef58Properties& result)
{
  std::vector<cutlayer_property::lef58_cutclass> parsed;
  requireParsed(property, cutlayer_property::parse_lef58_cutclass<std::string::const_iterator>, parsed);
  for (const auto& source : parsed) {
    TechCutLef58CutClassRule rule{.name = source._classname,
                                  .via_width = toDatabaseUnits(source._via_width, units, "LEF58 CUTCLASS WIDTH"),
                                  .orient = cutDirection(source._orient)};
    if (source._via_length) {
      rule.flags |= TechCutLef58CutClassRuleFlag::kHasViaLength;
      rule.via_length = toDatabaseUnits(*source._via_length, units, "LEF58 CUTCLASS LENGTH");
    }
    if (source._num_cut) {
      if (*source._num_cut <= 0)
        throw std::runtime_error("LEF58 CUTCLASS CUTS must be positive");
      rule.flags |= TechCutLef58CutClassRuleFlag::kHasNumCut;
      rule.num_cut = static_cast<uint32_t>(*source._num_cut);
    }
    result.cut_class_rules.push_back(std::move(rule));
  }
}

void prepareCutEnclosure(const TechProperty& property, int32_t units, PreparedCutLef58Properties& result)
{
  std::vector<cutlayer_property::lef58_enclosure> parsed;
  requireParsed(property, cutlayer_property::parse_lef58_enclosure<std::string::const_iterator>, parsed);
  for (const auto& source : parsed) {
    TechCutLef58EnclosureRule rule{.cutclass_name = source._classname, .side = cutSide(source._direction)};
    if (source._overhang1) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasOverhang1;
      rule.overhang1 = toDatabaseUnits(*source._overhang1, units, "LEF58 ENCLOSURE overhang1");
    }
    if (source._overhang2) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasOverhang2;
      rule.overhang2 = toDatabaseUnits(*source._overhang2, units, "LEF58 ENCLOSURE overhang2");
    }
    if (source._end_overhang1) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasEndOverhang1;
      rule.end_overhang1 = toDatabaseUnits(*source._end_overhang1, units, "LEF58 ENCLOSURE END");
    }
    if (source._side_overhang2) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasSideOverhang2;
      rule.side_overhang2 = toDatabaseUnits(*source._side_overhang2, units, "LEF58 ENCLOSURE SIDE");
    }
    if (source._min_width) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasMinWidth;
      rule.min_width = toDatabaseUnits(*source._min_width, units, "LEF58 ENCLOSURE WIDTH");
    }
    if (!source._include_abutted.empty()) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kIncludeAbutted;
    }
    if (source._cut_winthin) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kExceptExtraCut | TechCutLef58EnclosureRuleFlag::kHasCutWithin;
      rule.cut_within = toDatabaseUnits(*source._cut_winthin, units, "LEF58 ENCLOSURE EXCEPTEXTRACUT");
      if (source._except_extracut_type == "PRL") {
        rule.flags |= TechCutLef58EnclosureRuleFlag::kPrl;
      } else if (source._except_extracut_type == "NOSHAREDEDGE") {
        rule.flags |= TechCutLef58EnclosureRuleFlag::kNoSharedEdge;
      }
    } else if (!source._except_extracut_type.empty()) {
      throw std::runtime_error("LEF58 ENCLOSURE PRL/NOSHAREDEDGE requires EXCEPTEXTRACUT");
    }
    if (source._min_length) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasMinLength;
      rule.min_length = toDatabaseUnits(*source._min_length, units, "LEF58 ENCLOSURE LENGTH");
    }
    if (source._cut_within) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasRedundantCut;
      rule.redundant_cut_within = toDatabaseUnits(*source._cut_within, units, "LEF58 ENCLOSURE REDUNDANTCUT");
    }
    result.enclosure_rules.push_back(std::move(rule));
  }
}

void prepareCutEnclosureEdge(const TechProperty& property, int32_t units, PreparedCutLef58Properties& result)
{
  std::vector<cutlayer_property::lef58_enclosureedge> parsed;
  requireParsed(property, cutlayer_property::parse_lef58_enclosureedge<std::string::const_iterator>, parsed);
  for (const auto& source : parsed) {
    TechCutLef58EnclosureEdgeRule rule{.cutclass_name = source._classname,
                                       .side = cutSide(source._direction),
                                       .overhang = toDatabaseUnits(source._overhang, units, "LEF58 ENCLOSUREEDGE overhang")};
    if (source._width_convex.which() == 0) {
      const auto& width = boost::get<cutlayer_property::lef58_enclosureedge_width>(source._width_convex);
      rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasMinWidth | TechCutLef58EnclosureEdgeRuleFlag::kHasParLength
                    | TechCutLef58EnclosureEdgeRuleFlag::kHasParWithin;
      rule.min_width = toDatabaseUnits(width._min_width, units, "LEF58 ENCLOSUREEDGE WIDTH");
      rule.par_length = toDatabaseUnits(width._par_length, units, "LEF58 ENCLOSUREEDGE PARALLEL");
      rule.par_within = toDatabaseUnits(width._par_within, units, "LEF58 ENCLOSUREEDGE WITHIN");
      if (!width._except_extracut.empty())
        rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kExceptExtraCut;
      if (width._cut_within) {
        rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasCutWithin;
        rule.cut_within = toDatabaseUnits(*width._cut_within, units, "LEF58 ENCLOSUREEDGE EXCEPTEXTRACUT");
      }
      if (!width._except_two_edges.empty())
        rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kExceptTwoEdges;
      if (width._except_within) {
        rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasExceptWithin;
        rule.except_within = toDatabaseUnits(*width._except_within, units, "LEF58 ENCLOSUREEDGE EXCEPTWITHIN");
      }
    } else {
      const auto& convex = boost::get<cutlayer_property::lef58_enclosureedge_convexcorners>(source._width_convex);
      rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasConvexCorners;
      rule.convex_length = toDatabaseUnits(convex._convex_length, units, "LEF58 ENCLOSUREEDGE CONVEXCORNERS");
      rule.adjacent_length = toDatabaseUnits(convex._adjacent_length, units, "LEF58 ENCLOSUREEDGE adjacent length");
      rule.convex_par_within = toDatabaseUnits(convex._par_within, units, "LEF58 ENCLOSUREEDGE PARALLEL");
      rule.convex_corner_length = toDatabaseUnits(convex._length, units, "LEF58 ENCLOSUREEDGE LENGTH");
    }
    result.enclosure_edge_rules.push_back(std::move(rule));
  }
}

TechCutLef58EolEnclosureRule prepareCutEolEnclosure(const TechProperty& property, int32_t units)
{
  cutlayer_property::lef58_eolenclosure source;
  requireParsed(property, cutlayer_property::parse_lef58_eolenclosure<std::string::const_iterator>, source);
  TechCutLef58EolEnclosureRule rule{.eol_width = toDatabaseUnits(source._eol_width, units, "LEF58 EOLENCLOSURE width"),
                                    .edge_direction = cutDirection(source._edge_direction),
                                    .cutclass_name = source._classname,
                                    .side = cutSide(source._direction)};
  if (source._min_eol_width) {
    rule.flags |= TechCutLef58EolEnclosureRuleFlag::kHasMinEolWidth;
    rule.min_eol_width = toDatabaseUnits(*source._min_eol_width, units, "LEF58 EOLENCLOSURE MINEOLWIDTH");
  }
  if (!source._equalrectwidth.empty())
    rule.flags |= TechCutLef58EolEnclosureRuleFlag::kEqualRectWidth;
  if (source._overhang.which() == 0) {
    const auto& overhang = boost::get<cutlayer_property::lef58_eolenclosure_edgeoverhang>(source._overhang);
    rule.application = overhang._applied_to == "LONGEDGEONLY" ? Lef58EolEnclosureApplication::kLongEdgeOnly
                                                              : Lef58EolEnclosureApplication::kShortEdgeOnly;
    rule.overhang = toDatabaseUnits(overhang._overhang, units, "LEF58 EOLENCLOSURE edge overhang");
  } else {
    const auto& overhang = boost::get<cutlayer_property::lef58_eolenclosure_overhang>(source._overhang);
    rule.overhang = toDatabaseUnits(overhang._overhang, units, "LEF58 EOLENCLOSURE overhang");
    if (overhang._extract_overhang) {
      rule.flags |= TechCutLef58EolEnclosureRuleFlag::kHasExtractOverhang;
      rule.extract_overhang = toDatabaseUnits(*overhang._extract_overhang, units, "LEF58 EOLENCLOSURE extract overhang");
    }
    if (overhang._par_space) {
      rule.flags |= TechCutLef58EolEnclosureRuleFlag::kHasParallelSpace;
      rule.parallel_space = toDatabaseUnits(*overhang._par_space, units, "LEF58 EOLENCLOSURE PARALLELEDGE");
    }
    if (overhang._backward_ext || overhang._forward_ext) {
      if (!overhang._backward_ext || !overhang._forward_ext) {
        throw std::runtime_error("LEF58 EOLENCLOSURE EXTENSION requires two values");
      }
      rule.flags |= TechCutLef58EolEnclosureRuleFlag::kHasExtension;
      rule.backward_ext = toDatabaseUnits(*overhang._backward_ext, units, "LEF58 EOLENCLOSURE backward EXTENSION");
      rule.forward_ext = toDatabaseUnits(*overhang._forward_ext, units, "LEF58 EOLENCLOSURE forward EXTENSION");
    }
    if (overhang._min_length) {
      rule.flags |= TechCutLef58EolEnclosureRuleFlag::kHasMinLength;
      rule.min_length = toDatabaseUnits(*overhang._min_length, units, "LEF58 EOLENCLOSURE MINLENGTH");
    }
    if (!overhang._allsides.empty())
      rule.flags |= TechCutLef58EolEnclosureRuleFlag::kAllSides;
  }
  return rule;
}

TechCutLef58EolSpacingRule prepareCutEolSpacing(const TechProperty& property, int32_t units)
{
  cutlayer_property::lef58_eolspacing source;
  requireParsed(property, cutlayer_property::parse_lef58_eolspacing<std::string::const_iterator>, source);
  TechCutLef58EolSpacingRule rule{
      .cutclass_name = source._classname1,
      .cut_spacing1 = toDatabaseUnits(source._cut_spacing1, units, "LEF58 EOLSPACING spacing1"),
      .cut_spacing2 = toDatabaseUnits(source._cut_spacing2, units, "LEF58 EOLSPACING spacing2"),
      .eol_width = toDatabaseUnits(source._eol_width, units, "LEF58 EOLSPACING ENDWIDTH"),
      .prl = toDatabaseUnits(source._prl, units, "LEF58 EOLSPACING PRL"),
      .smaller_overhang = toDatabaseUnits(source._smaller_overhang, units, "LEF58 EOLSPACING smaller ENCLOSURE"),
      .equal_overhang = toDatabaseUnits(source._equal_overhang, units, "LEF58 EOLSPACING equal ENCLOSURE"),
      .side_ext = toDatabaseUnits(source._side_ext, units, "LEF58 EOLSPACING side EXTENSION"),
      .backward_ext = toDatabaseUnits(source._backward_ext, units, "LEF58 EOLSPACING backward EXTENSION"),
      .span_length = toDatabaseUnits(source._span_length, units, "LEF58 EOLSPACING SPANLENGTH")};
  for (const auto& to_class : source._to_classes) {
    rule.to_classes.push_back(
        TechCutLef58EolSpacingToClass{.cutclass_name = to_class._classname,
                                      .cut_spacing1 = toDatabaseUnits(to_class._cut_spacing1, units, "LEF58 EOLSPACING TO spacing1"),
                                      .cut_spacing2 = toDatabaseUnits(to_class._cut_spacing2, units, "LEF58 EOLSPACING TO spacing2")});
  }
  return rule;
}

TechCutLef58SpacingTableRule prepareCutSpacingTable(const TechProperty& property, int32_t units)
{
  cutlayer_property::lef58_spacingtable source;
  requireParsed(property, cutlayer_property::parse_lef58_spacingtable<std::string::const_iterator>, source);
  TechCutLef58SpacingTableRule rule;
  if (source._default_spacing) {
    rule.flags |= TechCutLef58SpacingTableRuleFlag::kHasDefault;
    rule.default_spacing = toDatabaseUnits(*source._default_spacing, units, "LEF58 CUT SPACINGTABLE DEFAULT");
  }
  if (!source._same_mask.empty()) rule.flags |= TechCutLef58SpacingTableRuleFlag::kSameMask;
  if (source._same_kind == "SAMENET") rule.flags |= TechCutLef58SpacingTableRuleFlag::kSameNet;
  if (source._same_kind == "SAMEMETAL") rule.flags |= TechCutLef58SpacingTableRuleFlag::kSameMetal;
  if (source._same_kind == "SAMEVIA") rule.flags |= TechCutLef58SpacingTableRuleFlag::kSameVia;
  if (source._layer) {
    rule.flags |= TechCutLef58SpacingTableRuleFlag::kHasSecondLayer;
    rule.second_layer_name = source._layer->_second_layername;
    if (!source._layer->_nostack.empty()) rule.flags |= TechCutLef58SpacingTableRuleFlag::kNoStack;
    if (!source._layer->_prl_for_aligned_cut.empty()) rule.flags |= TechCutLef58SpacingTableRuleFlag::kPrlForAlignedCut;
    for (const auto& pair : source._layer->_prl_for_aligned_cut)
      rule.prl_for_aligned_cut.push_back({.from = pair._from, .to = pair._to});
  }
  if (source._prl) {
    rule.flags |= TechCutLef58SpacingTableRuleFlag::kHasPrl;
    rule.prl = toDatabaseUnits(source._prl->_prl, units, "LEF58 CUT SPACINGTABLE PRL");
    rule.prl_direction = cutDirection(source._prl->_direction);
    if (!source._prl->_maxxy.empty())
      rule.flags |= TechCutLef58SpacingTableRuleFlag::kMaxXY;
    for (const auto& entry : source._prl->_entries) {
      rule.prl_entries.push_back({.from = entry._from,
                                  .to = entry._to,
                                  .prl = toDatabaseUnits(entry._prl, units, "LEF58 CUT SPACINGTABLE PRL entry")});
    }
  }
  for (const auto& name : source._cutclass._classname1) {
    rule.cutclass1_names.push_back(name._classname);
    rule.cutclass1_edges.push_back(cutClassEdge(name._edge));
  }
  for (const auto& row : source._cutclass._cuts) {
    rule.cutclass2_names.push_back(row._classname2._classname);
    rule.cutclass2_edges.push_back(cutClassEdge(row._classname2._edge));
    for (const auto& source_cell : row._cutspacings) {
      TechCutLef58SpacingTableCell cell;
      if (source_cell._cut1) {
        cell.has_cut_spacing1 = true;
        cell.cut_spacing1 = toDatabaseUnits(*source_cell._cut1, units, "LEF58 CUT SPACINGTABLE spacing1");
      }
      if (source_cell._cut2) {
        cell.has_cut_spacing2 = true;
        cell.cut_spacing2 = toDatabaseUnits(*source_cell._cut2, units, "LEF58 CUT SPACINGTABLE spacing2");
      }
      rule.cells.push_back(cell);
    }
  }
  return rule;
}

TechCutOrthogonalSpacingTableRule prepareCutOrthogonalSpacingTable(const TechProperty& property, int32_t units)
{
  std::vector<cutlayer_property::lef58_spacingtable_orthogonal_item> parsed;
  requireParsed(property, cutlayer_property::parse_lef58_spacingtable_orthogonal<std::string::const_iterator>, parsed);
  TechCutOrthogonalSpacingTableRule rule{.flags = TechCutOrthogonalSpacingTableRuleFlag::kLef58Property};
  rule.items.reserve(parsed.size());
  for (const auto& item : parsed) {
    rule.items.push_back(TechCutOrthogonalSpacingTableItem{
        .within = toDatabaseUnits(item._within, units, "LEF58 SPACINGTABLE ORTHOGONAL WITHIN"),
        .spacing = toDatabaseUnits(item._spacing, units, "LEF58 SPACINGTABLE ORTHOGONAL SPACING")});
  }
  return rule;
}

std::vector<TechProperty> splitPropertyRules(const TechProperty& property)
{
  std::vector<TechProperty> rules;
  size_t begin = 0;
  while (begin < property.value.size()) {
    const auto end = property.value.find(';', begin);
    if (end == std::string::npos) {
      if (property.value.find_first_not_of(" \t\r\n", begin) != std::string::npos) {
        throw std::runtime_error("unterminated " + property.name + " rule");
      }
      break;
    }
    if (property.value.find_first_not_of(" \t\r\n", begin) < end) {
      rules.push_back(TechProperty{.name = property.name, .value = property.value.substr(begin, end - begin + 1)});
    }
    begin = end + 1;
  }
  return rules;
}

}  // namespace

PreparedRoutingLef58Properties prepareRoutingLef58Properties(const std::vector<TechProperty>& properties, int32_t units)
{
  PreparedRoutingLef58Properties result;
  for (const auto& property : properties) {
    const auto name = upper(property.name);
    if (name == "LEF58_RECTONLY") {
      bool except_non_core_pins = false;
      if (!layer_property::parse_lef58_rectonly(property.value.begin(), property.value.end(), except_non_core_pins)) {
        throw std::runtime_error("failed to parse LEF58_RECTONLY property");
      }
      result.layer_flags |= TechRoutingLayerFlag::kLef58RectOnly;
      if (except_non_core_pins)
        result.layer_flags |= TechRoutingLayerFlag::kLef58RectOnlyExceptNonCorePins;
    } else if (name == "LEF58_RIGHTWAYONGRIDONLY") {
      bool check_mask = false;
      if (!layer_property::parse_lef58_rightwayongridonly(property.value.begin(), property.value.end(), check_mask)) {
        throw std::runtime_error("failed to parse LEF58_RIGHTWAYONGRIDONLY property");
      }
      result.layer_flags |= TechRoutingLayerFlag::kLef58RightWayOnGridOnly;
      if (check_mask)
        result.layer_flags |= TechRoutingLayerFlag::kLef58RightWayOnGridOnlyCheckMask;
    } else if (name == "LEF58_AREA") {
      prepareRoutingArea(property, units, result);
    } else if (name == "LEF58_CORNERFILLSPACING") {
      routinglayer_property::lef58_cornerfillspacing source;
      requireParsed(property, routinglayer_property::parse_lef58_conerfillspacing<std::string::const_iterator>, source);
      result.corner_fill_spacing_rules.push_back(TechRoutingLef58CornerFillSpacingRule{
          .spacing = toDatabaseUnits(source._spacing, units, "LEF58 CORNERFILLSPACING"),
          .edge_length1 = toDatabaseUnits(source._length1, units, "LEF58 CORNERFILLSPACING EDGELENGTH"),
          .edge_length2 = toDatabaseUnits(source._length2, units, "LEF58 CORNERFILLSPACING EDGELENGTH"),
          .eol_width = toDatabaseUnits(source._eol_width, units, "LEF58 CORNERFILLSPACING ADJACENTEOL")});
    } else if (name == "LEF58_CORNERSPACING") {
      std::vector<routinglayer_property::lef58_cornerspacing> parsed;
      requireParsed(property, routinglayer_property::parse_lef58_cornerspacing<std::string::const_iterator>, parsed);
      for (const auto& source : parsed) {
        TechRoutingLef58CornerSpacingRule rule;
        rule.type = source._corner_type == "CONCAVECORNER" ? TechRoutingLef58CornerType::kConcave
                                                            : TechRoutingLef58CornerType::kConvex;
        if (source._except_eol) {
          rule.flags |= TechRoutingLef58CornerSpacingRuleFlag::kHasExceptEol;
          rule.except_eol = toDatabaseUnits(*source._except_eol, units, "LEF58 CORNERSPACING EXCEPTEOL");
        }
        if (!source._corner_to_corner.empty()) {
          rule.flags |= TechRoutingLef58CornerSpacingRuleFlag::kCornerToCorner;
        }
        rule.width_spacings.reserve(source._width_spacings.size());
        for (const auto& item : source._width_spacings) {
          rule.width_spacings.push_back(
              {.width = toDatabaseUnits(item._width, units, "LEF58 CORNERSPACING WIDTH"),
               .spacing = toDatabaseUnits(item._spacing, units, "LEF58 CORNERSPACING SPACING")});
        }
        result.corner_spacing_rules.push_back(std::move(rule));
      }
    } else if (name == "LEF58_MINIMUMCUT") {
      prepareRoutingMinimumCut(property, units, result);
    } else if (name == "LEF58_MINSTEP") {
      prepareRoutingMinStep(property, units, result);
    } else if (name == "LEF58_WIDTHTABLE") {
      std::vector<routinglayer_property::lef58_widthtable> parsed;
      requireParsed(property, routinglayer_property::parse_lef58_widthtable<std::string::const_iterator>, parsed);
      for (const auto& source : parsed) {
        TechRoutingLef58WidthTableRule rule;
        if (!source._wrong_direction.empty())
          rule.flags |= TechRoutingLef58WidthTableRuleFlag::kWrongDirection;
        if (!source._orthogonal.empty())
          rule.flags |= TechRoutingLef58WidthTableRuleFlag::kOrthogonal;
        for (double width : source._widths)
          rule.widths.push_back(toDatabaseUnits(width, units, "LEF58 WIDTHTABLE WIDTH"));
        result.width_table_rules.push_back(std::move(rule));
      }
    } else if (name == "LEF58_SPACING") {
      const auto value = upper(property.value);
      if (value.find("ENDOFLINE") != std::string::npos) {
        prepareRoutingSpacingEol(property, units, result);
      } else if (value.find("NOTCHLENGTH") != std::string::npos) {
        routinglayer_property::lef58_spacing_notchlength source;
        requireParsed(property, routinglayer_property::parse_lef58_spacing_notchlength<std::string::const_iterator>, source);
        TechRoutingLef58SpacingNotchLengthRule rule{
            .min_spacing = toDatabaseUnits(source._min_spacing, units, "LEF58 SPACING NOTCHLENGTH spacing"),
            .min_notch_length = toDatabaseUnits(source._min_notch_length, units, "LEF58 SPACING NOTCHLENGTH")};
        if (source._side_type == "CONCAVEENDS" && source._side_of_notch_width) {
          rule.flags |= TechRoutingLef58SpacingNotchLengthRuleFlag::kHasConcaveEndsSideOfNotchWidth;
          rule.concave_ends_side_of_notch_width = toDatabaseUnits(*source._side_of_notch_width, units, "LEF58 SPACING CONCAVEENDS");
        }
        result.spacing_notch_length_rules.push_back(rule);
      }
    } else if (name == "LEF58_SPACINGTABLE") {
      prepareRoutingSpacingTable(property, units, result);
    }
  }
  return result;
}

PreparedCutLef58Properties prepareCutLef58Properties(const std::vector<TechProperty>& properties, int32_t units)
{
  PreparedCutLef58Properties result;
  for (const auto& property : properties) {
    const auto name = upper(property.name);
    if (name == "LEF58_CUTCLASS") {
      prepareCutClass(property, units, result);
    } else if (name == "LEF58_ENCLOSURE") {
      prepareCutEnclosure(property, units, result);
    } else if (name == "LEF58_ENCLOSUREEDGE") {
      prepareCutEnclosureEdge(property, units, result);
    } else if (name == "LEF58_EOLENCLOSURE") {
      if (result.eol_enclosure_rule)
        throw std::runtime_error("duplicate LEF58_EOLENCLOSURE property");
      result.eol_enclosure_rule = prepareCutEolEnclosure(property, units);
    } else if (name == "LEF58_EOLSPACING") {
      if (result.eol_spacing_rule)
        throw std::runtime_error("duplicate LEF58_EOLSPACING property");
      result.eol_spacing_rule = prepareCutEolSpacing(property, units);
    } else if (name == "LEF58_SPACINGTABLE") {
      for (const auto& rule_property : splitPropertyRules(property)) {
        if (upper(rule_property.value).find("SPACINGTABLE ORTHOGONAL") != std::string::npos) {
          result.orthogonal_spacing_table_rules.push_back(prepareCutOrthogonalSpacingTable(rule_property, units));
        } else {
          result.spacing_table_rules.push_back(prepareCutSpacingTable(rule_property, units));
        }
      }
    }
  }
  return result;
}

std::optional<PreparedTrimmedMetalRule> prepareMastersliceLef58Properties(const std::vector<TechProperty>& properties)
{
  namespace qi = boost::spirit::qi;
  namespace ascii = boost::spirit::ascii;
  std::optional<PreparedTrimmedMetalRule> result;
  for (const auto& property : properties) {
    if (upper(property.name) != "LEF58_TRIMMEDMETAL")
      continue;
    if (result)
      throw std::runtime_error("duplicate LEF58_TRIMMEDMETAL property");
    PreparedTrimmedMetalRule rule;
    auto first = property.value.begin();
    const auto last = property.value.end();
    using Iterator = std::string::const_iterator;
    qi::rule<Iterator, std::string(), ascii::space_type> token = qi::lexeme[+(qi::char_ - qi::char_(" ;\n\t"))];
    std::vector<std::string> tokens;
    const bool parsed = qi::phrase_parse(first, last, +token >> qi::lit(';'), ascii::space, tokens);
    if (!parsed || first != last || (tokens.size() != 2u && tokens.size() != 4u) || tokens[0] != "TRIMMEDMETAL") {
      throw std::runtime_error("failed to parse LEF58_TRIMMEDMETAL property");
    }
    rule.routing_layer = std::move(tokens[1]);
    if (tokens.size() == 4u) {
      uint32_t mask = 0;
      const auto [end, error] = std::from_chars(tokens[3].data(), tokens[3].data() + tokens[3].size(), mask);
      if (tokens[2] != "MASK" || error != std::errc{} || end != tokens[3].data() + tokens[3].size() || mask == 0u) {
        throw std::runtime_error("failed to parse LEF58_TRIMMEDMETAL MASK");
      }
      rule.flags |= TechTrimmedMetalRuleFlag::kHasMask;
      rule.mask = mask;
    }
    result = std::move(rule);
  }
  return result;
}

void commitRoutingLef58Properties(TechRoutingLayerStorage& storage, TechRoutingLayerId owner,
                                  const PreparedRoutingLef58Properties& properties)
{
  storage.routingLayer(owner).flags |= properties.layer_flags;
  for (const auto& rule : properties.area_rules)
    static_cast<void>(storage.addLef58AreaRule(owner, rule));
  for (const auto& rule : properties.corner_fill_spacing_rules)
    static_cast<void>(storage.addLef58CornerFillSpacingRule(owner, rule));
  for (const auto& rule : properties.corner_spacing_rules)
    static_cast<void>(storage.addLef58CornerSpacingRule(owner, rule));
  for (const auto& rule : properties.minimum_cut_rules)
    static_cast<void>(storage.addLef58MinimumCutRule(owner, rule));
  for (const auto& rule : properties.min_step_rules)
    static_cast<void>(storage.addLef58MinStepRule(owner, rule));
  for (const auto& rule : properties.width_table_rules)
    static_cast<void>(storage.addLef58WidthTableRule(owner, rule));
  for (const auto& rule : properties.spacing_eol_rules)
    static_cast<void>(storage.addLef58SpacingEolRule(owner, rule));
  for (const auto& rule : properties.spacing_notch_length_rules)
    static_cast<void>(storage.addLef58SpacingNotchLengthRule(owner, rule));
  for (const auto& rule : properties.jog_to_jog_rules)
    static_cast<void>(storage.addLef58SpacingTableJogToJogRule(owner, rule));
  for (const auto& rule : properties.prl_spacing_tables)
    static_cast<void>(storage.addPrlSpacingTableRule(owner, rule));
}

void commitCutLef58Properties(TechCutLayerStorage& storage, TechCutLayerId owner, const PreparedCutLef58Properties& properties)
{
  for (const auto& rule : properties.cut_class_rules)
    static_cast<void>(storage.addLef58CutClassRule(owner, rule));
  for (const auto& rule : properties.enclosure_rules)
    static_cast<void>(storage.addLef58EnclosureRule(owner, rule));
  for (const auto& rule : properties.enclosure_edge_rules)
    static_cast<void>(storage.addLef58EnclosureEdgeRule(owner, rule));
  if (properties.eol_enclosure_rule)
    static_cast<void>(storage.setLef58EolEnclosureRule(owner, *properties.eol_enclosure_rule));
  if (properties.eol_spacing_rule)
    static_cast<void>(storage.setLef58EolSpacingRule(owner, *properties.eol_spacing_rule));
  for (const auto& rule : properties.spacing_table_rules)
    static_cast<void>(storage.addLef58SpacingTableRule(owner, rule));
  for (const auto& rule : properties.orthogonal_spacing_table_rules)
    static_cast<void>(storage.addOrthogonalSpacingTableRule(owner, rule));
}

void commitTrimmedMetalRule(TechStore& database, TechMastersliceLayerId owner, const PreparedTrimmedMetalRule& source)
{
  const auto layer = database.findLayer(source.routing_layer);
  if (!layer || !database.techRegistry().registry().all_of<TechRoutingLayer>(layer.entity())) {
    throw std::runtime_error("LEF58_TRIMMEDMETAL references a missing or non-ROUTING layer: " + source.routing_layer);
  }
  database.mastersliceLayerStorage().setTrimmedMetalRule(
      owner, TechTrimmedMetalRule{.flags = source.flags, .metal_layer = TechRoutingLayerId{layer.entity()}, .mask = source.mask});
}

}  // namespace eccdb::lef_detail
