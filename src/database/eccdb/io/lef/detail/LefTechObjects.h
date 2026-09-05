#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "geometry/GeometryInput.h"
#include "tech/common/RoutingTypes.h"
#include "tech/common/TechLayerTypes.h"

namespace LefDefParser {
class lefiNonDefault;
class lefiVia;
class lefiViaRule;
}  // namespace LefDefParser

namespace eccdb {

class TechStore;

namespace lef_detail {

struct StagedRect
{
  double ll_x = 0.0;
  double ll_y = 0.0;
  double ur_x = 0.0;
  double ur_y = 0.0;
};

struct StagedPolygon
{
  std::vector<std::pair<double, double>> points;
};

struct StagedViaLayer
{
  std::string name;
  std::vector<StagedRect> rects;
  std::vector<StagedPolygon> polygons;
};

struct StagedGeneratedVia
{
  std::string via_rule;
  std::string bottom_layer;
  std::string cut_layer;
  std::string top_layer;
  double cut_size_x = 0.0;
  double cut_size_y = 0.0;
  double cut_spacing_x = 0.0;
  double cut_spacing_y = 0.0;
  double bottom_enclosure_x = 0.0;
  double bottom_enclosure_y = 0.0;
  double top_enclosure_x = 0.0;
  double top_enclosure_y = 0.0;
  std::optional<std::pair<uint32_t, uint32_t>> row_col;
  std::optional<std::pair<double, double>> origin;
  std::optional<std::array<double, 4>> offset;
  std::optional<std::string> pattern;
};

struct StagedViaMaster
{
  std::string name;
  uint32_t flags = 0;
  double resistance = 0.0;
  std::vector<TechProperty> properties;
  std::vector<StagedViaLayer> layers;
  std::optional<StagedGeneratedVia> generated;
};

struct StagedViaRuleLayer
{
  std::string name;
  RoutingDirection direction = RoutingDirection::kUnknown;
  std::optional<std::pair<double, double>> width;
  std::optional<std::pair<double, double>> enclosure;
  std::optional<std::pair<double, double>> spacing;
  std::optional<StagedRect> rect;
  std::optional<double> resistance;
  std::optional<double> overhang;
  std::optional<double> metal_overhang;
};

struct StagedViaRule
{
  std::string name;
  bool generate = false;
  bool is_default = false;
  std::vector<StagedViaRuleLayer> layers;
  std::vector<std::string> candidates;
  std::vector<TechProperty> properties;
};

struct StagedNdrRoutingRule
{
  std::string layer;
  std::optional<double> width;
  std::optional<double> diag_width;
  std::optional<double> spacing;
  std::optional<double> wire_extension;
  std::optional<double> resistance;
  std::optional<double> capacitance;
  std::optional<double> edge_capacitance;
};

struct StagedNdrMinCuts
{
  std::string layer;
  uint32_t cut_count = 0;
};

struct StagedNdrSameNetSpacing
{
  std::string first_layer;
  std::string second_layer;
  double spacing = 0.0;
  bool stack = false;
};

struct StagedNonDefaultRule
{
  std::string name;
  bool hard_spacing = false;
  std::vector<StagedNdrRoutingRule> routing_rules;
  std::vector<std::string> use_vias;
  std::vector<std::string> use_via_rules;
  std::vector<StagedNdrMinCuts> min_cuts;
  std::vector<StagedViaMaster> vias;
  std::vector<StagedNdrSameNetSpacing> same_net_spacing_rules;
  std::vector<TechProperty> properties;
};

struct StagedTechObjects
{
  std::vector<StagedViaMaster> vias;
  std::vector<StagedViaRule> via_rules;
  std::vector<StagedNonDefaultRule> non_default_rules;
};

struct PreparedViaLayer
{
  std::string name;
  std::vector<Rect> rects;
  std::vector<GeometryPolygonInput> polygons;
};

struct PreparedGeneratedVia
{
  std::string via_rule;
  std::string bottom_layer;
  std::string cut_layer;
  std::string top_layer;
  int32_t cut_size_x = 0;
  int32_t cut_size_y = 0;
  int32_t cut_spacing_x = 0;
  int32_t cut_spacing_y = 0;
  int32_t bottom_enclosure_x = 0;
  int32_t bottom_enclosure_y = 0;
  int32_t top_enclosure_x = 0;
  int32_t top_enclosure_y = 0;
  uint32_t flags = 0;
  uint32_t row_count = 1;
  uint32_t column_count = 1;
  int32_t origin_x = 0;
  int32_t origin_y = 0;
  int32_t bottom_offset_x = 0;
  int32_t bottom_offset_y = 0;
  int32_t top_offset_x = 0;
  int32_t top_offset_y = 0;
  std::string pattern;
};

struct PreparedViaMaster
{
  std::string name;
  uint32_t flags = 0;
  double resistance = 0.0;
  std::vector<TechProperty> properties;
  std::vector<PreparedViaLayer> layers;
  std::optional<PreparedGeneratedVia> generated;
};

struct PreparedViaRuleLayer
{
  std::string name;
  RoutingDirection direction = RoutingDirection::kUnknown;
  uint32_t flags = 0;
  int32_t min_width = 0;
  int32_t max_width = 0;
  int32_t enclosure_overhang1 = 0;
  int32_t enclosure_overhang2 = 0;
  int32_t spacing_x = 0;
  int32_t spacing_y = 0;
  Rect rect;
  double resistance = 0.0;
  int32_t overhang = 0;
  int32_t metal_overhang = 0;
};

struct PreparedViaRule
{
  std::string name;
  bool generate = false;
  bool is_default = false;
  std::vector<PreparedViaRuleLayer> layers;
  std::vector<std::string> candidates;
  std::vector<TechProperty> properties;
};

struct PreparedNdrRoutingRule
{
  std::string layer;
  uint32_t flags = 0;
  int32_t width = 0;
  int32_t diag_width = 0;
  int32_t spacing = 0;
  int32_t wire_extension = 0;
  double resistance = 0.0;
  double capacitance = 0.0;
  double edge_capacitance = 0.0;
};

struct PreparedNonDefaultRule
{
  std::string name;
  bool hard_spacing = false;
  std::vector<PreparedNdrRoutingRule> routing_rules;
  std::vector<std::string> use_vias;
  std::vector<std::string> use_via_rules;
  std::vector<StagedNdrMinCuts> min_cuts;
  std::vector<PreparedViaMaster> vias;
  struct SameNetSpacing
  {
    std::string first_layer;
    std::string second_layer;
    int32_t spacing = 0;
    bool stack = false;
  };
  std::vector<SameNetSpacing> same_net_spacing_rules;
  std::vector<TechProperty> properties;
};

struct PreparedTechObjects
{
  std::vector<PreparedViaMaster> vias;
  std::vector<PreparedViaRule> via_rules;
  std::vector<PreparedNonDefaultRule> non_default_rules;
};

[[nodiscard]] StagedViaMaster stageVia(const ::LefDefParser::lefiVia& source);
[[nodiscard]] StagedViaRule stageViaRule(const ::LefDefParser::lefiViaRule& source);
[[nodiscard]] StagedNonDefaultRule stageNonDefaultRule(const ::LefDefParser::lefiNonDefault& source);

[[nodiscard]] PreparedTechObjects prepareTechObjects(const StagedTechObjects& source, int32_t database_units_per_micron);
void commitTechObjects(TechStore& database, const PreparedTechObjects& objects);

}  // namespace lef_detail
}  // namespace eccdb
