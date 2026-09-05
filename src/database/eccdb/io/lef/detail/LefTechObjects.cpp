#include "lef/detail/LefTechObjects.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lefiNonDefault.hpp"
#include "lefiVia.hpp"
#include "lefiViaRule.hpp"
#include "tech/TechStore.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/masterslice_layer/model/MastersliceLayerComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "tech/via_rule/model/ViaRuleComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb::lef_detail {
namespace {

std::string requiredText(const char* value, const char* field)
{
  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(std::string(field) + " is required");
  }
  return value;
}

std::string numberText(double value)
{
  std::ostringstream stream;
  stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return stream.str();
}

TechProperty viaProperty(const lefiVia& source, int index)
{
  const auto* value = source.propValue(index);
  return TechProperty{.name = requiredText(source.propName(index), "VIA PROPERTY name"),
                      .value = value == nullptr ? numberText(source.propNumber(index)) : std::string(value)};
}

TechProperty viaRuleProperty(const lefiViaRule& source, int index)
{
  const auto* value = source.propValue(index);
  return TechProperty{.name = requiredText(source.propName(index), "VIARULE PROPERTY name"),
                      .value = value == nullptr ? numberText(source.propNumber(index)) : std::string(value)};
}

TechProperty ndrProperty(const lefiNonDefault& source, int index)
{
  const auto* value = source.propValue(index);
  return TechProperty{.name = requiredText(source.propName(index), "NONDEFAULTRULE PROPERTY name"),
                      .value = value == nullptr ? numberText(source.propNumber(index)) : std::string(value)};
}

int32_t toDatabaseUnits(double value, int32_t units, const char* field, bool allow_negative = false)
{
  if (units <= 0) {
    throw std::invalid_argument("database units per micron must be positive");
  }
  if (!std::isfinite(value) || (!allow_negative && value < 0.0)) {
    throw std::runtime_error(std::string(field) + (allow_negative ? " must be finite" : " must be non-negative"));
  }
  const auto scaled = value * static_cast<double>(units);
  if (!std::isfinite(scaled) || scaled < static_cast<double>(std::numeric_limits<int32_t>::min())
      || scaled > static_cast<double>(std::numeric_limits<int32_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside int32 DBU range");
  }
  return static_cast<int32_t>(std::llround(scaled));
}

Rect prepareRect(const StagedRect& source, int32_t units, const char* field)
{
  return Rect{.ll_x = toDatabaseUnits(source.ll_x, units, field, true),
              .ll_y = toDatabaseUnits(source.ll_y, units, field, true),
              .ur_x = toDatabaseUnits(source.ur_x, units, field, true),
              .ur_y = toDatabaseUnits(source.ur_y, units, field, true)}
      .normalized();
}

GeometryPolygonInput preparePolygon(const StagedPolygon& source, int32_t units, const char* field)
{
  if (source.points.size() < 3u) {
    throw std::runtime_error(std::string(field) + " requires at least three points");
  }
  GeometryPolygonInput result;
  result.points.reserve(source.points.size());
  for (const auto [x, y] : source.points) {
    result.points.push_back(Point{.x = toDatabaseUnits(x, units, field, true), .y = toDatabaseUnits(y, units, field, true)});
  }
  return result;
}

RoutingDirection direction(const lefiViaRuleLayer& source)
{
  if (!source.hasDirection()) {
    return RoutingDirection::kUnknown;
  }
  if (source.isHorizontal()) {
    return RoutingDirection::kHorizontal;
  }
  if (source.isVertical()) {
    return RoutingDirection::kVertical;
  }
  return RoutingDirection::kUnknown;
}

StagedViaRuleLayer stageViaRuleLayer(const lefiViaRuleLayer& source)
{
  StagedViaRuleLayer result{.name = requiredText(source.name(), "VIARULE LAYER name"), .direction = direction(source)};
  if (source.hasWidth()) {
    result.width = std::pair{source.widthMin(), source.widthMax()};
  }
  if (source.hasEnclosure()) {
    result.enclosure = std::pair{source.enclosureOverhang1(), source.enclosureOverhang2()};
  }
  if (source.hasSpacing()) {
    result.spacing = std::pair{source.spacingStepX(), source.spacingStepY()};
  }
  if (source.hasRect()) {
    result.rect = StagedRect{source.xl(), source.yl(), source.xh(), source.yh()};
  }
  if (source.hasResistance()) {
    result.resistance = source.resistance();
  }
  if (source.hasOverhang()) {
    result.overhang = source.overhang();
  }
  if (source.hasMetalOverhang()) {
    result.metal_overhang = source.metalOverhang();
  }
  return result;
}

PreparedViaRuleLayer prepareViaRuleLayer(const StagedViaRuleLayer& source, int32_t units)
{
  PreparedViaRuleLayer result{.name = source.name, .direction = source.direction};
  if (source.width) {
    result.flags |= TechViaRuleGenerateRoutingLayerFlag::kHasWidth;
    result.min_width = toDatabaseUnits(source.width->first, units, "VIARULE WIDTH minimum");
    result.max_width = toDatabaseUnits(source.width->second, units, "VIARULE WIDTH maximum");
  }
  if (source.enclosure) {
    result.flags |= TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure;
    result.enclosure_overhang1 = toDatabaseUnits(source.enclosure->first, units, "VIARULE ENCLOSURE first overhang");
    result.enclosure_overhang2 = toDatabaseUnits(source.enclosure->second, units, "VIARULE ENCLOSURE second overhang");
  }
  if (source.spacing) {
    result.flags |= TechViaRuleGenerateCutLayerFlag::kHasSpacing;
    result.spacing_x = toDatabaseUnits(source.spacing->first, units, "VIARULE SPACING X");
    result.spacing_y = toDatabaseUnits(source.spacing->second, units, "VIARULE SPACING Y");
  }
  if (source.rect) {
    result.flags |= TechViaRuleGenerateCutLayerFlag::kHasRect;
    result.rect = prepareRect(*source.rect, units, "VIARULE RECT");
  }
  if (source.resistance) {
    if (!std::isfinite(*source.resistance) || *source.resistance < 0.0) {
      throw std::runtime_error("VIARULE RESISTANCE must be non-negative");
    }
    result.flags |= TechViaRuleGenerateCutLayerFlag::kHasResistance;
    result.resistance = *source.resistance;
  }
  if (source.overhang) {
    result.flags |= TechViaRuleGenerateRoutingLayerFlag::kHasOverhang;
    result.overhang = toDatabaseUnits(*source.overhang, units, "VIARULE OVERHANG");
  }
  if (source.metal_overhang) {
    result.flags |= TechViaRuleGenerateRoutingLayerFlag::kHasMetalOverhang;
    result.metal_overhang = toDatabaseUnits(*source.metal_overhang, units, "VIARULE METALOVERHANG");
  }
  return result;
}

TechLayerId requireLayer(const TechStore& database, const std::string& name, const char* field)
{
  const auto layer = database.findLayer(name);
  if (!layer) {
    throw std::runtime_error(std::string(field) + " references undefined LAYER " + name);
  }
  return layer;
}

TechRoutingLayerId requireRoutingLayer(const TechStore& database, const std::string& name, const char* field)
{
  const auto layer = requireLayer(database, name, field);
  if (!database.techRegistry().registry().all_of<TechRoutingLayer>(layer.entity())) {
    throw std::runtime_error(std::string(field) + " requires ROUTING LAYER " + name);
  }
  return TechRoutingLayerId{layer.entity()};
}

TechCutLayerId requireCutLayer(const TechStore& database, const std::string& name, const char* field)
{
  const auto layer = requireLayer(database, name, field);
  if (!database.techRegistry().registry().all_of<TechCutLayer>(layer.entity())) {
    throw std::runtime_error(std::string(field) + " requires CUT LAYER " + name);
  }
  return TechCutLayerId{layer.entity()};
}

TechConductorLayerRef requireConductorLayer(const TechStore& database, const std::string& name, const char* field)
{
  const auto layer = requireLayer(database, name, field);
  const auto& registry = database.techRegistry().registry();
  if (registry.all_of<TechRoutingLayer>(layer.entity())) {
    return TechConductorLayerRef{TechRoutingLayerId{layer.entity()}};
  }
  if (registry.all_of<TechMastersliceLayer>(layer.entity())) {
    return TechConductorLayerRef{TechMastersliceLayerId{layer.entity()}};
  }
  throw std::runtime_error(std::string(field) + " requires a ROUTING or MASTERSLICE LAYER " + name);
}

int32_t checkedCoordinate(int64_t value, const char* field)
{
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    throw std::runtime_error(std::string(field) + " is outside int32 DBU range");
  }
  return static_cast<int32_t>(value);
}

int hexValue(char value)
{
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

std::optional<std::vector<bool>> decodeCutPattern(std::string_view pattern, uint32_t row_count, uint32_t column_count)
{
  if (pattern.empty()) return std::nullopt;
  std::vector<std::string_view> tokens;
  for (size_t begin = 0; begin <= pattern.size();) {
    const auto end = pattern.find('_', begin);
    const auto token = pattern.substr(begin, end == std::string_view::npos ? pattern.size() - begin : end - begin);
    if (!token.empty()) tokens.push_back(token);
    if (end == std::string_view::npos) break;
    begin = end + 1u;
  }
  if (tokens.empty() || tokens.size() % 2u != 0u) return std::nullopt;

  std::vector<bool> result(static_cast<size_t>(row_count) * column_count, false);
  uint32_t row = 0;
  for (size_t token_index = 0; token_index < tokens.size(); token_index += 2u) {
    uint32_t repeated_rows = 0;
    for (const auto digit : tokens[token_index]) {
      const auto value = hexValue(digit);
      if (value < 0 || repeated_rows > (std::numeric_limits<uint32_t>::max() - static_cast<uint32_t>(value)) / 16u) {
        return std::nullopt;
      }
      repeated_rows = repeated_rows * 16u + static_cast<uint32_t>(value);
    }

    std::vector<bool> row_bits;
    const auto encoded = tokens[token_index + 1u];
    for (size_t index = 0; index < encoded.size();) {
      uint32_t repeat = 1;
      char nibble = encoded[index++];
      if (nibble == 'R') {
        if (index + 1u >= encoded.size()) return std::nullopt;
        const auto count = hexValue(encoded[index++]);
        if (count < 0) return std::nullopt;
        repeat = static_cast<uint32_t>(count);
        nibble = encoded[index++];
      }
      const auto value = hexValue(nibble);
      if (value < 0) return std::nullopt;
      for (uint32_t copy = 0; copy < repeat; ++copy) {
        for (int bit = 3; bit >= 0; --bit) row_bits.push_back((value & (1 << bit)) != 0);
      }
    }
    for (uint32_t copy = 0; copy < repeated_rows && row < row_count; ++copy, ++row) {
      for (uint32_t column = 0; column < column_count && column < row_bits.size(); ++column) {
        result[static_cast<size_t>(row) * column_count + column] = row_bits[column];
      }
    }
  }
  return result;
}

TechViaMasterShapeInput materializeGeneratedVia(const TechStore& database, const PreparedGeneratedVia& source)
{
  if (source.cut_size_x <= 0 || source.cut_size_y <= 0 || source.row_count == 0 || source.column_count == 0) {
    throw std::runtime_error("generated VIA requires positive CUTSIZE and ROWCOL");
  }
  const auto total_x = static_cast<int64_t>(source.column_count) * source.cut_size_x
                       + static_cast<int64_t>(source.column_count - 1u) * source.cut_spacing_x;
  const auto total_y
      = static_cast<int64_t>(source.row_count) * source.cut_size_y + static_cast<int64_t>(source.row_count - 1u) * source.cut_spacing_y;
  const auto dx = total_x / 2;
  const auto dy = total_y / 2;

  TechViaMasterShapeInput shapes;
  shapes.bottom_layer = requireConductorLayer(database, source.bottom_layer, "generated VIA bottom layer");
  shapes.cut_layer = requireCutLayer(database, source.cut_layer, "generated VIA cut layer");
  shapes.top_layer = requireConductorLayer(database, source.top_layer, "generated VIA top layer");
  shapes.cut_geometry.rects.reserve(static_cast<size_t>(source.row_count) * source.column_count);
  const auto pattern = decodeCutPattern(source.pattern, source.row_count, source.column_count);
  for (uint32_t row = 0; row < source.row_count; ++row) {
    for (uint32_t column = 0; column < source.column_count; ++column) {
      if (pattern && !(*pattern)[static_cast<size_t>(row) * source.column_count + column]) {
        continue;
      }
      const auto x = static_cast<int64_t>(column) * (source.cut_size_x + source.cut_spacing_x) - dx + source.origin_x;
      const auto y = static_cast<int64_t>(row) * (source.cut_size_y + source.cut_spacing_y) - dy + source.origin_y;
      shapes.cut_geometry.rects.push_back(Rect{checkedCoordinate(x, "generated VIA cut X"), checkedCoordinate(y, "generated VIA cut Y"),
                                               checkedCoordinate(x + source.cut_size_x, "generated VIA cut X"),
                                               checkedCoordinate(y + source.cut_size_y, "generated VIA cut Y")});
    }
  }

  const auto min_x = -dx + source.origin_x;
  const auto min_y = -dy + source.origin_y;
  const auto max_x = total_x - dx + source.origin_x;
  const auto max_y = total_y - dy + source.origin_y;
  shapes.bottom_geometry.rects.push_back(
      Rect{checkedCoordinate(min_x - source.bottom_enclosure_x + source.bottom_offset_x, "generated VIA bottom X"),
           checkedCoordinate(min_y - source.bottom_enclosure_y + source.bottom_offset_y, "generated VIA bottom Y"),
           checkedCoordinate(max_x + source.bottom_enclosure_x + source.bottom_offset_x, "generated VIA bottom X"),
           checkedCoordinate(max_y + source.bottom_enclosure_y + source.bottom_offset_y, "generated VIA bottom Y")});
  shapes.top_geometry.rects.push_back(Rect{checkedCoordinate(min_x - source.top_enclosure_x + source.top_offset_x, "generated VIA top X"),
                                           checkedCoordinate(min_y - source.top_enclosure_y + source.top_offset_y, "generated VIA top Y"),
                                           checkedCoordinate(max_x + source.top_enclosure_x + source.top_offset_x, "generated VIA top X"),
                                           checkedCoordinate(max_y + source.top_enclosure_y + source.top_offset_y, "generated VIA top Y")});
  return shapes;
}

TechViaMasterShapeInput fixedViaShapes(const TechStore& database, const PreparedViaMaster& source)
{
  struct ConductorShapes
  {
    TechConductorLayerRef layer;
    std::vector<Rect> rects;
    std::vector<GeometryPolygonInput> polygons;
  };
  std::vector<ConductorShapes> conductors;
  TechCutLayerId cut;
  std::vector<Rect> cut_rects;
  std::vector<GeometryPolygonInput> cut_polygons;

  for (const auto& clause : source.layers) {
    const auto layer = requireLayer(database, clause.name, "fixed VIA geometry");
    const auto& registry = database.techRegistry().registry();
    if (registry.all_of<TechCutLayer>(layer.entity())) {
      const auto typed = TechCutLayerId{layer.entity()};
      if (cut && cut != typed) {
        throw std::runtime_error("fixed VIA must use exactly one CUT layer");
      }
      cut = typed;
      cut_rects.insert(cut_rects.end(), clause.rects.begin(), clause.rects.end());
      cut_polygons.insert(cut_polygons.end(), clause.polygons.begin(), clause.polygons.end());
      continue;
    }

    const auto conductor = requireConductorLayer(database, clause.name, "fixed VIA geometry");
    auto found = std::find_if(conductors.begin(), conductors.end(), [&](const ConductorShapes& item) { return item.layer == conductor; });
    if (found == conductors.end()) {
      conductors.push_back(ConductorShapes{.layer = conductor, .rects = clause.rects, .polygons = clause.polygons});
    } else {
      found->rects.insert(found->rects.end(), clause.rects.begin(), clause.rects.end());
      found->polygons.insert(found->polygons.end(), clause.polygons.begin(), clause.polygons.end());
    }
  }
  if (!cut || conductors.size() != 2u) {
    throw std::runtime_error("fixed VIA requires two conductor layers and one CUT layer");
  }
  const auto first_position = database.layerPosition(conductors[0].layer.layer());
  const auto second_position = database.layerPosition(conductors[1].layer.layer());
  if (!first_position || !second_position || *first_position == *second_position) {
    throw std::runtime_error("fixed VIA conductor layers are absent from the physical layer sequence");
  }
  if (*second_position < *first_position) {
    std::swap(conductors[0], conductors[1]);
  }
  return TechViaMasterShapeInput{
      .bottom_layer = conductors[0].layer,
      .bottom_geometry = {.rects = std::move(conductors[0].rects), .polygons = std::move(conductors[0].polygons)},
      .cut_layer = cut,
      .cut_geometry = {.rects = std::move(cut_rects), .polygons = std::move(cut_polygons)},
      .top_layer = conductors[1].layer,
      .top_geometry = {.rects = std::move(conductors[1].rects), .polygons = std::move(conductors[1].polygons)}};
}

void commitViaGenerateRules(TechStore& database, const std::vector<PreparedViaRule>& rules)
{
  for (const auto& source : rules) {
    if (!source.generate) {
      continue;
    }
    const PreparedViaRuleLayer* cut_source = nullptr;
    std::vector<const PreparedViaRuleLayer*> conductor_sources;
    for (const auto& layer_source : source.layers) {
      const auto layer = requireLayer(database, layer_source.name, "VIARULE GENERATE");
      const auto& registry = database.techRegistry().registry();
      if (registry.all_of<TechCutLayer>(layer.entity())) {
        if (cut_source != nullptr) {
          throw std::runtime_error("VIARULE GENERATE has more than one CUT layer");
        }
        cut_source = &layer_source;
      } else if (registry.all_of<TechRoutingLayer>(layer.entity()) || registry.all_of<TechMastersliceLayer>(layer.entity())) {
        conductor_sources.push_back(&layer_source);
      } else {
        throw std::runtime_error("VIARULE GENERATE layers must be ROUTING/MASTERSLICE/CUT");
      }
    }
    if (cut_source == nullptr || conductor_sources.size() != 2u) {
      throw std::runtime_error("VIARULE GENERATE requires two conductor layers and one CUT layer");
    }
    const auto first = requireConductorLayer(database, conductor_sources[0]->name, "VIARULE GENERATE");
    const auto second = requireConductorLayer(database, conductor_sources[1]->name, "VIARULE GENERATE");
    if (!database.isBelow(first.layer(), second.layer())) {
      std::swap(conductor_sources[0], conductor_sources[1]);
    }
    const auto bottom_id = requireConductorLayer(database, conductor_sources[0]->name, "VIARULE GENERATE bottom");
    const auto top_id = requireConductorLayer(database, conductor_sources[1]->name, "VIARULE GENERATE top");

    const auto conductorComponent = [](const PreparedViaRuleLayer& layer, TechConductorLayerRef id) {
      TechViaRuleGenerateBottomLayer result{
          .layer = id,
          .flags = layer.flags
                   & (TechViaRuleGenerateRoutingLayerFlag::kHasDirection | TechViaRuleGenerateRoutingLayerFlag::kHasEnclosure
                      | TechViaRuleGenerateRoutingLayerFlag::kHasWidth | TechViaRuleGenerateRoutingLayerFlag::kHasOverhang
                      | TechViaRuleGenerateRoutingLayerFlag::kHasMetalOverhang),
          .enclosure_overhang1 = layer.enclosure_overhang1,
          .enclosure_overhang2 = layer.enclosure_overhang2,
          .min_width = layer.min_width,
          .max_width = layer.max_width,
          .overhang = layer.overhang,
          .metal_overhang = layer.metal_overhang};
      if (layer.direction == RoutingDirection::kHorizontal) {
        result.direction = TechRoutingDirection::kHorizontal;
      } else if (layer.direction == RoutingDirection::kVertical) {
        result.direction = TechRoutingDirection::kVertical;
      }
      return result;
    };
    auto bottom = conductorComponent(*conductor_sources[0], bottom_id);
    const auto top_base = conductorComponent(*conductor_sources[1], top_id);
    TechViaRuleGenerateTopLayer top{.layer = top_base.layer,
                                    .flags = top_base.flags,
                                    .direction = top_base.direction,
                                    .enclosure_overhang1 = top_base.enclosure_overhang1,
                                    .enclosure_overhang2 = top_base.enclosure_overhang2,
                                    .min_width = top_base.min_width,
                                    .max_width = top_base.max_width,
                                    .overhang = top_base.overhang,
                                    .metal_overhang = top_base.metal_overhang};
    TechViaRuleGenerateCutLayer cut{.layer = requireCutLayer(database, cut_source->name, "VIARULE GENERATE cut"),
                                    .flags = cut_source->flags
                                             & (TechViaRuleGenerateCutLayerFlag::kHasRect | TechViaRuleGenerateCutLayerFlag::kHasSpacing
                                                | TechViaRuleGenerateCutLayerFlag::kHasResistance),
                                    .cut_rect = cut_source->rect,
                                    .spacing_x = cut_source->spacing_x,
                                    .spacing_y = cut_source->spacing_y,
                                    .resistance_per_cut = cut_source->resistance};
    TechViaRuleGenerate rule{.name = source.name, .properties = source.properties};
    if (source.is_default) {
      rule.flags |= TechViaRuleGenerateFlag::kDefault;
    }
    static_cast<void>(
        database.viaRuleGenerateStorage().createViaRuleGenerate(std::move(rule), std::move(bottom), std::move(cut), std::move(top)));
  }
}

void commitVias(TechStore& database, const std::vector<PreparedViaMaster>& vias)
{
  for (const auto& source : vias) {
    TechViaMaster master{.name = source.name, .flags = source.flags, .resistance = source.resistance, .properties = source.properties};
    if (!source.generated) {
      static_cast<void>(database.viaMasterStorage().createFixedViaMaster(std::move(master), fixedViaShapes(database, source)));
      continue;
    }
    const auto& staged = *source.generated;
    const auto generate_rule = database.viaRuleGenerateStorage().findViaRuleGenerate(staged.via_rule);
    if (!generate_rule) {
      throw std::runtime_error("generated VIA references undefined VIARULE GENERATE " + staged.via_rule);
    }
    TechGeneratedViaMaster generated{.via_rule_generate = generate_rule,
                                     .flags = staged.flags,
                                     .cut_size_x = staged.cut_size_x,
                                     .cut_size_y = staged.cut_size_y,
                                     .cut_spacing_x = staged.cut_spacing_x,
                                     .cut_spacing_y = staged.cut_spacing_y,
                                     .bottom_enclosure_x = staged.bottom_enclosure_x,
                                     .bottom_enclosure_y = staged.bottom_enclosure_y,
                                     .top_enclosure_x = staged.top_enclosure_x,
                                     .top_enclosure_y = staged.top_enclosure_y,
                                     .row_count = staged.row_count,
                                     .column_count = staged.column_count,
                                     .origin_x = staged.origin_x,
                                     .origin_y = staged.origin_y,
                                     .bottom_offset_x = staged.bottom_offset_x,
                                     .bottom_offset_y = staged.bottom_offset_y,
                                     .top_offset_x = staged.top_offset_x,
                                     .top_offset_y = staged.top_offset_y,
                                     .pattern = staged.pattern};
    auto shapes = materializeGeneratedVia(database, staged);
    static_cast<void>(database.viaMasterStorage().createGeneratedViaMaster(std::move(master), std::move(generated), std::move(shapes)));
  }
}

void commitOrdinaryViaRules(TechStore& database, const std::vector<PreparedViaRule>& rules)
{
  for (const auto& source : rules) {
    if (source.generate) {
      continue;
    }
    if (source.layers.size() != 2u) {
      throw std::runtime_error("ordinary VIARULE requires exactly two ROUTING layers");
    }
    const PreparedViaRuleLayer* lower_source = &source.layers[0];
    const PreparedViaRuleLayer* upper_source = &source.layers[1];
    auto lower_id = requireRoutingLayer(database, lower_source->name, "ordinary VIARULE");
    auto upper_id = requireRoutingLayer(database, upper_source->name, "ordinary VIARULE");
    if (!database.isBelow(TechLayerId{lower_id.entity()}, TechLayerId{upper_id.entity()})) {
      std::swap(lower_source, upper_source);
      std::swap(lower_id, upper_id);
    }
    const auto makeFlags = [](const PreparedViaRuleLayer& layer) {
      return (layer.flags & TechViaRuleGenerateRoutingLayerFlag::kHasWidth) != 0u ? TechViaRuleLayerFlag::kHasWidth : 0u;
    };
    TechViaRuleLowerLayer lower{.layer = lower_id,
                                .flags = makeFlags(*lower_source),
                                .direction = lower_source->direction,
                                .min_width = lower_source->min_width,
                                .max_width = lower_source->max_width};
    TechViaRuleUpperLayer upper{.layer = upper_id,
                                .flags = makeFlags(*upper_source),
                                .direction = upper_source->direction,
                                .min_width = upper_source->min_width,
                                .max_width = upper_source->max_width};
    std::vector<TechViaMasterId> candidates;
    candidates.reserve(source.candidates.size());
    for (const auto& name : source.candidates) {
      const auto via = database.viaMasterStorage().findViaMaster(name);
      if (!via) {
        throw std::runtime_error("ordinary VIARULE references undefined VIA " + name);
      }
      candidates.push_back(via);
    }
    std::vector<TechViaRuleProperty> properties;
    properties.reserve(source.properties.size());
    for (const auto& property : source.properties) {
      properties.push_back(TechViaRuleProperty{.name = property.name, .value = property.value});
    }
    static_cast<void>(database.viaRuleStorage().createViaRule(TechViaRule{.name = source.name}, std::move(lower), std::move(upper),
                                                              std::move(candidates), std::move(properties)));
  }
}

void commitNonDefaultRules(TechStore& database, const std::vector<PreparedNonDefaultRule>& rules)
{
  auto& storage = database.nonDefaultRuleStorage();
  for (const auto& source : rules) {
    TechNonDefaultRule rule{.name = source.name};
    if (source.hard_spacing) {
      rule.flags |= TechNonDefaultRuleFlag::kHardSpacing;
    }
    const auto owner = storage.createNonDefaultRule(std::move(rule));
    for (const auto& source_rule : source.routing_rules) {
      TechNdrRoutingRule target{.layer = requireRoutingLayer(database, source_rule.layer, "NONDEFAULTRULE LAYER"),
                                .flags = source_rule.flags,
                                .width = source_rule.width,
                                .diag_width = source_rule.diag_width,
                                .spacing = source_rule.spacing,
                                .wire_extension = source_rule.wire_extension,
                                .resistance = source_rule.resistance,
                                .capacitance = source_rule.capacitance,
                                .edge_capacitance = source_rule.edge_capacitance};
      storage.setRoutingRule(owner, target);
    }
    for (const auto& source_rule : source.min_cuts) {
      storage.setMinCutsRule(owner, TechNdrMinCutsRule{.layer = requireCutLayer(database, source_rule.layer, "NONDEFAULTRULE MINCUTS"),
                                                       .cut_count = source_rule.cut_count});
    }
    for (const auto& source_via : source.vias) {
      TechViaMaster master{
          .name = source_via.name, .flags = source_via.flags, .resistance = source_via.resistance, .properties = source_via.properties};
      if (!source_via.generated) {
        static_cast<void>(storage.addFixedViaDefinition(owner, std::move(master), fixedViaShapes(database, source_via)));
        continue;
      }
      const auto& staged = *source_via.generated;
      const auto generate_rule = database.viaRuleGenerateStorage().findViaRuleGenerate(staged.via_rule);
      if (!generate_rule) {
        throw std::runtime_error("generated NONDEFAULTRULE VIA references undefined VIARULE GENERATE " + staged.via_rule);
      }
      TechGeneratedViaMaster generated{.via_rule_generate = generate_rule,
                                       .flags = staged.flags,
                                       .cut_size_x = staged.cut_size_x,
                                       .cut_size_y = staged.cut_size_y,
                                       .cut_spacing_x = staged.cut_spacing_x,
                                       .cut_spacing_y = staged.cut_spacing_y,
                                       .bottom_enclosure_x = staged.bottom_enclosure_x,
                                       .bottom_enclosure_y = staged.bottom_enclosure_y,
                                       .top_enclosure_x = staged.top_enclosure_x,
                                       .top_enclosure_y = staged.top_enclosure_y,
                                       .row_count = staged.row_count,
                                       .column_count = staged.column_count,
                                       .origin_x = staged.origin_x,
                                       .origin_y = staged.origin_y,
                                       .bottom_offset_x = staged.bottom_offset_x,
                                       .bottom_offset_y = staged.bottom_offset_y,
                                       .top_offset_x = staged.top_offset_x,
                                       .top_offset_y = staged.top_offset_y,
                                       .pattern = staged.pattern};
      static_cast<void>(
          storage.addGeneratedViaDefinition(owner, std::move(master), std::move(generated), materializeGeneratedVia(database, staged)));
    }
    for (const auto& name : source.use_vias) {
      auto via = database.viaMasterStorage().findViaMaster(name);
      if (!via) {
        via = storage.findViaDefinition(name);
      }
      if (!via) {
        throw std::runtime_error("NONDEFAULTRULE USEVIA references undefined VIA " + name);
      }
      storage.addUseVia(owner, via);
    }
    for (const auto& name : source.use_via_rules) {
      const auto via_rule = database.viaRuleGenerateStorage().findViaRuleGenerate(name);
      if (!via_rule) {
        throw std::runtime_error("NONDEFAULTRULE USEVIARULE references undefined VIARULE GENERATE " + name);
      }
      storage.addUseViaRule(owner, via_rule);
    }
    for (const auto& source_rule : source.same_net_spacing_rules) {
      TechNdrSameNetSpacingRule target{.first_layer = requireLayer(database, source_rule.first_layer, "NONDEFAULTRULE SPACING"),
                                       .second_layer = requireLayer(database, source_rule.second_layer, "NONDEFAULTRULE SPACING"),
                                       .spacing = source_rule.spacing};
      if (source_rule.stack) {
        target.flags |= TechNdrSameNetSpacingRuleFlag::kStack;
      }
      storage.addSameNetSpacingRule(owner, target);
    }
    for (const auto& property : source.properties) {
      storage.addProperty(owner, TechNdrProperty{.name = property.name, .value = property.value});
    }
  }
}

}  // namespace

StagedViaMaster stageVia(const lefiVia& source)
{
  if (source.hasForeign()) {
    throw std::runtime_error("VIA FOREIGN is not represented by the current technology model");
  }
  StagedViaMaster result{.name = requiredText(source.name(), "VIA name")};
  if (source.hasDefault()) {
    result.flags |= TechViaMasterFlag::kDefault;
  }
  if (source.hasTopOfStack()) {
    result.flags |= TechViaMasterFlag::kTopOfStackOnly;
  }
  if (source.hasResistance()) {
    result.flags |= TechViaMasterFlag::kHasResistance;
    result.resistance = source.resistance();
  }
  result.properties.reserve(static_cast<size_t>(source.numProperties()));
  for (int index = 0; index < source.numProperties(); ++index) {
    result.properties.push_back(viaProperty(source, index));
  }

  result.layers.reserve(static_cast<size_t>(source.numLayers()));
  for (int layer_index = 0; layer_index < source.numLayers(); ++layer_index) {
    StagedViaLayer layer{.name = requiredText(source.layerName(layer_index), "VIA LAYER name")};
    layer.rects.reserve(static_cast<size_t>(source.numRects(layer_index)));
    for (int rect_index = 0; rect_index < source.numRects(layer_index); ++rect_index) {
      if (source.rectColorMask(layer_index, rect_index) != 0) {
        throw std::runtime_error("VIA RECT MASK is not represented by the current geometry model");
      }
      layer.rects.push_back(StagedRect{source.xl(layer_index, rect_index), source.yl(layer_index, rect_index),
                                       source.xh(layer_index, rect_index), source.yh(layer_index, rect_index)});
    }
    layer.polygons.reserve(static_cast<size_t>(source.numPolygons(layer_index)));
    for (int polygon_index = 0; polygon_index < source.numPolygons(layer_index); ++polygon_index) {
      if (source.polyColorMask(layer_index, polygon_index) != 0) {
        throw std::runtime_error("VIA POLYGON MASK is not represented by the current geometry model");
      }
      const auto polygon = source.getPolygon(layer_index, polygon_index);
      if (polygon.numPoints < 3 || polygon.x == nullptr || polygon.y == nullptr) {
        throw std::runtime_error("VIA POLYGON requires at least three points");
      }
      StagedPolygon staged;
      staged.points.reserve(static_cast<size_t>(polygon.numPoints));
      for (int point_index = 0; point_index < polygon.numPoints; ++point_index) {
        staged.points.emplace_back(polygon.x[point_index], polygon.y[point_index]);
      }
      layer.polygons.push_back(std::move(staged));
    }
    result.layers.push_back(std::move(layer));
  }
  if (source.hasViaRule()) {
    StagedGeneratedVia generated{.via_rule = requiredText(source.viaRuleName(), "generated VIA VIARULE"),
                                 .bottom_layer = requiredText(source.botMetalLayer(), "generated VIA bottom LAYER"),
                                 .cut_layer = requiredText(source.cutLayer(), "generated VIA cut LAYER"),
                                 .top_layer = requiredText(source.topMetalLayer(), "generated VIA top LAYER"),
                                 .cut_size_x = source.xCutSize(),
                                 .cut_size_y = source.yCutSize(),
                                 .cut_spacing_x = source.xCutSpacing(),
                                 .cut_spacing_y = source.yCutSpacing(),
                                 .bottom_enclosure_x = source.xBotEnc(),
                                 .bottom_enclosure_y = source.yBotEnc(),
                                 .top_enclosure_x = source.xTopEnc(),
                                 .top_enclosure_y = source.yTopEnc()};
    if (source.hasRowCol()) {
      if (source.numCutRows() <= 0 || source.numCutCols() <= 0) {
        throw std::runtime_error("generated VIA ROWCOL must be positive");
      }
      generated.row_col = std::pair{static_cast<uint32_t>(source.numCutRows()), static_cast<uint32_t>(source.numCutCols())};
    }
    if (source.hasOrigin()) {
      generated.origin = std::pair{source.xOffset(), source.yOffset()};
    }
    if (source.hasOffset()) {
      generated.offset = std::array{source.xBotOffset(), source.yBotOffset(), source.xTopOffset(), source.yTopOffset()};
    }
    if (source.hasCutPattern()) {
      generated.pattern = requiredText(source.cutPattern(), "generated VIA PATTERN");
    }
    result.generated = std::move(generated);
  }
  return result;
}

StagedViaRule stageViaRule(const lefiViaRule& source)
{
  StagedViaRule result{
      .name = requiredText(source.name(), "VIARULE name"), .generate = source.hasGenerate() != 0, .is_default = source.hasDefault() != 0};
  result.layers.reserve(static_cast<size_t>(source.numLayers()));
  for (int index = 0; index < source.numLayers(); ++index) {
    const auto* layer = source.layer(index);
    if (layer == nullptr) {
      throw std::runtime_error("SI2 returned a null VIARULE LAYER");
    }
    result.layers.push_back(stageViaRuleLayer(*layer));
  }
  result.candidates.reserve(static_cast<size_t>(source.numVias()));
  for (int index = 0; index < source.numVias(); ++index) {
    result.candidates.push_back(requiredText(source.viaName(index), "VIARULE VIA candidate"));
  }
  result.properties.reserve(static_cast<size_t>(source.numProps()));
  for (int index = 0; index < source.numProps(); ++index) {
    result.properties.push_back(viaRuleProperty(source, index));
  }
  return result;
}

StagedNonDefaultRule stageNonDefaultRule(const lefiNonDefault& source)
{
  StagedNonDefaultRule result{.name = requiredText(source.name(), "NONDEFAULTRULE name"), .hard_spacing = source.hasHardspacing() != 0};
  result.routing_rules.reserve(static_cast<size_t>(source.numLayers()));
  for (int index = 0; index < source.numLayers(); ++index) {
    StagedNdrRoutingRule rule{.layer = requiredText(source.layerName(index), "NONDEFAULTRULE LAYER name")};
    if (source.hasLayerWidth(index)) {
      rule.width = source.layerWidth(index);
    }
    if (source.hasLayerDiagWidth(index)) {
      rule.diag_width = source.layerDiagWidth(index);
    }
    if (source.hasLayerSpacing(index)) {
      rule.spacing = source.layerSpacing(index);
    }
    if (source.hasLayerWireExtension(index)) {
      rule.wire_extension = source.layerWireExtension(index);
    }
    if (source.hasLayerResistance(index)) {
      rule.resistance = source.layerResistance(index);
    }
    if (source.hasLayerCapacitance(index)) {
      rule.capacitance = source.layerCapacitance(index);
    }
    if (source.hasLayerEdgeCap(index)) {
      rule.edge_capacitance = source.layerEdgeCap(index);
    }
    result.routing_rules.push_back(std::move(rule));
  }
  result.use_vias.reserve(static_cast<size_t>(source.numUseVia()));
  for (int index = 0; index < source.numUseVia(); ++index) {
    result.use_vias.push_back(requiredText(source.viaName(index), "NONDEFAULTRULE USEVIA"));
  }
  result.use_via_rules.reserve(static_cast<size_t>(source.numUseViaRule()));
  for (int index = 0; index < source.numUseViaRule(); ++index) {
    result.use_via_rules.push_back(requiredText(source.viaRuleName(index), "NONDEFAULTRULE USEVIARULE"));
  }
  result.min_cuts.reserve(static_cast<size_t>(source.numMinCuts()));
  for (int index = 0; index < source.numMinCuts(); ++index) {
    if (source.numCuts(index) <= 0) {
      throw std::runtime_error("NONDEFAULTRULE MINCUTS count must be positive");
    }
    result.min_cuts.push_back(StagedNdrMinCuts{.layer = requiredText(source.cutLayerName(index), "NONDEFAULTRULE MINCUTS LAYER"),
                                               .cut_count = static_cast<uint32_t>(source.numCuts(index))});
  }
  result.vias.reserve(static_cast<size_t>(source.numVias()));
  for (int index = 0; index < source.numVias(); ++index) {
    const auto* via = source.viaRule(index);
    if (via == nullptr) {
      throw std::runtime_error("SI2 returned a null NONDEFAULTRULE VIA");
    }
    result.vias.push_back(stageVia(*via));
  }
  result.same_net_spacing_rules.reserve(static_cast<size_t>(source.numSpacingRules()));
  for (int index = 0; index < source.numSpacingRules(); ++index) {
    const auto* spacing = source.spacingRule(index);
    if (spacing == nullptr) {
      throw std::runtime_error("SI2 returned a null NONDEFAULTRULE SPACING rule");
    }
    result.same_net_spacing_rules.push_back(
        StagedNdrSameNetSpacing{.first_layer = requiredText(spacing->name1(), "NONDEFAULTRULE SPACING first layer"),
                                .second_layer = requiredText(spacing->name2(), "NONDEFAULTRULE SPACING second layer"),
                                .spacing = spacing->distance(),
                                .stack = spacing->hasStack() != 0});
  }
  result.properties.reserve(static_cast<size_t>(source.numProps()));
  for (int index = 0; index < source.numProps(); ++index) {
    result.properties.push_back(ndrProperty(source, index));
  }
  return result;
}

PreparedViaMaster prepareViaMaster(const StagedViaMaster& via, int32_t units)
{
  PreparedViaMaster prepared{.name = via.name, .flags = via.flags, .resistance = via.resistance, .properties = via.properties};
  prepared.layers.reserve(via.layers.size());
  for (const auto& source_layer : via.layers) {
    PreparedViaLayer layer{.name = source_layer.name};
    layer.rects.reserve(source_layer.rects.size());
    for (const auto& source_rect : source_layer.rects) {
      const auto rect = prepareRect(source_rect, units, "VIA RECT");
      if (!rect.hasArea()) {
        throw std::runtime_error("VIA RECT has no area after DBU conversion");
      }
      layer.rects.push_back(rect);
    }
    layer.polygons.reserve(source_layer.polygons.size());
    for (const auto& source_polygon : source_layer.polygons) {
      layer.polygons.push_back(preparePolygon(source_polygon, units, "VIA POLYGON"));
    }
    prepared.layers.push_back(std::move(layer));
  }
  if (via.generated) {
    const auto& staged = *via.generated;
    PreparedGeneratedVia generated{
        .via_rule = staged.via_rule,
        .bottom_layer = staged.bottom_layer,
        .cut_layer = staged.cut_layer,
        .top_layer = staged.top_layer,
        .cut_size_x = toDatabaseUnits(staged.cut_size_x, units, "generated VIA CUTSIZE X"),
        .cut_size_y = toDatabaseUnits(staged.cut_size_y, units, "generated VIA CUTSIZE Y"),
        .cut_spacing_x = toDatabaseUnits(staged.cut_spacing_x, units, "generated VIA CUTSPACING X"),
        .cut_spacing_y = toDatabaseUnits(staged.cut_spacing_y, units, "generated VIA CUTSPACING Y"),
        .bottom_enclosure_x = toDatabaseUnits(staged.bottom_enclosure_x, units, "generated VIA bottom ENCLOSURE X"),
        .bottom_enclosure_y = toDatabaseUnits(staged.bottom_enclosure_y, units, "generated VIA bottom ENCLOSURE Y"),
        .top_enclosure_x = toDatabaseUnits(staged.top_enclosure_x, units, "generated VIA top ENCLOSURE X"),
        .top_enclosure_y = toDatabaseUnits(staged.top_enclosure_y, units, "generated VIA top ENCLOSURE Y")};
    if (staged.row_col) {
      generated.flags |= TechGeneratedViaMasterFlag::kHasRowCol;
      generated.row_count = staged.row_col->first;
      generated.column_count = staged.row_col->second;
    }
    if (staged.origin) {
      generated.flags |= TechGeneratedViaMasterFlag::kHasOrigin;
      generated.origin_x = toDatabaseUnits(staged.origin->first, units, "generated VIA ORIGIN X", true);
      generated.origin_y = toDatabaseUnits(staged.origin->second, units, "generated VIA ORIGIN Y", true);
    }
    if (staged.offset) {
      generated.flags |= TechGeneratedViaMasterFlag::kHasOffset;
      generated.bottom_offset_x = toDatabaseUnits((*staged.offset)[0], units, "generated VIA bottom OFFSET X", true);
      generated.bottom_offset_y = toDatabaseUnits((*staged.offset)[1], units, "generated VIA bottom OFFSET Y", true);
      generated.top_offset_x = toDatabaseUnits((*staged.offset)[2], units, "generated VIA top OFFSET X", true);
      generated.top_offset_y = toDatabaseUnits((*staged.offset)[3], units, "generated VIA top OFFSET Y", true);
    }
    if (staged.pattern) {
      generated.flags |= TechGeneratedViaMasterFlag::kHasPattern;
      generated.pattern = *staged.pattern;
    }
    prepared.generated = std::move(generated);
  }
  return prepared;
}

PreparedTechObjects prepareTechObjects(const StagedTechObjects& source, int32_t units)
{
  PreparedTechObjects result;
  result.vias.reserve(source.vias.size());
  for (const auto& via : source.vias) {
    result.vias.push_back(prepareViaMaster(via, units));
  }

  result.via_rules.reserve(source.via_rules.size());
  for (const auto& rule : source.via_rules) {
    PreparedViaRule prepared{.name = rule.name,
                             .generate = rule.generate,
                             .is_default = rule.is_default,
                             .candidates = rule.candidates,
                             .properties = rule.properties};
    prepared.layers.reserve(rule.layers.size());
    for (const auto& layer : rule.layers) {
      prepared.layers.push_back(prepareViaRuleLayer(layer, units));
    }
    if (!prepared.generate) {
      for (const auto& layer : rule.layers) {
        if (layer.enclosure || layer.spacing || layer.rect || layer.resistance || layer.overhang || layer.metal_overhang) {
          throw std::runtime_error("ordinary VIARULE contains GENERATE-only layer fields");
        }
      }
    }
    result.via_rules.push_back(std::move(prepared));
  }

  result.non_default_rules.reserve(source.non_default_rules.size());
  for (const auto& ndr : source.non_default_rules) {
    PreparedNonDefaultRule prepared{.name = ndr.name,
                                    .hard_spacing = ndr.hard_spacing,
                                    .use_vias = ndr.use_vias,
                                    .use_via_rules = ndr.use_via_rules,
                                    .min_cuts = ndr.min_cuts,
                                    .properties = ndr.properties};
    prepared.vias.reserve(ndr.vias.size());
    for (const auto& via : ndr.vias) {
      prepared.vias.push_back(prepareViaMaster(via, units));
    }
    prepared.same_net_spacing_rules.reserve(ndr.same_net_spacing_rules.size());
    for (const auto& spacing : ndr.same_net_spacing_rules) {
      prepared.same_net_spacing_rules.push_back(
          PreparedNonDefaultRule::SameNetSpacing{.first_layer = spacing.first_layer,
                                                 .second_layer = spacing.second_layer,
                                                 .spacing = toDatabaseUnits(spacing.spacing, units, "NONDEFAULTRULE same-net SPACING"),
                                                 .stack = spacing.stack});
    }
    prepared.routing_rules.reserve(ndr.routing_rules.size());
    for (const auto& source_rule : ndr.routing_rules) {
      if (!source_rule.width) {
        throw std::runtime_error("NONDEFAULTRULE LAYER WIDTH is required");
      }
      PreparedNdrRoutingRule rule{.layer = source_rule.layer, .width = toDatabaseUnits(*source_rule.width, units, "NONDEFAULTRULE WIDTH")};
      if (source_rule.diag_width) {
        rule.flags |= TechNdrRoutingRuleFlag::kHasDiagWidth;
        rule.diag_width = toDatabaseUnits(*source_rule.diag_width, units, "NONDEFAULTRULE DIAGWIDTH");
      }
      if (source_rule.spacing) {
        rule.flags |= TechNdrRoutingRuleFlag::kHasSpacing;
        rule.spacing = toDatabaseUnits(*source_rule.spacing, units, "NONDEFAULTRULE SPACING");
      }
      if (source_rule.wire_extension) {
        rule.flags |= TechNdrRoutingRuleFlag::kHasWireExtension;
        rule.wire_extension = toDatabaseUnits(*source_rule.wire_extension, units, "NONDEFAULTRULE WIREEXTENSION");
      }
      if (source_rule.resistance) {
        rule.flags |= TechNdrRoutingRuleFlag::kHasResistance;
        rule.resistance = *source_rule.resistance;
      }
      if (source_rule.capacitance) {
        rule.flags |= TechNdrRoutingRuleFlag::kHasCapacitance;
        rule.capacitance = *source_rule.capacitance;
      }
      if (source_rule.edge_capacitance) {
        rule.flags |= TechNdrRoutingRuleFlag::kHasEdgeCapacitance;
        rule.edge_capacitance = *source_rule.edge_capacitance;
      }
      prepared.routing_rules.push_back(std::move(rule));
    }
    result.non_default_rules.push_back(std::move(prepared));
  }
  return result;
}

void commitTechObjects(TechStore& database, const PreparedTechObjects& objects)
{
  commitViaGenerateRules(database, objects.via_rules);
  commitVias(database, objects.vias);
  commitOrdinaryViaRules(database, objects.via_rules);
  commitNonDefaultRules(database, objects.non_default_rules);
}

}  // namespace eccdb::lef_detail
