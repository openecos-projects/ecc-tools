#pragma once

#include <cstdint>

#include "tech/common/TechLayerTypes.h"

namespace eccdb {

enum class TechRoutingDirection : uint8_t
{
  kUnknown,
  kHorizontal,
  kVertical,
  kDiag45,
  kDiag135
};

enum class TechRoutingAxisValueForm : uint8_t
{
  kNone,
  kBothXY,
  kSeparateXY
};

namespace TechRoutingLayerFlag {
constexpr uint64_t kHasMinWidth = 1ull << 0;
constexpr uint64_t kHasMaxWidth = 1ull << 1;
constexpr uint64_t kHasPitchY = 1ull << 2;
constexpr uint64_t kHasOffsetY = 1ull << 3;
constexpr uint64_t kHasWireExtension = 1ull << 4;
constexpr uint64_t kHasThickness = 1ull << 5;
constexpr uint64_t kHasHeight = 1ull << 6;
constexpr uint64_t kHasArea = 1ull << 7;
constexpr uint64_t kHasResistance = 1ull << 8;
constexpr uint64_t kHasCapacitance = 1ull << 9;
constexpr uint64_t kHasEdgeCapacitance = 1ull << 10;
constexpr uint64_t kHasDensity = 1ull << 11;
constexpr uint64_t kHasMinCut = 1ull << 12;
constexpr uint64_t kLef58RectOnly = 1ull << 13;
constexpr uint64_t kLef58RectOnlyExceptNonCorePins = 1ull << 14;
constexpr uint64_t kLef58RightWayOnGridOnly = 1ull << 15;
constexpr uint64_t kLef58RightWayOnGridOnlyCheckMask = 1ull << 16;
constexpr uint64_t kHasWidth = 1ull << 17;
constexpr uint64_t kHasPowerSegmentWidth = 1ull << 18;
constexpr uint64_t kHasMinDensity = 1ull << 19;
constexpr uint64_t kHasMaxDensity = 1ull << 20;
constexpr uint64_t kHasDensityCheckWindow = 1ull << 21;
constexpr uint64_t kHasDensityCheckStep = 1ull << 22;
constexpr uint64_t kHasDiagWidth = 1ull << 23;
constexpr uint64_t kHasDiagSpacing = 1ull << 24;
constexpr uint64_t kHasProtrusion = 1ull << 25;
constexpr uint64_t kHasShrinkage = 1ull << 26;
constexpr uint64_t kHasCapMultiplier = 1ull << 27;
constexpr uint64_t kHasFillActiveSpacing = 1ull << 28;
constexpr uint64_t kPolyRouting = 1ull << 29;
}  // namespace TechRoutingLayerFlag

// LEF 5.8 ROUTING layer scalar subset represented by this component:
//   LAYER name
//     TYPE ROUTING ;
//     DIRECTION direction ;
//     [PITCH distance | PITCH xDistance yDistance ;]
//     [OFFSET distance | OFFSET xDistance yDistance ;]
//     WIDTH defaultWidth ;
//     [MINWIDTH width ;] [MAXWIDTH width ;]
//     ... scalar electrical/density clauses, rules, and properties ...
//   END name
// Repeated SPACING/table/LEF58 rules are separate owned rule entities.
struct TechRoutingLayer
{
  uint64_t flags = 0;
  TechRoutingDirection direction = TechRoutingDirection::kUnknown;
  TechRoutingAxisValueForm pitch_form = TechRoutingAxisValueForm::kNone;
  TechRoutingAxisValueForm offset_form = TechRoutingAxisValueForm::kNone;
  int32_t width = 0;
  int32_t min_width = 0;
  int32_t max_width = 0;
  int32_t diag_width = 0;
  int32_t diag_spacing = 0;
  int32_t pitch_x = 0;
  int32_t pitch_y = 0;
  int32_t offset_x = 0;
  int32_t offset_y = 0;
  int32_t wire_extension = 0;
  int32_t thickness = 0;
  int32_t height = 0;
  int32_t shrinkage = 0;
  double cap_multiplier = 0.0;
  int32_t fill_active_spacing = 0;
  int64_t area = 0;  // DBU squared.
  double resistance = 0.0;
  double capacitance = 0.0;
  double edge_capacitance = 0.0;
  double min_density = 0.0;
  double max_density = 0.0;
  int32_t density_check_length = 0;
  int32_t density_check_width = 0;
  int32_t density_check_step = 0;
  int32_t min_cut_num = 0;
  int32_t min_cut_width = 0;
  int32_t power_segment_width = 0;
  int32_t protrusion_width1 = 0;
  int32_t protrusion_length = 0;
  int32_t protrusion_width2 = 0;
};

}  // namespace eccdb
