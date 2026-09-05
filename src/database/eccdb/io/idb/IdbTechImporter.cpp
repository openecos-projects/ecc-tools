#include "idb/IdbTechImporter.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "IdbLayout.h"

namespace eccdb {
namespace {

std::string upper(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  return value;
}

Rect rect(::idb::IdbRect& source)
{
  return Rect{.ll_x = source.get_low_x(), .ll_y = source.get_low_y(), .ur_x = source.get_high_x(), .ur_y = source.get_high_y()};
}

int32_t checkedCoordinate(int64_t value, const char* field)
{
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    throw std::runtime_error(std::string(field) + " is outside int32 DBU range");
  }
  return static_cast<int32_t>(value);
}

TechConductorLayerRef conductorLayer(const TechStore& database, TechLayerId layer, const char* field)
{
  const auto& registry = database.techRegistry().registry();
  if (registry.all_of<TechRoutingLayer>(layer.entity())) {
    return TechConductorLayerRef{TechRoutingLayerId{layer.entity()}};
  }
  if (registry.all_of<TechMastersliceLayer>(layer.entity())) {
    return TechConductorLayerRef{TechMastersliceLayerId{layer.entity()}};
  }
  throw std::runtime_error(std::string(field) + " requires a ROUTING or MASTERSLICE layer");
}

TechViaMasterShapeInput materializeGeneratedVia(const TechGeneratedViaMaster& source, TechConductorLayerRef bottom,
                                                TechCutLayerId cut, TechConductorLayerRef top)
{
  if (source.cut_size_x <= 0 || source.cut_size_y <= 0 || source.row_count == 0 || source.column_count == 0) {
    throw std::runtime_error("legacy generated VIA requires positive CUTSIZE and ROWCOL");
  }
  const auto total_x = static_cast<int64_t>(source.column_count) * source.cut_size_x
                       + static_cast<int64_t>(source.column_count - 1u) * source.cut_spacing_x;
  const auto total_y = static_cast<int64_t>(source.row_count) * source.cut_size_y
                       + static_cast<int64_t>(source.row_count - 1u) * source.cut_spacing_y;
  const auto dx = total_x / 2;
  const auto dy = total_y / 2;

  TechViaMasterShapeInput shapes{.bottom_layer = bottom, .cut_layer = cut, .top_layer = top};
  shapes.cut_geometry.rects.reserve(static_cast<size_t>(source.row_count) * source.column_count);
  for (uint32_t row = 0; row < source.row_count; ++row) {
    for (uint32_t column = 0; column < source.column_count; ++column) {
      const auto x = static_cast<int64_t>(column) * (source.cut_size_x + source.cut_spacing_x) - dx + source.origin_x;
      const auto y = static_cast<int64_t>(row) * (source.cut_size_y + source.cut_spacing_y) - dy + source.origin_y;
      shapes.cut_geometry.rects.push_back(
          Rect{checkedCoordinate(x, "legacy generated VIA cut X"), checkedCoordinate(y, "legacy generated VIA cut Y"),
               checkedCoordinate(x + source.cut_size_x, "legacy generated VIA cut X"),
               checkedCoordinate(y + source.cut_size_y, "legacy generated VIA cut Y")});
    }
  }

  const auto min_x = -dx + source.origin_x;
  const auto min_y = -dy + source.origin_y;
  const auto max_x = total_x - dx + source.origin_x;
  const auto max_y = total_y - dy + source.origin_y;
  shapes.bottom_geometry.rects.push_back(
      Rect{checkedCoordinate(min_x - source.bottom_enclosure_x + source.bottom_offset_x, "legacy generated VIA bottom X"),
           checkedCoordinate(min_y - source.bottom_enclosure_y + source.bottom_offset_y, "legacy generated VIA bottom Y"),
           checkedCoordinate(max_x + source.bottom_enclosure_x + source.bottom_offset_x, "legacy generated VIA bottom X"),
           checkedCoordinate(max_y + source.bottom_enclosure_y + source.bottom_offset_y, "legacy generated VIA bottom Y")});
  shapes.top_geometry.rects.push_back(
      Rect{checkedCoordinate(min_x - source.top_enclosure_x + source.top_offset_x, "legacy generated VIA top X"),
           checkedCoordinate(min_y - source.top_enclosure_y + source.top_offset_y, "legacy generated VIA top Y"),
           checkedCoordinate(max_x + source.top_enclosure_x + source.top_offset_x, "legacy generated VIA top X"),
           checkedCoordinate(max_y + source.top_enclosure_y + source.top_offset_y, "legacy generated VIA top Y")});
  return shapes;
}

TechRoutingDirection routingDirection(::idb::IdbLayerDirection direction)
{
  switch (direction) {
    case ::idb::IdbLayerDirection::kHorizontal:
      return TechRoutingDirection::kHorizontal;
    case ::idb::IdbLayerDirection::kVertical:
      return TechRoutingDirection::kVertical;
    case ::idb::IdbLayerDirection::kDiag45:
      return TechRoutingDirection::kDiag45;
    case ::idb::IdbLayerDirection::kDiag135:
      return TechRoutingDirection::kDiag135;
    case ::idb::IdbLayerDirection::kNone:
    case ::idb::IdbLayerDirection::kMax:
      return TechRoutingDirection::kUnknown;
  }
  return TechRoutingDirection::kUnknown;
}

TechRoutingAxisValueForm axisValueForm(::idb::IdbLayerOrientType type)
{
  switch (type) {
    case ::idb::IdbLayerOrientType::kBothXY:
      return TechRoutingAxisValueForm::kBothXY;
    case ::idb::IdbLayerOrientType::kSeperateXY:
      return TechRoutingAxisValueForm::kSeparateXY;
    case ::idb::IdbLayerOrientType::kNone:
    case ::idb::IdbLayerOrientType::kMax:
      return TechRoutingAxisValueForm::kNone;
  }
  return TechRoutingAxisValueForm::kNone;
}

TechMastersliceType mastersliceType(const std::string& source)
{
  const auto type = upper(source);
  if (type == "NWELL") {
    return TechMastersliceType::kNWell;
  }
  if (type == "PWELL") {
    return TechMastersliceType::kPWell;
  }
  if (type == "ABOVEDIEEDGE") {
    return TechMastersliceType::kAboveDieEdge;
  }
  if (type == "BELOWDIEEDGE") {
    return TechMastersliceType::kBelowDieEdge;
  }
  if (type == "DIFFUSION") {
    return TechMastersliceType::kDiffusion;
  }
  if (type == "TRIMPOLY") {
    return TechMastersliceType::kTrimPoly;
  }
  if (type == "TRIMMETAL") {
    return TechMastersliceType::kTrimMetal;
  }
  if (type == "REGION") {
    return TechMastersliceType::kRegion;
  }
  return TechMastersliceType::kNone;
}

TechLef58LayerType lef58LayerType(const std::string& source)
{
  const auto type = upper(source);
  if (type == "POLYROUTING") return TechLef58LayerType::kPolyRouting;
  if (type == "MIMCAP") return TechLef58LayerType::kMimCap;
  if (type == "TSV") return TechLef58LayerType::kTsv;
  if (type == "PASSIVATION") return TechLef58LayerType::kPassivation;
  if (type == "TRIMPOLY") return TechLef58LayerType::kTrimPoly;
  if (type == "NWELL") return TechLef58LayerType::kNWell;
  if (type == "PWELL") return TechLef58LayerType::kPWell;
  if (type == "BELOWDIEEDGE") return TechLef58LayerType::kBelowDieEdge;
  if (type == "ABOVEDIEEDGE") return TechLef58LayerType::kAboveDieEdge;
  if (type == "DIFFUSION") return TechLef58LayerType::kDiffusion;
  if (type == "TRIMMETAL") return TechLef58LayerType::kTrimMetal;
  if (type == "MEOL") return TechLef58LayerType::kMeol;
  if (type == "PADMETAL") return TechLef58LayerType::kPadMetal;
  if (type == "TSVMETAL") return TechLef58LayerType::kTsvMetal;
  if (type == "STACKEDMIMCAP") return TechLef58LayerType::kStackedMimCap;
  if (type == "SPECIALCUT") return TechLef58LayerType::kSpecialCut;
  if (type == "WELLDISTANCE") return TechLef58LayerType::kWellDistance;
  if (type == "CPODE") return TechLef58LayerType::kCpode;
  if (type == "HIGHR") return TechLef58LayerType::kHighR;
  if (type == "REGION") return TechLef58LayerType::kRegion;
  if (type == "RCBLOCKAGE") return TechLef58LayerType::kRcBlockage;
  if (type == "ABUTFILLER") return TechLef58LayerType::kAbutFiller;
  if (type == "ABUTLOGIC") return TechLef58LayerType::kAbutLogic;
  return TechLef58LayerType::kNone;
}

CutClassEdge cutClassEdge(const std::string& source)
{
  if (source == "SIDE") return CutClassEdge::kSide;
  if (source == "END") return CutClassEdge::kEnd;
  return CutClassEdge::kUnspecified;
}

CutDirection cutDirection(const std::string& source)
{
  if (source == "HORIZONTAL") return CutDirection::kHorizontal;
  if (source == "VERTICAL") return CutDirection::kVertical;
  return CutDirection::kUnknown;
}

TechRoutingMinStepType routingLef58MinStepType(::idb::routinglayer::Lef58MinStep::Type source)
{
  switch (source) {
    case ::idb::routinglayer::Lef58MinStep::Type::kInsideCorner:
      return TechRoutingMinStepType::kInsideCorner;
    case ::idb::routinglayer::Lef58MinStep::Type::kOutsideCorner:
      return TechRoutingMinStepType::kOutsideCorner;
    case ::idb::routinglayer::Lef58MinStep::Type::kStep:
      return TechRoutingMinStepType::kStep;
    case ::idb::routinglayer::Lef58MinStep::Type::kNone:
      return TechRoutingMinStepType::kNone;
  }
  return TechRoutingMinStepType::kNone;
}

TechRoutingMinimumCutOrient routingLef58MinimumCutOrient(::idb::routinglayer::Lef58MinimumCut::Orient source)
{
  switch (source) {
    case ::idb::routinglayer::Lef58MinimumCut::Orient::kFromAbove:
      return TechRoutingMinimumCutOrient::kFromAbove;
    case ::idb::routinglayer::Lef58MinimumCut::Orient::kFromBelow:
      return TechRoutingMinimumCutOrient::kFromBelow;
    case ::idb::routinglayer::Lef58MinimumCut::Orient::kNone:
      return TechRoutingMinimumCutOrient::kNone;
  }
  return TechRoutingMinimumCutOrient::kNone;
}

CutDirection cutClassDirection(::idb::cutlayer::Lef58Cutclass::Orient source)
{
  switch (source) {
    case ::idb::cutlayer::Lef58Cutclass::kHorizontal:
      return CutDirection::kHorizontal;
    case ::idb::cutlayer::Lef58Cutclass::kVertical:
      return CutDirection::kVertical;
    case ::idb::cutlayer::Lef58Cutclass::kNone:
      return CutDirection::kUnknown;
  }
  return CutDirection::kUnknown;
}

CutLayerSide cutLayerSide(::idb::cutlayer::Lef58EnclosureEdge::Direction source)
{
  if (source == ::idb::cutlayer::Lef58EnclosureEdge::kAbove) return CutLayerSide::kAbove;
  if (source == ::idb::cutlayer::Lef58EnclosureEdge::kBelow) return CutLayerSide::kBelow;
  return CutLayerSide::kUnknown;
}

CutLayerSide cutLayerSide(::idb::cutlayer::Lef58EolEnclosure::Direction source)
{
  if (source == ::idb::cutlayer::Lef58EolEnclosure::Direction::kAbove) return CutLayerSide::kAbove;
  if (source == ::idb::cutlayer::Lef58EolEnclosure::Direction::kBelow) return CutLayerSide::kBelow;
  return CutLayerSide::kUnknown;
}

CutDirection cutDirection(::idb::cutlayer::Lef58EolEnclosure::EdgeDirection source)
{
  if (source == ::idb::cutlayer::Lef58EolEnclosure::EdgeDirection::kHorizontal) return CutDirection::kHorizontal;
  if (source == ::idb::cutlayer::Lef58EolEnclosure::EdgeDirection::kVertical) return CutDirection::kVertical;
  return CutDirection::kUnknown;
}

Lef58EolEnclosureApplication eolEnclosureApplication(::idb::cutlayer::Lef58EolEnclosure::ApplicationType source)
{
  if (source == ::idb::cutlayer::Lef58EolEnclosure::ApplicationType::kLongEdgeOnly) {
    return Lef58EolEnclosureApplication::kLongEdgeOnly;
  }
  if (source == ::idb::cutlayer::Lef58EolEnclosure::ApplicationType::kShortEdgeOnly) {
    return Lef58EolEnclosureApplication::kShortEdgeOnly;
  }
  return Lef58EolEnclosureApplication::kUnknown;
}

TechGlobalUnits importUnits(const ::idb::IdbUnits& source)
{
  TechGlobalUnits target;
  if (source.get_nanoseconds() > 0) {
    target.flags |= TechGlobalUnitsFlag::kHasNanoseconds;
    target.nanoseconds = source.get_nanoseconds();
  }
  if (source.get_picofarads() > 0) {
    target.flags |= TechGlobalUnitsFlag::kHasPicofarads;
    target.picofarads = source.get_picofarads();
  }
  if (source.get_ohms() > 0) {
    target.flags |= TechGlobalUnitsFlag::kHasOhms;
    target.ohms = source.get_ohms();
  }
  if (source.get_milliwatts() > 0) {
    target.flags |= TechGlobalUnitsFlag::kHasMilliwatts;
    target.milliwatts = source.get_milliwatts();
  }
  if (source.get_milliamps() > 0) {
    target.flags |= TechGlobalUnitsFlag::kHasMilliamps;
    target.milliamps = source.get_milliamps();
  }
  if (source.get_volts() > 0) {
    target.flags |= TechGlobalUnitsFlag::kHasVolts;
    target.volts = source.get_volts();
  }
  if (source.get_micron_dbu() > 0) {
    target.flags |= TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron;
    target.database_units_per_micron = source.get_micron_dbu();
  }
  if (source.get_megahertz() > 0) {
    target.flags |= TechGlobalUnitsFlag::kHasMegahertz;
    target.megahertz = source.get_megahertz();
  }
  return target;
}

TechRoutingLayer importRoutingLayer(::idb::IdbLayerRouting& source)
{
  TechRoutingLayer target;
  target.direction = routingDirection(source.get_direction());
  target.pitch_form = axisValueForm(source.get_pitch().type);
  target.offset_form = axisValueForm(source.get_offset().type);

  if (source.get_width() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasWidth;
    target.width = source.get_width();
  }
  if (source.get_min_width() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasMinWidth;
    target.min_width = source.get_min_width();
  }
  if (source.get_max_width() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasMaxWidth;
    target.max_width = source.get_max_width();
  }
  if (source.get_diag_width() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasDiagWidth;
    target.diag_width = source.get_diag_width();
  }
  if (source.get_diag_spacing() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasDiagSpacing;
    target.diag_spacing = source.get_diag_spacing();
  }
  if (target.pitch_form != TechRoutingAxisValueForm::kNone) {
    target.pitch_x = source.get_pitch_x();
    target.pitch_y = source.get_pitch_y();
    if (target.pitch_form == TechRoutingAxisValueForm::kSeparateXY) {
      target.flags |= TechRoutingLayerFlag::kHasPitchY;
    }
  }
  if (target.offset_form != TechRoutingAxisValueForm::kNone) {
    target.offset_x = source.get_offset_x();
    target.offset_y = source.get_offset_y();
    if (target.offset_form == TechRoutingAxisValueForm::kSeparateXY) {
      target.flags |= TechRoutingLayerFlag::kHasOffsetY;
    }
  }
  if (source.get_wire_extension() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasWireExtension;
    target.wire_extension = source.get_wire_extension();
  }
  if (source.get_thickness() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasThickness;
    target.thickness = source.get_thickness();
  }
  if (source.get_height() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasHeight;
    target.height = source.get_height();
  }
  if (source.get_shrinkage() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasShrinkage;
    target.shrinkage = source.get_shrinkage();
  }
  if (source.get_cap_multiplier() >= 0.0) {
    target.flags |= TechRoutingLayerFlag::kHasCapMultiplier;
    target.cap_multiplier = source.get_cap_multiplier();
  }
  if (source.get_fill_active_spacing() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasFillActiveSpacing;
    target.fill_active_spacing = source.get_fill_active_spacing();
  }
  if (source.get_area() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasArea;
    target.area = source.get_area();
  }
  if (source.get_resistance() >= 0.0) {
    target.flags |= TechRoutingLayerFlag::kHasResistance;
    target.resistance = source.get_resistance();
  }
  if (source.get_capacitance() >= 0.0) {
    target.flags |= TechRoutingLayerFlag::kHasCapacitance;
    target.capacitance = source.get_capacitance();
  }
  if (source.get_edge_capacitance() >= 0.0) {
    target.flags |= TechRoutingLayerFlag::kHasEdgeCapacitance;
    target.edge_capacitance = source.get_edge_capacitance();
  }
  if (source.get_min_density() >= 0.0) {
    target.flags |= TechRoutingLayerFlag::kHasMinDensity;
    target.min_density = source.get_min_density();
  }
  if (source.get_max_density() >= 0.0) {
    target.flags |= TechRoutingLayerFlag::kHasMaxDensity;
    target.max_density = source.get_max_density();
  }
  if (source.get_density_check_length() >= 0 && source.get_density_check_width() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasDensityCheckWindow;
    target.density_check_length = source.get_density_check_length();
    target.density_check_width = source.get_density_check_width();
  }
  if (source.get_density_check_step() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasDensityCheckStep;
    target.density_check_step = source.get_density_check_step();
  }
  if (source.get_min_cut_num() >= 0 && source.get_min_cut_width() >= 0) {
    target.flags |= TechRoutingLayerFlag::kHasMinCut;
    target.min_cut_num = source.get_min_cut_num();
    target.min_cut_width = source.get_min_cut_width();
  }
  if (source.has_protrusion_width()) {
    target.flags |= TechRoutingLayerFlag::kHasProtrusion;
    target.protrusion_width1 = source.get_protrusion_width_1();
    target.protrusion_length = source.get_protrusion_length();
    target.protrusion_width2 = source.get_protrusion_width_2();
  }
  if (source.is_lef58_rect_only()) {
    target.flags |= TechRoutingLayerFlag::kLef58RectOnly;
  }
  if (source.is_lef58_rect_only_except_non_core_pins()) {
    target.flags |= TechRoutingLayerFlag::kLef58RectOnlyExceptNonCorePins;
  }
  if (source.is_lef58_right_way_on_grid_only()) {
    target.flags |= TechRoutingLayerFlag::kLef58RightWayOnGridOnly;
  }
  if (source.is_lef58_right_way_on_grid_only_check_mask()) {
    target.flags |= TechRoutingLayerFlag::kLef58RightWayOnGridOnlyCheckMask;
  }
  if (upper(source.get_lef58_type()) == "POLYROUTING") {
    target.flags |= TechRoutingLayerFlag::kPolyRouting;
  }
  return target;
}

TechCutLayer importCutLayer(::idb::IdbLayerCut& source)
{
  TechCutLayer target;
  if (source.get_width() >= 0) {
    target.flags |= TechCutLayerFlag::kHasWidth;
    target.width = source.get_width();
  }
  if (source.get_resistance_per_cut() >= 0.0) {
    target.flags |= TechCutLayerFlag::kHasResistance;
    target.resistance_per_cut = source.get_resistance_per_cut();
  }
  return target;
}

TechRoutingMinStepType routingMinStepType(::idb::IdbMinStep::Type source)
{
  switch (source) {
    case ::idb::IdbMinStep::Type::kINSIDECORNER:
      return TechRoutingMinStepType::kInsideCorner;
    case ::idb::IdbMinStep::Type::kOUTSIDECORNER:
      return TechRoutingMinStepType::kOutsideCorner;
    case ::idb::IdbMinStep::Type::kSTEP:
      return TechRoutingMinStepType::kStep;
    case ::idb::IdbMinStep::Type::kNone:
      return TechRoutingMinStepType::kNone;
  }
  return TechRoutingMinStepType::kNone;
}

TechRoutingMinimumCutOrient routingMinimumCutOrient(::idb::IdbLayerMinimumCut::Orient source)
{
  switch (source) {
    case ::idb::IdbLayerMinimumCut::Orient::kFromAbove:
      return TechRoutingMinimumCutOrient::kFromAbove;
    case ::idb::IdbLayerMinimumCut::Orient::kFromBelow:
      return TechRoutingMinimumCutOrient::kFromBelow;
    case ::idb::IdbLayerMinimumCut::Orient::kNone:
      return TechRoutingMinimumCutOrient::kNone;
  }
  return TechRoutingMinimumCutOrient::kNone;
}

TechRoutingCurrentDensityType routingCurrentDensityType(::idb::IdbRoutingCurrentDensity::Type source)
{
  switch (source) {
    case ::idb::IdbRoutingCurrentDensity::Type::kPeak:
      return TechRoutingCurrentDensityType::kPeak;
    case ::idb::IdbRoutingCurrentDensity::Type::kAverage:
      return TechRoutingCurrentDensityType::kAverage;
    case ::idb::IdbRoutingCurrentDensity::Type::kRms:
      return TechRoutingCurrentDensityType::kRms;
    case ::idb::IdbRoutingCurrentDensity::Type::kUnknown:
      return TechRoutingCurrentDensityType::kUnknown;
  }
  return TechRoutingCurrentDensityType::kUnknown;
}

TechCutCurrentDensityType cutCurrentDensityType(::idb::IdbCutCurrentDensity::Type source)
{
  switch (source) {
    case ::idb::IdbCutCurrentDensity::Type::kPeak:
      return TechCutCurrentDensityType::kPeak;
    case ::idb::IdbCutCurrentDensity::Type::kAverage:
      return TechCutCurrentDensityType::kAverage;
    case ::idb::IdbCutCurrentDensity::Type::kRms:
      return TechCutCurrentDensityType::kRms;
    case ::idb::IdbCutCurrentDensity::Type::kUnknown:
      return TechCutCurrentDensityType::kUnknown;
  }
  return TechCutCurrentDensityType::kUnknown;
}

void importRoutingRules(TechRoutingLayerStorage& storage, TechRoutingLayerId owner, ::idb::IdbLayerRouting& source)
{
  for (const auto* source_rule : source.get_spacing_list()->get_spacing_list()) {
    if (source_rule == nullptr) {
      continue;
    }
    if (source_rule->get_spacing_type() == ::idb::IdbLayerSpacingType::kSpacingEndOfLine) {
      TechRoutingEndOfLineSpacingRule rule{.min_spacing = source_rule->get_min_spacing(),
                                           .eol_width = source_rule->get_eol_width(),
                                           .eol_within = source_rule->get_eol_within()};
      if (source_rule->get_has_parallel_edge()) {
        rule.flags |= TechRoutingEndOfLineSpacingRuleFlag::kHasParallelEdge;
        rule.parallel_space = source_rule->get_par_space();
        rule.parallel_within = source_rule->get_par_within();
      }
      if (source_rule->get_has_two_edges()) {
        rule.flags |= TechRoutingEndOfLineSpacingRuleFlag::kTwoEdges;
      }
      static_cast<void>(storage.addEndOfLineSpacingRule(owner, rule));
      continue;
    }
    TechRoutingSpacingRule rule{.min_spacing = source_rule->get_min_spacing()};
    if (source_rule->get_spacing_type() == ::idb::IdbLayerSpacingType::kSpacingRange) {
      rule.type = TechRoutingSpacingType::kRange;
      rule.min_width = source_rule->get_min_width();
      rule.max_width = source_rule->get_max_width();
    }
    static_cast<void>(storage.addSpacingRule(owner, rule));
  }

  for (const auto& source_rule : source.get_lef58_area()) {
    if (source_rule == nullptr) continue;
    TechRoutingLef58AreaRule rule{.min_area = source_rule->get_min_area()};
    if (const auto value = source_rule->get_mask()) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasMask;
      rule.mask = *value;
    }
    if (const auto value = source_rule->get_except_min_width()) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasExceptMinWidth;
      rule.except_min_width = *value;
    }
    if (const auto value = source_rule->get_except_edge_length(); value != nullptr) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasExceptEdgeLength;
      rule.except_max_edge_length = value->get_max_edge_length();
      if (const auto minimum = value->get_min_edge_length()) {
        rule.flags |= TechRoutingLef58AreaRuleFlag::kHasExceptMinEdgeLength;
        rule.except_min_edge_length = *minimum;
      }
    }
    for (const auto& size : source_rule->get_except_min_size()) {
      rule.except_min_sizes.push_back({.min_width = size.get_min_width(), .min_length = size.get_min_length()});
    }
    if (const auto value = source_rule->get_except_step()) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasExceptStep;
      rule.except_step_x = value->get_step_x();
      rule.except_step_y = value->get_step_y();
    }
    if (const auto value = source_rule->get_rect_width()) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasRectWidth;
      rule.rect_width = *value;
    }
    if (source_rule->is_except_rectangle()) rule.flags |= TechRoutingLef58AreaRuleFlag::kExceptRectangle;
    if (!source_rule->get_trim_layer().empty()) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasTrimLayer;
      rule.trim_layer_name = source_rule->get_trim_layer();
    }
    if (const auto value = source_rule->get_overlap()) {
      rule.flags |= TechRoutingLef58AreaRuleFlag::kHasOverlap;
      rule.overlap = *value;
    }
    static_cast<void>(storage.addLef58AreaRule(owner, std::move(rule)));
  }

  if (const auto source_rule = source.get_lef58_corner_fill_spacing(); source_rule != nullptr) {
    static_cast<void>(storage.addLef58CornerFillSpacingRule(
        owner, TechRoutingLef58CornerFillSpacingRule{.spacing = source_rule->get_spacing(),
                                                     .edge_length1 = source_rule->get_edge_length1(),
                                                     .edge_length2 = source_rule->get_edge_length2(),
                                                     .eol_width = source_rule->get_eol_width()}));
  }

  for (const auto& source_rule : source.get_lef58_corner_spacing_list()) {
    if (source_rule == nullptr) {
      continue;
    }
    TechRoutingLef58CornerSpacingRule rule;
    rule.type = source_rule->get_corner_type() == ::idb::routinglayer::Lef58CornerSpacing::CornerType::kConcaveCorner
                    ? TechRoutingLef58CornerType::kConcave
                    : TechRoutingLef58CornerType::kConvex;
    if (source_rule->get_except_eol()) {
      rule.flags |= TechRoutingLef58CornerSpacingRuleFlag::kHasExceptEol;
      rule.except_eol = *source_rule->get_except_eol();
    }
    if (source_rule->is_corner_to_corner()) {
      rule.flags |= TechRoutingLef58CornerSpacingRuleFlag::kCornerToCorner;
    }
    for (const auto& item : source_rule->get_width_spacing_list()) {
      rule.width_spacings.push_back({.width = item.get_width(), .spacing = item.get_spacing()});
    }
    static_cast<void>(storage.addLef58CornerSpacingRule(owner, std::move(rule)));
  }

  for (const auto& source_rule : source.get_lef58_minimum_cut()) {
    if (source_rule == nullptr) continue;
    TechRoutingLef58MinimumCutRule rule{.width = source_rule->get_width(),
                                        .orient = routingLef58MinimumCutOrient(source_rule->get_orient())};
    if (const auto value = source_rule->get_num_cuts()) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasNumCuts;
      rule.num_cuts = *value;
    }
    if (!source_rule->get_cut_classes().empty()) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasCutClasses;
      for (const auto& cutclass : source_rule->get_cut_classes()) {
        rule.cutclasses.push_back({.cutclass_name = cutclass.get_class_name(), .num_cuts = cutclass.get_num_cuts()});
      }
    }
    if (const auto value = source_rule->get_within_cut_distance()) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasWithinCutDistance;
      rule.within_cut_distance = *value;
    }
    if (const auto value = source_rule->get_length()) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasLength;
      rule.length = value->get_length();
      rule.length_distance = value->get_distance();
    }
    if (const auto value = source_rule->get_area()) {
      rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasArea;
      rule.area = value->get_area();
      if (const auto distance = value->get_within_distance()) {
        rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kHasAreaWithinDistance;
        rule.area_within_distance = *distance;
      }
    }
    if (source_rule->is_same_metal_overlap()) rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kSameMetalOverlap;
    if (source_rule->is_fully_enclosed()) rule.flags |= TechRoutingLef58MinimumCutRuleFlag::kFullyEnclosed;
    static_cast<void>(storage.addLef58MinimumCutRule(owner, std::move(rule)));
  }

  for (const auto& source_rule : source.get_lef58_min_step()) {
    if (source_rule == nullptr) continue;
    TechRoutingLef58MinStepRule rule{.type = routingLef58MinStepType(source_rule->get_type()),
                                     .min_step_length = source_rule->get_min_step_length()};
    if (source_rule->get_type() != ::idb::routinglayer::Lef58MinStep::Type::kNone) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasType;
    }
    if (const auto value = source_rule->get_max_length_sum()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasMaxLengthSum;
      rule.max_length_sum = *value;
    }
    if (const auto value = source_rule->get_max_edges()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasMaxEdges;
      rule.max_edges = static_cast<uint32_t>(*value);
    }
    if (source_rule->is_except_rectangle()) rule.flags |= TechRoutingLef58MinStepRuleFlag::kExceptRectangle;
    if (const auto value = source_rule->get_min_adjacent_length()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasMinAdjacentLength;
      rule.min_adjacent_length = value->get_min_adj_length();
      if (const auto second = value->get_min_adj_length2()) {
        rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasSecondMinAdjacentLength;
        rule.min_adjacent_length2 = *second;
      }
      if (value->is_convex_corner()) rule.flags |= TechRoutingLef58MinStepRuleFlag::kConvexCorner;
      if (value->is_concave_corner()) rule.flags |= TechRoutingLef58MinStepRuleFlag::kConcaveCorner;
      if (const auto within = value->get_except_within()) {
        rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasExceptWithin;
        rule.except_within = *within;
      }
    }
    if (source_rule->has_three_concave_corners()) rule.flags |= TechRoutingLef58MinStepRuleFlag::kThreeConcaveCorners;
    if (const auto value = source_rule->get_center_width()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasCenterWidth;
      rule.center_width = *value;
    }
    if (const auto value = source_rule->get_min_between_length()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasMinBetweenLength;
      rule.min_between_length = *value;
    }
    if (source_rule->is_except_same_corners()) rule.flags |= TechRoutingLef58MinStepRuleFlag::kExceptSameCorners;
    if (const auto value = source_rule->get_no_adjacent_eol_width()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasNoAdjacentEol;
      rule.no_adjacent_eol_width = *value;
    }
    if (const auto value = source_rule->get_except_adjacent_length()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasExceptAdjacentLength;
      rule.except_adjacent_length = *value;
    }
    if (const auto value = source_rule->get_followup_min_adjacent_length()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasFollowupMinAdjacentLength;
      rule.followup_min_adjacent_length = *value;
    }
    if (source_rule->has_concave_corners()) rule.flags |= TechRoutingLef58MinStepRuleFlag::kConcaveCorners;
    if (const auto value = source_rule->get_no_between_eol_width()) {
      rule.flags |= TechRoutingLef58MinStepRuleFlag::kHasNoBetweenEol;
      rule.no_between_eol_width = *value;
    }
    static_cast<void>(storage.addLef58MinStepRule(owner, std::move(rule)));
  }

  for (const auto& source_rule : source.get_lef58_width_table()) {
    if (source_rule == nullptr) continue;
    TechRoutingLef58WidthTableRule rule{.widths = source_rule->get_widths()};
    if (source_rule->is_wrong_direction()) rule.flags |= TechRoutingLef58WidthTableRuleFlag::kWrongDirection;
    if (source_rule->is_orthogonal()) rule.flags |= TechRoutingLef58WidthTableRuleFlag::kOrthogonal;
    static_cast<void>(storage.addLef58WidthTableRule(owner, std::move(rule)));
  }

  if (const auto source_rule = source.get_lef58_spacing_notchlength(); source_rule != nullptr) {
    TechRoutingLef58SpacingNotchLengthRule rule{.min_spacing = source_rule->get_min_spacing(),
                                                .min_notch_length = source_rule->get_min_notch_length()};
    if (const auto value = source_rule->get_concave_ends_side_of_notch_width()) {
      rule.flags |= TechRoutingLef58SpacingNotchLengthRuleFlag::kHasConcaveEndsSideOfNotchWidth;
      rule.concave_ends_side_of_notch_width = *value;
    }
    static_cast<void>(storage.addLef58SpacingNotchLengthRule(owner, std::move(rule)));
  }

  for (const auto& source_rule : source.get_lef58_spacing_eol_list()) {
    if (source_rule == nullptr) continue;
    TechRoutingLef58SpacingEolRule rule{.eol_space = source_rule->get_eol_space(), .eol_width = source_rule->get_eol_width()};
    if (source_rule->is_exact_width()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kExactWidth;
    if (const auto value = source_rule->get_wrong_dir_space()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasWrongDirSpace;
      rule.wrong_dir_space = *value;
    }
    if (const auto value = source_rule->get_opposite_width()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasOppositeWidth;
      rule.opposite_width = *value;
    }
    if (const auto value = source_rule->get_eol_within()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEolWithin;
      rule.eol_within = *value;
    }
    if (const auto value = source_rule->get_wrong_dir_within()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasWrongDirWithin;
      rule.wrong_dir_within = *value;
    }
    if (source_rule->is_same_mask()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kSameMask;
    if (const auto value = source_rule->get_except_exact_width()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasExceptExactWidth;
      rule.except_exact_width1 = value->get_width1();
      rule.except_exact_width2 = value->get_width2();
    }
    if (const auto value = source_rule->get_fill_concave_corner_width()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasFillConcaveCorner;
      rule.fill_concave_corner_width = *value;
    }
    if (const auto value = source_rule->get_with_cut()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasWithCut;
      if (!value->get_cutclass().empty()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasWithCutClass;
        rule.with_cut_class_name = value->get_cutclass();
      }
      if (value->is_above()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kWithCutAbove;
      rule.with_cut_space = value->get_with_cut_space();
      if (const auto width = value->get_enclosure_end_width()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEnclosureEndWidth;
        rule.enclosure_end_width = *width;
      }
      if (const auto within = value->get_enclosure_end_within()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEnclosureEndWithin;
        rule.enclosure_end_within = *within;
      }
    }
    if (const auto value = source_rule->get_end_prl_spacing()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEndPrlSpacing;
      rule.end_prl_space = value->get_end_prl_space();
      rule.end_prl = value->get_end_prl();
    }
    if (const auto value = source_rule->get_end_to_end()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEndToEnd;
      rule.end_to_end_space = value->get_end_to_end_space();
      if (const auto spacing = value->get_one_cut_space()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasOneCutSpace;
        rule.one_cut_space = *spacing;
      }
      if (const auto spacing = value->get_two_cut_space()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasTwoCutSpace;
        rule.two_cut_space = *spacing;
      }
      if (const auto extension = value->get_extionsion()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasExtension;
        rule.extension = *extension;
      }
      if (const auto extension = value->get_wrong_dir_extionsion()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasWrongDirExtension;
        rule.wrong_dir_extension = *extension;
      }
      if (const auto width = value->get_other_end_width()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasOtherEndWidth;
        rule.other_end_width = *width;
      }
    }
    if (const auto value = source_rule->get_adj_edge_length()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasAdjacentEdgeLength;
      if (const auto length = value->get_max_length()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasAdjacentMaxLength;
        rule.adjacent_max_length = *length;
      }
      if (const auto length = value->get_min_length()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasAdjacentMinLength;
        rule.adjacent_min_length = *length;
      }
      if (value->is_two_sides()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kTwoSides;
    }
    if (source_rule->is_equal_rect_width()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kEqualRectWidth;
    if (const auto value = source_rule->get_parallel_edge()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasParallelEdge;
      rule.parallel_space = value->get_par_space();
      rule.parallel_within = value->get_par_within();
      if (value->is_subtract_eol_width()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kSubtractEolWidth;
      if (const auto length = value->get_prl()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasParallelRunLength;
        rule.parallel_run_length = *length;
      }
      if (const auto length = value->get_min_length()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasParallelMinLength;
        rule.parallel_min_length = *length;
      }
      if (value->is_two_edges()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kTwoEdges;
      if (value->is_same_metal()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kSameMetal;
      if (value->is_non_eol_corner_only()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kNonEolCornerOnly;
      if (value->is_parallel_same_mask()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kParallelSameMask;
    }
    if (const auto value = source_rule->get_enclose_cut()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasEncloseCut;
      if (value->get_direction() == ::idb::routinglayer::Lef58SpacingEol::Direction::kAbove) {
        rule.enclose_cut_side = CutLayerSide::kAbove;
      } else if (value->get_direction() == ::idb::routinglayer::Lef58SpacingEol::Direction::kBelow) {
        rule.enclose_cut_side = CutLayerSide::kBelow;
      }
      rule.enclose_distance = value->get_enclose_dist();
      rule.cut_to_metal_spacing = value->get_cut_to_metal_space();
      if (value->is_all_cuts()) rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kAllCuts;
    }
    if (const auto value = source_rule->get_to_concave_corner()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasToConcaveCorner;
      if (const auto length = value->get_min_length()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasToConcaveCornerMinLength;
        rule.to_concave_corner_min_length = *length;
      }
      if (const auto length = value->get_min_adj_length1()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasToConcaveCornerMinAdjacentLength;
        rule.to_concave_corner_min_adjacent_length1 = *length;
      }
      if (const auto length = value->get_min_adj_length2()) {
        rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasToConcaveCornerTwoMinAdjacentLengths;
        rule.to_concave_corner_min_adjacent_length2 = *length;
      }
    }
    if (const auto value = source_rule->get_notch_length()) {
      rule.flags |= TechRoutingLef58SpacingEolRuleFlag::kHasToNotchLength;
      rule.notch_length = *value;
    }
    static_cast<void>(storage.addLef58SpacingEolRule(owner, std::move(rule)));
  }

  const auto& source_notch = source.get_spacing_notchlength();
  if (source_notch.exist()) {
    static_cast<void>(storage.addSpacingNotchLengthRule(
        owner,
        TechRoutingSpacingNotchLengthRule{.min_spacing = source_notch.get_min_spacing(), .notch_length = source_notch.get_notch_length()}));
  }

  for (const auto& source_rule : source.get_min_enclose_area_list()->get_min_area_list()) {
    static_cast<void>(
        storage.addMinEncloseAreaRule(owner, TechRoutingMinEncloseAreaRule{.area = source_rule._area, .width = source_rule._width}));
  }

  if (const auto source_rule = source.get_min_step(); source_rule != nullptr) {
    TechRoutingMinStepRule rule{.min_step_length = source_rule->get_min_step_length(), .type = routingMinStepType(source_rule->get_type())};
    if (source_rule->has_length_sum()) {
      rule.flags |= TechRoutingMinStepRuleFlag::kHasMaxLengthSum;
      rule.max_length_sum = source_rule->get_max_length_sum();
    }
    if (source_rule->has_max_edges()) {
      rule.flags |= TechRoutingMinStepRuleFlag::kHasMaxEdges;
      rule.max_edges = source_rule->get_max_edges();
    }
    static_cast<void>(storage.addMinStepRule(owner, rule));
  }

  for (const auto& source_rule : source.get_minimum_cut_list()) {
    TechRoutingMinimumCutRule rule{.num_cuts = source_rule.get_num_cuts(),
                                   .width = source_rule.get_width(),
                                   .orient = routingMinimumCutOrient(source_rule.get_orient())};
    if (source_rule.has_within_cut_distance()) {
      rule.flags |= TechRoutingMinimumCutRuleFlag::kHasWithinCutDistance;
      rule.within_cut_distance = source_rule.get_within_cut_distance();
    }
    if (source_rule.has_length()) {
      rule.flags |= TechRoutingMinimumCutRuleFlag::kHasLength;
      rule.length = source_rule.get_length();
      rule.length_distance = source_rule.get_length_distance();
    }
    static_cast<void>(storage.addMinimumCutRule(owner, rule));
  }

  if (const auto tables = source.get_spacing_table(); tables != nullptr) {
    if (const auto source_table = tables->get_parallel(); source_table != nullptr) {
      TechRoutingPrlSpacingTableRule table{.widths = source_table->get_width_list(),
                                           .parallel_run_lengths = source_table->get_parallel_length_list()};
      const auto& rows = source_table->get_spacing_table();
      for (const auto& row : rows) {
        table.cells.insert(table.cells.end(), row.begin(), row.end());
      }
      static_cast<void>(storage.addPrlSpacingTableRule(owner, std::move(table)));
    }
    if (const auto source_table = tables->get_influence(); source_table != nullptr) {
      TechRoutingInfluenceSpacingTableRule table;
      table.entries.reserve(source_table->get_entry_list().size());
      for (const auto& entry : source_table->get_entry_list()) {
        table.entries.push_back(
            TechRoutingInfluenceSpacingTableEntry{.width = entry.width, .within = entry.within, .spacing = entry.spacing});
      }
      static_cast<void>(storage.addInfluenceSpacingTableRule(owner, std::move(table)));
    }
    if (const auto source_table = tables->get_two_widths(); source_table != nullptr) {
      TechRoutingTwoWidthsSpacingTableRule table;
      table.widths.reserve(source_table->get_width_list().size());
      for (const auto& source_width : source_table->get_width_list()) {
        table.widths.push_back(
            TechRoutingTwoWidthsSpacingTableWidth{.width = source_width.width, .has_prl = source_width.has_prl, .prl = source_width.prl});
      }
      for (const auto& row : source_table->get_spacing_table()) {
        table.cells.insert(table.cells.end(), row.begin(), row.end());
      }
      static_cast<void>(storage.addTwoWidthsSpacingTableRule(owner, std::move(table)));
    }
  }

  for (const auto& source_table : source.get_lef58_spacingtable_prl()) {
    if (source_table == nullptr) continue;
    TechRoutingPrlSpacingTableRule table;
    if (source_table->is_wrong_direction()) table.flags |= TechRoutingPrlSpacingTableRuleFlag::kWrongDirection;
    if (source_table->is_same_mask()) table.flags |= TechRoutingPrlSpacingTableRuleFlag::kSameMask;
    if (const auto eol_width = source_table->get_except_eol_width()) {
      table.flags |= TechRoutingPrlSpacingTableRuleFlag::kExceptEol | TechRoutingPrlSpacingTableRuleFlag::kHasEolWidth;
      table.eol_width = *eol_width;
    }
    table.parallel_run_lengths = source_table->get_parallel_run_lengths();
    for (uint32_t index = 0; index < source_table->get_widths().size(); ++index) {
      const auto& width = source_table->get_widths()[index];
      table.widths.push_back(width.get_width());
      table.cells.insert(table.cells.end(), width.get_spacings().begin(), width.get_spacings().end());
      if (const auto except = width.get_except_within())
        table.except_withins.push_back({.width_index = index, .low = except->first, .high = except->second});
    }
    for (const auto& influence : source_table->get_influences()) {
      table.influences.push_back({.width = influence.get_width(),
                                  .within = influence.get_within(),
                                  .spacing = influence.get_spacing()});
    }
    static_cast<void>(storage.addPrlSpacingTableRule(owner, std::move(table)));
  }

  if (const auto source_table = source.get_lef58_spacingtable_jogtojog(); source_table != nullptr) {
    TechRoutingLef58SpacingTableJogToJogRule rule{.jog_to_jog_spacing = source_table->get_jog_to_jog_spacing(),
                                                   .jog_width = source_table->get_jog_width(),
                                                   .short_jog_spacing = source_table->get_short_jog_spacing()};
    for (const auto& width : source_table->get_width_list()) {
      rule.widths.push_back({.width = width.get_width(),
                             .parallel_length = width.get_par_length(),
                             .parallel_within = width.get_par_within(),
                             .long_jog_spacing = width.get_long_jog_spacing()});
    }
    static_cast<void>(storage.addLef58SpacingTableJogToJogRule(owner, std::move(rule)));
  }

  for (const auto& source_rule : source.get_current_density_list()) {
    TechRoutingCurrentDensityRule rule{.signal = source_rule.get_kind() == ::idb::IdbRoutingCurrentDensity::Kind::kAc
                                                     ? TechRoutingCurrentDensitySignal::kAc
                                                     : TechRoutingCurrentDensitySignal::kDc,
                                       .type = routingCurrentDensityType(source_rule.get_type()),
                                       .scalar = source_rule.get_scalar(),
                                       .frequencies = source_rule.get_frequency_list(),
                                       .widths = source_rule.get_width_list(),
                                       .table_entries = source_rule.get_table_entries()};
    if (source_rule.has_scalar()) {
      rule.flags |= TechRoutingCurrentDensityRuleFlag::kHasScalar;
    }
    static_cast<void>(storage.addCurrentDensityRule(owner, std::move(rule)));
  }
}

void importCutRules(TechCutLayerStorage& storage, TechCutLayerId owner, ::idb::IdbLayerCut& source)
{
  for (const auto* source_rule : source.get_spacings()) {
    if (source_rule == nullptr) {
      continue;
    }
    TechCutSpacingRule rule{.spacing = source_rule->get_spacing()};
    if (source_rule->get_has_same_net()) {
      rule.flags |= TechCutSpacingRuleFlag::kSameNet;
    }
    if (source_rule->is_center_to_center()) {
      rule.flags |= TechCutSpacingRuleFlag::kCenterToCenter;
    }
    if (source_rule->is_same_net_pg_only()) {
      rule.flags |= TechCutSpacingRuleFlag::kSameNetPgOnly;
    }
    if (!source_rule->get_second_layer_name().empty()) {
      rule.flags |= TechCutSpacingRuleFlag::kHasSecondLayer;
      rule.second_layer_name = source_rule->get_second_layer_name();
    }
    if (source_rule->is_stack()) {
      rule.flags |= TechCutSpacingRuleFlag::kStack;
    }
    if (source_rule->is_except_same_pg_net()) {
      rule.flags |= TechCutSpacingRuleFlag::kExceptSamePgNet;
    }
    if (source_rule->is_parallel_overlap()) {
      rule.flags |= TechCutSpacingRuleFlag::kParallelOverlap;
    }
    if (source_rule->get_cut_area() >= 0) {
      rule.flags |= TechCutSpacingRuleFlag::kHasCutArea;
      rule.cut_area = source_rule->get_cut_area();
    }
    if (const auto adjacent = source_rule->get_adjacent_cuts(); adjacent) {
      rule.flags |= TechCutSpacingRuleFlag::kHasAdjacentCuts;
      rule.adjacent_cut_count = static_cast<uint32_t>(adjacent->get_adjacent_cuts());
      rule.adjacent_cut_within = adjacent->get_cut_within();
    }
    static_cast<void>(storage.addSpacingRule(owner, rule));
  }

  const auto import_enclosure = [&](const ::idb::IdbLayerCutEnclosure* source_rule, CutLayerSide side) {
    if (source_rule != nullptr && source_rule->get_overhang_1() >= 0 && source_rule->get_overhang_2() >= 0) {
      TechCutEnclosureRule rule{
          .side = side, .overhang1 = source_rule->get_overhang_1(), .overhang2 = source_rule->get_overhang_2()};
      if (source_rule->get_min_width() >= 0) {
        rule.flags |= TechCutEnclosureRuleFlag::kHasMinWidth;
        rule.min_width = source_rule->get_min_width();
      }
      if (source_rule->get_cut_within() >= 0) {
        rule.flags |= TechCutEnclosureRuleFlag::kExceptExtraCut | TechCutEnclosureRuleFlag::kHasCutWithin;
        rule.cut_within = source_rule->get_cut_within();
      }
      if (source_rule->get_min_length() >= 0) {
        rule.flags |= TechCutEnclosureRuleFlag::kHasMinLength;
        rule.min_length = source_rule->get_min_length();
      }
      static_cast<void>(storage.addEnclosureRule(owner, rule));
    }
  };
  if (source.get_enclosure_rules().empty()) {
    import_enclosure(source.get_enclosure_below(), CutLayerSide::kBelow);
    import_enclosure(source.get_enclosure_above(), CutLayerSide::kAbove);
  } else {
    for (const auto& source_rule : source.get_enclosure_rules()) {
      CutLayerSide side = CutLayerSide::kUnknown;
      if (source_rule.get_side() == ::idb::IdbLayerCutEnclosure::Side::kAbove) {
        side = CutLayerSide::kAbove;
      } else if (source_rule.get_side() == ::idb::IdbLayerCutEnclosure::Side::kBelow) {
        side = CutLayerSide::kBelow;
      }
      import_enclosure(&source_rule, side);
    }
  }

  if (auto* source_rule = source.get_array_spacing();
      source_rule != nullptr && source_rule->get_cut_spacing() >= 0 && source_rule->get_array_cut_number() > 0) {
    TechCutArraySpacingRule rule{.cut_spacing = source_rule->get_cut_spacing()};
    if (source_rule->is_long_array()) {
      rule.flags |= TechCutArraySpacingRuleFlag::kLongArray;
    }
    if (source_rule->get_via_width() >= 0) {
      rule.flags |= TechCutArraySpacingRuleFlag::kHasViaWidth;
      rule.via_width = source_rule->get_via_width();
    }
    rule.items.reserve(source_rule->get_array_cut_list().size());
    for (const auto& source_item : source_rule->get_array_cut_list()) {
      rule.items.push_back(
          TechCutArraySpacingItem{.array_cut_count = static_cast<uint32_t>(source_item._array_cut), .spacing = source_item._array_spacing});
    }
    static_cast<void>(storage.setArraySpacingRule(owner, std::move(rule)));
  }

  for (const auto& source_table : source.get_orthogonal_spacing_tables()) {
    TechCutOrthogonalSpacingTableRule rule;
    if (source_table.lef58_property) {
      rule.flags |= TechCutOrthogonalSpacingTableRuleFlag::kLef58Property;
    }
    rule.items.reserve(source_table.items.size());
    for (const auto& item : source_table.items) {
      rule.items.push_back(TechCutOrthogonalSpacingTableItem{.within = item.within, .spacing = item.spacing});
    }
    static_cast<void>(storage.addOrthogonalSpacingTableRule(owner, std::move(rule)));
  }

  for (const auto& source_rule : source.get_lef58_cutclass_list()) {
    if (source_rule == nullptr) continue;
    TechCutLef58CutClassRule rule{.name = source_rule->get_class_name(),
                                  .via_width = source_rule->get_via_width(),
                                  .orient = cutClassDirection(source_rule->get_orient())};
    if (const auto value = source_rule->get_via_length()) {
      rule.flags |= TechCutLef58CutClassRuleFlag::kHasViaLength;
      rule.via_length = *value;
    }
    if (const auto value = source_rule->get_num_cut()) {
      rule.flags |= TechCutLef58CutClassRuleFlag::kHasNumCut;
      rule.num_cut = static_cast<uint32_t>(*value);
    }
    static_cast<void>(storage.addLef58CutClassRule(owner, std::move(rule)));
  }

  for (const auto& source_rule : source.get_lef58_enclosure_list()) {
    if (source_rule == nullptr) {
      continue;
    }
    CutLayerSide side = CutLayerSide::kUnknown;
    if (source_rule->get_direction() == ::idb::cutlayer::Lef58Enclosure::Direction::kAbove) {
      side = CutLayerSide::kAbove;
    } else if (source_rule->get_direction() == ::idb::cutlayer::Lef58Enclosure::Direction::kBelow) {
      side = CutLayerSide::kBelow;
    }
    TechCutLef58EnclosureRule rule{.cutclass_name = source_rule->get_class_name(), .side = side};
    if (source_rule->get_overhang1()) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasOverhang1;
      rule.overhang1 = *source_rule->get_overhang1();
    }
    if (source_rule->get_overhang2()) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasOverhang2;
      rule.overhang2 = *source_rule->get_overhang2();
    }
    if (source_rule->get_end_overhang1()) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasEndOverhang1;
      rule.end_overhang1 = *source_rule->get_end_overhang1();
    }
    if (source_rule->get_side_overhang2()) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasSideOverhang2;
      rule.side_overhang2 = *source_rule->get_side_overhang2();
    }
    if (source_rule->get_min_width()) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasMinWidth;
      rule.min_width = *source_rule->get_min_width();
    }
    if (source_rule->is_include_abutted()) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kIncludeAbutted;
    }
    if (source_rule->get_cut_within()) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kExceptExtraCut | TechCutLef58EnclosureRuleFlag::kHasCutWithin;
      rule.cut_within = *source_rule->get_cut_within();
      if (source_rule->get_except_extra_cut_type() == "PRL") {
        rule.flags |= TechCutLef58EnclosureRuleFlag::kPrl;
      } else if (source_rule->get_except_extra_cut_type() == "NOSHAREDEDGE") {
        rule.flags |= TechCutLef58EnclosureRuleFlag::kNoSharedEdge;
      }
    }
    if (source_rule->get_min_length()) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasMinLength;
      rule.min_length = *source_rule->get_min_length();
    }
    if (source_rule->get_redundant_cut_within()) {
      rule.flags |= TechCutLef58EnclosureRuleFlag::kHasRedundantCut;
      rule.redundant_cut_within = *source_rule->get_redundant_cut_within();
    }
    static_cast<void>(storage.addLef58EnclosureRule(owner, std::move(rule)));
  }

  for (const auto& source_rule : source.get_lef58_enclosure_edge_list()) {
    if (source_rule == nullptr) continue;
    TechCutLef58EnclosureEdgeRule rule{.cutclass_name = source_rule->get_class_name(),
                                       .side = cutLayerSide(source_rule->get_direction()),
                                       .overhang = source_rule->get_overhang()};
    if (const auto value = source_rule->get_min_width()) {
      rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasMinWidth;
      rule.min_width = *value;
    }
    if (const auto value = source_rule->get_max_width()) {
      rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasMaxWidth;
      rule.max_width = *value;
    }
    if (const auto value = source_rule->get_par_length()) {
      rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasParLength;
      rule.par_length = *value;
    }
    if (const auto value = source_rule->get_par_within()) {
      rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasParWithin;
      rule.par_within = *value;
    }
    if (source_rule->has_except_extracut()) rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kExceptExtraCut;
    if (const auto value = source_rule->get_extracut_within()) {
      rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasCutWithin;
      rule.cut_within = *value;
    }
    if (source_rule->has_except_twoedges()) rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kExceptTwoEdges;
    if (const auto value = source_rule->get_except_within()) {
      rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasExceptWithin;
      rule.except_within = *value;
    }
    if (const auto value = source_rule->get_convex_corners()) {
      rule.flags |= TechCutLef58EnclosureEdgeRuleFlag::kHasConvexCorners;
      rule.convex_length = value->get_convex_length();
      rule.adjacent_length = value->get_adjacent_length();
      rule.convex_par_within = value->get_par_within();
      rule.convex_corner_length = value->get_length();
    }
    static_cast<void>(storage.addLef58EnclosureEdgeRule(owner, std::move(rule)));
  }

  if (const auto source_rule = source.get_lef58_eol_enclosure(); source_rule != nullptr) {
    TechCutLef58EolEnclosureRule rule{.eol_width = source_rule->get_eol_width(),
                                      .edge_direction = cutDirection(source_rule->get_edge_direction()),
                                      .cutclass_name = source_rule->get_class_name(),
                                      .side = cutLayerSide(source_rule->get_direction()),
                                      .application = eolEnclosureApplication(source_rule->get_application_type()),
                                      .overhang = source_rule->get_overhang()};
    if (const auto value = source_rule->get_min_eol_width()) {
      rule.flags |= TechCutLef58EolEnclosureRuleFlag::kHasMinEolWidth;
      rule.min_eol_width = *value;
    }
    if (source_rule->is_equal_rect_width()) rule.flags |= TechCutLef58EolEnclosureRuleFlag::kEqualRectWidth;
    if (const auto value = source_rule->get_extract_overhang()) {
      rule.flags |= TechCutLef58EolEnclosureRuleFlag::kHasExtractOverhang;
      rule.extract_overhang = *value;
    }
    if (const auto value = source_rule->get_par_space()) {
      rule.flags |= TechCutLef58EolEnclosureRuleFlag::kHasParallelSpace;
      rule.parallel_space = *value;
    }
    if (const auto value = source_rule->get_extension()) {
      rule.flags |= TechCutLef58EolEnclosureRuleFlag::kHasExtension;
      rule.backward_ext = value->get_backward_ext();
      rule.forward_ext = value->get_forward_ext();
    }
    if (const auto value = source_rule->get_min_length()) {
      rule.flags |= TechCutLef58EolEnclosureRuleFlag::kHasMinLength;
      rule.min_length = *value;
    }
    if (source_rule->is_all_sides()) rule.flags |= TechCutLef58EolEnclosureRuleFlag::kAllSides;
    static_cast<void>(storage.setLef58EolEnclosureRule(owner, std::move(rule)));
  }

  if (const auto source_rule = source.get_lef58_eol_spacing(); source_rule != nullptr) {
    TechCutLef58EolSpacingRule rule{.cutclass_name = source_rule->get_class_name1(),
                                    .cut_spacing1 = source_rule->get_cut_spacing1(),
                                    .cut_spacing2 = source_rule->get_cut_spacing2(),
                                    .eol_width = source_rule->get_eol_width(),
                                    .prl = source_rule->get_prl(),
                                    .smaller_overhang = source_rule->get_smaller_overhang(),
                                    .equal_overhang = source_rule->get_equal_overhang(),
                                    .side_ext = source_rule->get_side_ext(),
                                    .backward_ext = source_rule->get_backward_ext(),
                                    .span_length = source_rule->get_span_length()};
    for (const auto& item : source_rule->get_to_classes()) {
      rule.to_classes.push_back({.cutclass_name = item.get_class_name(),
                                 .cut_spacing1 = item.get_cut_spacing1(),
                                 .cut_spacing2 = item.get_cut_spacing2()});
    }
    static_cast<void>(storage.setLef58EolSpacingRule(owner, std::move(rule)));
  }

  for (const auto& source_table : source.get_lef58_spacing_table()) {
    if (source_table == nullptr) {
      continue;
    }
    TechCutLef58SpacingTableRule rule;
    if (const auto default_spacing = source_table->get_default_spacing()) {
      rule.flags |= TechCutLef58SpacingTableRuleFlag::kHasDefault;
      rule.default_spacing = *default_spacing;
    }
    if (source_table->is_same_mask()) rule.flags |= TechCutLef58SpacingTableRuleFlag::kSameMask;
    if (source_table->get_same_kind() == "SAMENET") rule.flags |= TechCutLef58SpacingTableRuleFlag::kSameNet;
    if (source_table->get_same_kind() == "SAMEMETAL") rule.flags |= TechCutLef58SpacingTableRuleFlag::kSameMetal;
    if (source_table->get_same_kind() == "SAMEVIA") rule.flags |= TechCutLef58SpacingTableRuleFlag::kSameVia;
    if (const auto second_layer = source_table->get_second_layer()) {
      rule.flags |= TechCutLef58SpacingTableRuleFlag::kHasSecondLayer;
      rule.second_layer_name = second_layer->get_second_layer_name();
      if (second_layer->is_nostack()) rule.flags |= TechCutLef58SpacingTableRuleFlag::kNoStack;
      if (!second_layer->get_prl_for_aligned_cut().empty())
        rule.flags |= TechCutLef58SpacingTableRuleFlag::kPrlForAlignedCut;
      for (const auto& pair : second_layer->get_prl_for_aligned_cut())
        rule.prl_for_aligned_cut.push_back({.from = pair.get_from(), .to = pair.get_to()});
    }
    if (const auto prl = source_table->get_prl()) {
      rule.flags |= TechCutLef58SpacingTableRuleFlag::kHasPrl;
      rule.prl = prl->get_prl();
      rule.prl_direction = cutDirection(prl->get_direction());
      if (prl->is_maxxy()) {
        rule.flags |= TechCutLef58SpacingTableRuleFlag::kMaxXY;
      }
      for (const auto& entry : prl->get_entries())
        rule.prl_entries.push_back({.from = entry.from, .to = entry.to, .prl = entry.prl});
    }
    const auto& cutclass = source_table->get_cutclass();
    for (const auto& name : cutclass.get_class_name1_list()) {
      rule.cutclass1_names.push_back(name.get_class_name());
      rule.cutclass1_edges.push_back(cutClassEdge(name.get_edge()));
    }
    for (const auto& name : cutclass.get_class_name2_list()) {
      rule.cutclass2_names.push_back(name.get_class_name());
      rule.cutclass2_edges.push_back(cutClassEdge(name.get_edge()));
    }
    for (std::size_t row = 0; row < rule.cutclass2_names.size(); ++row) {
      for (std::size_t column = 0; column < rule.cutclass1_names.size(); ++column) {
        const auto source_cell = cutclass.get_cut_spacing(static_cast<int>(column), static_cast<int>(row));
        TechCutLef58SpacingTableCell cell;
        if (source_cell.get_cut_spacing1()) {
          cell.has_cut_spacing1 = true;
          cell.cut_spacing1 = *source_cell.get_cut_spacing1();
        }
        if (source_cell.get_cut_spacing2()) {
          cell.has_cut_spacing2 = true;
          cell.cut_spacing2 = *source_cell.get_cut_spacing2();
        }
        rule.cells.push_back(cell);
      }
    }
    static_cast<void>(storage.addLef58SpacingTableRule(owner, std::move(rule)));
  }

  for (const auto& source_rule : source.get_current_density_list()) {
    TechCutCurrentDensityRule rule{.signal = source_rule.get_kind() == ::idb::IdbCutCurrentDensity::Kind::kAc
                                                 ? TechCutCurrentDensitySignal::kAc
                                                 : TechCutCurrentDensitySignal::kDc,
                                   .type = cutCurrentDensityType(source_rule.get_type()),
                                   .scalar = source_rule.get_scalar(),
                                   .frequencies = source_rule.get_frequency_list(),
                                   .table_entries = source_rule.get_table_entries()};
    const auto& source_cut_areas = source_rule.get_cut_area_list();
    rule.cut_areas.assign(source_cut_areas.begin(), source_cut_areas.end());
    if (source_rule.has_scalar()) {
      rule.flags |= TechCutCurrentDensityRuleFlag::kHasScalar;
    }
    static_cast<void>(storage.addCurrentDensityRule(owner, std::move(rule)));
  }
}

}  // namespace

void IdbTechImporter::import(::idb::IdbLayout& source)
{
  if (_imported) {
    throw std::logic_error("legacy iDB technology importer is one-shot");
  }
  _imported = true;

  if (const auto* units = source.get_units(); units != nullptr) {
    _database.globalStorage().setUnits(importUnits(*units));
  }
  if (source.get_munufacture_grid() > 0) {
    _database.globalStorage().setManufacturingGrid(source.get_munufacture_grid());
  }

  for (auto* layer : source.get_layers()->get_layers()) {
    if (layer == nullptr) {
      continue;
    }

    TechLayerInfo info{.name = layer->get_name(), .lef58_type = lef58LayerType(layer->get_lef58_type())};
    if (layer->is_lef58_backside()) {
      info.flags |= TechLayerInfoFlag::kLef58Backside;
    }
    TechLayerId imported;
    switch (layer->get_type()) {
      case ::idb::IdbLayerType::kLayerRouting: {
        auto* routing = dynamic_cast<::idb::IdbLayerRouting*>(layer);
        if (routing == nullptr) {
          throw std::runtime_error("legacy ROUTING layer has an invalid dynamic type");
        }
        const auto id = _database.createRoutingLayer(info, importRoutingLayer(*routing));
        imported = TechLayerId{id.entity()};
        break;
      }
      case ::idb::IdbLayerType::kLayerCut: {
        auto* cut = dynamic_cast<::idb::IdbLayerCut*>(layer);
        if (cut == nullptr) {
          throw std::runtime_error("legacy CUT layer has an invalid dynamic type");
        }
        const auto id = _database.createCutLayer(info, importCutLayer(*cut));
        imported = TechLayerId{id.entity()};
        break;
      }
      case ::idb::IdbLayerType::kLayerImplant: {
        auto* implant = dynamic_cast<::idb::IdbLayerImplant*>(layer);
        if (implant == nullptr) {
          throw std::runtime_error("legacy IMPLANT layer has an invalid dynamic type");
        }
        TechImplantLayer component;
        if (implant->get_min_width() > 0) {
          component.flags |= TechImplantLayerFlag::kHasMinWidth;
          component.min_width = implant->get_min_width();
        }
        const auto id = _database.createImplantLayer(info, component);
        imported = TechLayerId{id.entity()};
        break;
      }
      case ::idb::IdbLayerType::kLayerMasterslice: {
        const auto id = _database.createMastersliceLayer(info, TechMastersliceLayer{.subtype = mastersliceType(layer->get_lef58_type())});
        imported = TechLayerId{id.entity()};
        break;
      }
      case ::idb::IdbLayerType::kLayerOverlap: {
        const auto id = _database.createOverlapLayer(info);
        imported = TechLayerId{id.entity()};
        break;
      }
      case ::idb::IdbLayerType::kNone:
      case ::idb::IdbLayerType::kMax:
        throw std::runtime_error("legacy iDB contains an unsupported technology layer type");
    }
    _layer_ids.emplace(layer, imported);
  }

  for (auto* layer : source.get_layers()->get_layers()) {
    if (layer == nullptr) {
      continue;
    }
    if (layer->get_type() == ::idb::IdbLayerType::kLayerRouting) {
      importRoutingRules(_database.routingLayerStorage(), TechRoutingLayerId{layerId(layer).entity()},
                         *dynamic_cast<::idb::IdbLayerRouting*>(layer));
    } else if (layer->get_type() == ::idb::IdbLayerType::kLayerCut) {
      importCutRules(_database.cutLayerStorage(), TechCutLayerId{layerId(layer).entity()}, *dynamic_cast<::idb::IdbLayerCut*>(layer));
    } else if (layer->get_type() == ::idb::IdbLayerType::kLayerImplant) {
      auto* implant = dynamic_cast<::idb::IdbLayerImplant*>(layer);
      const auto owner = TechImplantLayerId{layerId(layer).entity()};
      for (auto* source_rule : implant->get_min_spacing_list()->get_min_spacing_list()) {
        if (source_rule == nullptr || source_rule->get_min_spacing() <= 0) {
          continue;
        }
        TechImplantSpacingRule rule{.min_spacing = source_rule->get_min_spacing()};
        if (const auto* other_layer = source_rule->get_layer_2nd(); other_layer != nullptr) {
          const auto other = layerId(other_layer);
          if (!_database.techRegistry().registry().all_of<TechImplantLayer>(other.entity())) {
            throw std::runtime_error("legacy IMPLANT spacing references a non-IMPLANT layer");
          }
          rule.flags |= TechImplantSpacingRuleFlag::kHasOtherLayer;
          rule.other_layer = TechImplantLayerId{other.entity()};
        }
        _database.implantLayerStorage().addSpacingRule(owner, rule);
      }
    }
  }

  if (auto* source_stack = source.get_max_via_stack(); source_stack != nullptr) {
    TechMaxViaStack stack{.max_stack_count = source_stack->get_stacked_via_num()};
    if (source_stack->is_no_single()) {
      stack.flags |= TechMaxViaStackFlag::kNoSingle;
    }
    if (source_stack->is_range()) {
      const auto bottom = _database.findLayer(source_stack->get_layer_bottom());
      const auto top = _database.findLayer(source_stack->get_layer_top());
      const auto& registry = _database.techRegistry().registry();
      if (!bottom || !top || !registry.all_of<TechRoutingLayer>(bottom.entity()) || !registry.all_of<TechRoutingLayer>(top.entity())) {
        throw std::runtime_error("legacy MAXVIASTACK range does not reference routing layers");
      }
      stack.flags |= TechMaxViaStackFlag::kHasRange;
      stack.bottom_layer = TechRoutingLayerId{bottom.entity()};
      stack.top_layer = TechRoutingLayerId{top.entity()};
    }
    _database.globalStorage().setMaxViaStack(stack);
  }

  for (auto* source_rule : source.get_via_rule_list()->get_rule_list()) {
    if (source_rule == nullptr || source_rule->get_layer_bottom() == nullptr || source_rule->get_layer_cut() == nullptr
        || source_rule->get_layer_top() == nullptr) {
      // The legacy reader also creates name-only entries for ordinary
      // VIARULE statements, but does not materialize their payload.
      continue;
    }

    TechViaRuleGenerateBottomLayer bottom{.layer = conductorLayer(
                                               _database, layerId(source_rule->get_layer_bottom()), "legacy VIARULE bottom")};
    if (auto* enclosure = source_rule->get_enclosure_bottom();
        enclosure != nullptr && enclosure->get_overhang_1() >= 0 && enclosure->get_overhang_2() >= 0) {
      bottom.flags |= TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure;
      bottom.enclosure_overhang1 = enclosure->get_overhang_1();
      bottom.enclosure_overhang2 = enclosure->get_overhang_2();
    }

    TechViaRuleGenerateTopLayer top{.layer = conductorLayer(_database, layerId(source_rule->get_layer_top()), "legacy VIARULE top")};
    if (auto* enclosure = source_rule->get_enclosure_top();
        enclosure != nullptr && enclosure->get_overhang_1() >= 0 && enclosure->get_overhang_2() >= 0) {
      top.flags |= TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure;
      top.enclosure_overhang1 = enclosure->get_overhang_1();
      top.enclosure_overhang2 = enclosure->get_overhang_2();
    }

    TechViaRuleGenerateCutLayer cut{.layer = TechCutLayerId{layerId(source_rule->get_layer_cut()).entity()}};
    if (auto* source_rect = source_rule->get_cut_rect(); source_rect != nullptr) {
      const auto imported_rect = rect(*source_rect).normalized();
      if (imported_rect.hasArea()) {
        cut.flags |= TechViaRuleGenerateCutLayerFlag::kHasRect;
        cut.cut_rect = imported_rect;
      }
    }
    if (source_rule->get_spacing_x() >= 0 && source_rule->get_spacing_y() >= 0) {
      cut.flags |= TechViaRuleGenerateCutLayerFlag::kHasSpacing;
      cut.spacing_x = source_rule->get_spacing_x();
      cut.spacing_y = source_rule->get_spacing_y();
    }
    if (source_rule->get_resistance_per_cut() >= 0.0) {
      cut.flags |= TechViaRuleGenerateCutLayerFlag::kHasResistance;
      cut.resistance_per_cut = source_rule->get_resistance_per_cut();
    }

    static_cast<void>(_database.viaRuleGenerateStorage().createViaRuleGenerate(
        TechViaRuleGenerate{.name = source_rule->get_name()}, std::move(bottom), std::move(cut), std::move(top)));
  }

  for (auto* source_via : source.get_via_list()->get_via_list()) {
    if (source_via == nullptr || source_via->get_instance() == nullptr) {
      continue;
    }
    auto* source_master = source_via->get_instance();
    TechViaMaster master{.name = source_master->get_name()};
    if (source_master->is_default()) {
      master.flags |= TechViaMasterFlag::kDefault;
    }
    if (source_master->get_resistance() >= 0.0) {
      master.flags |= TechViaMasterFlag::kHasResistance;
      master.resistance = source_master->get_resistance();
    }

    if (source_master->is_generate()) {
      auto* source_generate = source_master->get_master_generate();
      if (source_generate == nullptr || source_generate->get_layer_bottom() == nullptr || source_generate->get_layer_cut() == nullptr
          || source_generate->get_layer_top() == nullptr) {
        throw std::runtime_error("legacy generated VIA is incomplete: " + source_master->get_name());
      }
      const auto rule = _database.viaRuleGenerateStorage().findViaRuleGenerate(source_generate->get_rule_name());
      if (!rule) {
        throw std::runtime_error("legacy generated VIA references an unavailable VIARULE: " + source_generate->get_rule_name());
      }

      TechGeneratedViaMaster generated{.via_rule_generate = rule,
                                       .cut_size_x = source_generate->get_cut_size_x(),
                                       .cut_size_y = source_generate->get_cut_size_y(),
                                       .cut_spacing_x = source_generate->get_cut_spcing_x(),
                                       .cut_spacing_y = source_generate->get_cut_spcing_y(),
                                       .bottom_enclosure_x = source_generate->get_enclosure_bottom_x(),
                                       .bottom_enclosure_y = source_generate->get_enclosure_bottom_y(),
                                       .top_enclosure_x = source_generate->get_enclosure_top_x(),
                                       .top_enclosure_y = source_generate->get_enclosure_top_y(),
                                       .row_count = static_cast<uint32_t>(source_generate->get_cut_rows()),
                                       .column_count = static_cast<uint32_t>(source_generate->get_cut_cols()),
                                       .origin_x = source_generate->get_original_offset_x(),
                                       .origin_y = source_generate->get_original_offset_y(),
                                       .bottom_offset_x = source_generate->get_offset_bottom_x(),
                                       .bottom_offset_y = source_generate->get_offset_bottom_y(),
                                       .top_offset_x = source_generate->get_offset_top_x(),
                                       .top_offset_y = source_generate->get_offset_top_y()};
      if (generated.row_count != 1u || generated.column_count != 1u) {
        generated.flags |= TechGeneratedViaMasterFlag::kHasRowCol;
      }
      if (generated.origin_x != 0 || generated.origin_y != 0) {
        generated.flags |= TechGeneratedViaMasterFlag::kHasOrigin;
      }
      if (generated.bottom_offset_x != 0 || generated.bottom_offset_y != 0 || generated.top_offset_x != 0
          || generated.top_offset_y != 0) {
        generated.flags |= TechGeneratedViaMasterFlag::kHasOffset;
      }
      if (auto* pattern = source_generate->get_patttern(); pattern != nullptr) {
        generated.flags |= TechGeneratedViaMasterFlag::kHasPattern;
        generated.pattern = pattern->get_pattern_string();
      }

      const auto bottom = conductorLayer(_database, layerId(source_generate->get_layer_bottom()), "legacy generated VIA bottom");
      const auto cut = TechCutLayerId{layerId(source_generate->get_layer_cut()).entity()};
      const auto top = conductorLayer(_database, layerId(source_generate->get_layer_top()), "legacy generated VIA top");
      auto shapes = materializeGeneratedVia(generated, bottom, cut, top);
      if (source_generate->get_patttern() != nullptr) {
        shapes.cut_geometry.rects.clear();
        shapes.cut_geometry.rects.reserve(source_generate->get_cut_rect_list().size());
        for (auto* source_rect : source_generate->get_cut_rect_list()) {
          if (source_rect != nullptr) {
            shapes.cut_geometry.rects.push_back(rect(*source_rect));
          }
        }
      }
      static_cast<void>(
          _database.viaMasterStorage().createGeneratedViaMaster(std::move(master), std::move(generated), std::move(shapes)));
      continue;
    }

    struct ConductorGeometry
    {
      TechConductorLayerRef layer;
      std::vector<Rect> rects;
    };
    std::vector<ConductorGeometry> conductors;
    TechCutLayerId cut;
    std::vector<Rect> cut_rects;
    for (auto* source_fixed : source_master->get_master_fixed_list()) {
      if (source_fixed == nullptr || source_fixed->get_layer() == nullptr) {
        continue;
      }
      const auto imported_layer = layerId(source_fixed->get_layer());
      auto imported_rects = std::vector<Rect>{};
      imported_rects.reserve(source_fixed->get_rect_list().size());
      for (auto* source_rect : source_fixed->get_rect_list()) {
        if (source_rect != nullptr) {
          imported_rects.push_back(rect(*source_rect));
        }
      }
      const auto& registry = _database.techRegistry().registry();
      if (registry.all_of<TechCutLayer>(imported_layer.entity())) {
        const auto typed = TechCutLayerId{imported_layer.entity()};
        if (cut && cut != typed) {
          throw std::runtime_error("legacy fixed VIA uses more than one CUT layer: " + source_master->get_name());
        }
        cut = typed;
        cut_rects.insert(cut_rects.end(), imported_rects.begin(), imported_rects.end());
      } else {
        const auto conductor = conductorLayer(_database, imported_layer, "legacy fixed VIA geometry");
        auto found = std::find_if(conductors.begin(), conductors.end(),
                                  [&](const ConductorGeometry& item) { return item.layer == conductor; });
        if (found == conductors.end()) {
          conductors.push_back(ConductorGeometry{.layer = conductor, .rects = std::move(imported_rects)});
        } else {
          found->rects.insert(found->rects.end(), imported_rects.begin(), imported_rects.end());
        }
      }
    }
    if (!cut || conductors.size() != 2u) {
      throw std::runtime_error("legacy fixed VIA requires two conductor layers and one CUT layer: " + source_master->get_name());
    }
    const auto first_position = _database.layerPosition(conductors[0].layer.layer());
    const auto second_position = _database.layerPosition(conductors[1].layer.layer());
    if (!first_position || !second_position || *first_position == *second_position) {
      throw std::runtime_error("legacy fixed VIA conductor layers are absent from the physical layer sequence: "
                               + source_master->get_name());
    }
    if (*second_position < *first_position) {
      std::swap(conductors[0], conductors[1]);
    }
    TechViaMasterShapeInput shapes{.bottom_layer = conductors[0].layer,
                                   .bottom_geometry = {.rects = std::move(conductors[0].rects)},
                                   .cut_layer = cut,
                                   .cut_geometry = {.rects = std::move(cut_rects)},
                                   .top_layer = conductors[1].layer,
                                   .top_geometry = {.rects = std::move(conductors[1].rects)}};
    static_cast<void>(_database.viaMasterStorage().createFixedViaMaster(std::move(master), std::move(shapes)));
  }
}

TechLayerId IdbTechImporter::layerId(const ::idb::IdbLayer* source) const
{
  const auto found = _layer_ids.find(source);
  if (found == _layer_ids.end()) {
    throw std::out_of_range("legacy iDB layer was not imported");
  }
  return found->second;
}

TechViaMasterId IdbTechImporter::viaMasterId(std::string_view name) const
{
  const auto id = _database.viaMasterStorage().findViaMaster(name);
  if (!id) {
    throw std::out_of_range("legacy iDB VIA master was not imported: " + std::string{name});
  }
  return id;
}

}  // namespace eccdb
