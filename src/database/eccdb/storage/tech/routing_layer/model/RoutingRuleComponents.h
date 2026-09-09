#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tech/cut_layer/model/CutLayerTypes.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/routing_layer/model/RoutingRuleItems.h"

namespace eccdb {

enum class TechRoutingSpacingType : uint8_t
{
  kDefault,
  kRange
};

// Native ROUTING-layer spacing forms represented here:
//   SPACING minSpacing ;
//   SPACING minSpacing RANGE minWidth maxWidth ;
struct TechRoutingSpacingRule
{
  TechRoutingSpacingType type = TechRoutingSpacingType::kDefault;
  int32_t min_spacing = 0;
  int32_t min_width = -1;
  int32_t max_width = -1;
};

namespace TechRoutingEndOfLineSpacingRuleFlag {
constexpr uint32_t kHasParallelEdge = 1u << 0;
constexpr uint32_t kTwoEdges = 1u << 1;
}  // namespace TechRoutingEndOfLineSpacingRuleFlag

// Native ROUTING-layer clause:
//   SPACING minSpacing ENDOFLINE eolWidth WITHIN eolWithin
//     [PARALLELEDGE parSpace WITHIN parWithin [TWOEDGES]] ;
struct TechRoutingEndOfLineSpacingRule
{
  uint32_t flags = 0;
  int32_t min_spacing = 0;
  int32_t eol_width = 0;
  int32_t eol_within = 0;
  int32_t parallel_space = 0;
  int32_t parallel_within = 0;
};

// Native ROUTING-layer clause:
//   MINENCLOSEDAREA area [WIDTH width] ;
struct TechRoutingMinEncloseAreaRule
{
  int64_t area = 0;  // DBU squared.
  int32_t width = -1;
};

enum class TechRoutingMinStepType : uint8_t
{
  kNone,
  kInsideCorner,
  kOutsideCorner,
  kStep
};

namespace TechRoutingMinStepRuleFlag {
constexpr uint32_t kHasMaxLengthSum = 1u << 0;
constexpr uint32_t kHasMaxEdges = 1u << 1;
}  // namespace TechRoutingMinStepRuleFlag

// Native ROUTING-layer clause:
//   MINSTEP minStepLength [INSIDECORNER | OUTSIDECORNER | STEP]
//     [LENGTHSUM maxLength] [MAXEDGES maxEdges] ;
struct TechRoutingMinStepRule
{
  uint32_t flags = 0;
  int32_t min_step_length = 0;
  int32_t max_length_sum = 0;
  int32_t max_edges = 0;
  TechRoutingMinStepType type = TechRoutingMinStepType::kNone;
};

enum class TechRoutingMinimumCutOrient : uint8_t
{
  kNone,
  kFromAbove,
  kFromBelow
};

namespace TechRoutingMinimumCutRuleFlag {
constexpr uint32_t kHasWithinCutDistance = 1u << 0;
constexpr uint32_t kHasLength = 1u << 1;
}  // namespace TechRoutingMinimumCutRuleFlag

// Native ROUTING-layer clause:
//   MINIMUMCUT numCuts WIDTH width [WITHIN cutDistance]
//     [FROMABOVE | FROMBELOW]
//     [LENGTH length WITHIN distance] ;
struct TechRoutingMinimumCutRule
{
  uint32_t flags = 0;
  int32_t num_cuts = 0;
  int32_t width = 0;
  int32_t within_cut_distance = 0;
  TechRoutingMinimumCutOrient orient = TechRoutingMinimumCutOrient::kNone;
  int32_t length = 0;
  int32_t length_distance = 0;
};

// Native ROUTING-layer clause:
//   SPACING minSpacing NOTCHLENGTH notchLength ;
struct TechRoutingSpacingNotchLengthRule
{
  int32_t min_spacing = 0;
  int32_t notch_length = 0;
};

namespace TechRoutingPrlSpacingTableRuleFlag {
constexpr uint32_t kHasEolWidth = 1u << 0;
constexpr uint32_t kWrongDirection = 1u << 1;
constexpr uint32_t kSameMask = 1u << 2;
constexpr uint32_t kExceptEol = 1u << 3;
constexpr uint32_t kHasExceptWithin = 1u << 4;
constexpr uint32_t kHasInfluence = 1u << 5;
}  // namespace TechRoutingPrlSpacingTableRuleFlag

// Native parallel-run-length table and optional companion data:
//   SPACINGTABLE PARALLELRUNLENGTH prl ...
//     {WIDTH width spacing ...} ... ;
//   [SPACINGTABLE INFLUENCE
//     {WIDTH width WITHIN distance SPACING spacing} ... ;]
// cells is a width-major rectangular matrix.
struct TechRoutingPrlSpacingTableRule
{
  uint32_t flags = 0;
  int32_t eol_width = 0;
  std::vector<int32_t> widths;
  std::vector<int32_t> parallel_run_lengths;
  // Width-major rectangular matrix: width_index * parallel_run_lengths.size() + prl_index.
  std::vector<int32_t> cells;
  std::vector<TechRoutingPrlSpacingTableExceptWithin> except_withins;
  std::vector<TechRoutingPrlSpacingTableInfluence> influences;

  [[nodiscard]] uint32_t widthCount() const noexcept { return static_cast<uint32_t>(widths.size()); }
  [[nodiscard]] uint32_t parallelRunLengthCount() const noexcept { return static_cast<uint32_t>(parallel_run_lengths.size()); }
};

// Standalone representation of:
//   SPACINGTABLE INFLUENCE
//     {WIDTH width WITHIN distance SPACING spacing} ... ;
struct TechRoutingInfluenceSpacingTableRule
{
  std::vector<TechRoutingInfluenceSpacingTableEntry> entries;

  [[nodiscard]] uint32_t entryCount() const noexcept { return static_cast<uint32_t>(entries.size()); }
};

// Native two-width table:
//   SPACINGTABLE TWOWIDTHS
//     {WIDTH width [PRL runLength] spacing ...} ... ;
// cells is a row-major N x N matrix using widths for both axes.
struct TechRoutingTwoWidthsSpacingTableRule
{
  std::vector<TechRoutingTwoWidthsSpacingTableWidth> widths;
  // Row-major N x N matrix indexed by first-width then second-width axis.
  std::vector<int32_t> cells;

  [[nodiscard]] uint32_t widthCount() const noexcept { return static_cast<uint32_t>(widths.size()); }
};

enum class TechRoutingCurrentDensitySignal : uint8_t
{
  kAc,
  kDc,
};

enum class TechRoutingCurrentDensityType : uint8_t
{
  kUnknown,
  kPeak,
  kAverage,
  kRms,
};

namespace TechRoutingCurrentDensityRuleFlag {
constexpr uint32_t kHasScalar = 1u << 0;
constexpr uint32_t kHasFrequencies = 1u << 1;
constexpr uint32_t kHasWidths = 1u << 2;
constexpr uint32_t kHasTableEntries = 1u << 3;
}  // namespace TechRoutingCurrentDensityRuleFlag

// Native ROUTING-layer current-density alternatives:
//   ACCURRENTDENSITY {PEAK | AVERAGE | RMS}
//     {scalar | FREQUENCY ... ; [WIDTH ... ;] TABLEENTRIES ...} ;
//   DCCURRENTDENSITY AVERAGE
//     {scalar | WIDTH ... ; TABLEENTRIES ...} ;
// AC table entries are frequency-major and width-minor. When WIDTH is absent,
// the table has one implicit width column and applies independently of width.
struct TechRoutingCurrentDensityRule
{
  TechRoutingCurrentDensitySignal signal = TechRoutingCurrentDensitySignal::kAc;
  TechRoutingCurrentDensityType type = TechRoutingCurrentDensityType::kUnknown;
  uint32_t flags = 0;
  double scalar = 0.0;
  std::vector<double> frequencies;
  std::vector<int32_t> widths;
  // Frequency-major, width-minor rectangular matrix.
  std::vector<double> table_entries;

  [[nodiscard]] uint32_t frequencyCount() const noexcept { return static_cast<uint32_t>(frequencies.size()); }
  [[nodiscard]] uint32_t widthCount() const noexcept { return static_cast<uint32_t>(widths.size()); }
  [[nodiscard]] uint32_t tableEntryCount() const noexcept { return static_cast<uint32_t>(table_entries.size()); }
};

namespace TechRoutingLef58AreaRuleFlag {
constexpr uint32_t kHasExceptEdgeLength = 1u << 0;
constexpr uint32_t kHasExceptMinEdgeLength = 1u << 1;
constexpr uint32_t kHasMask = 1u << 2;
constexpr uint32_t kHasExceptMinWidth = 1u << 3;
constexpr uint32_t kHasExceptStep = 1u << 4;
constexpr uint32_t kHasRectWidth = 1u << 5;
constexpr uint32_t kExceptRectangle = 1u << 6;
constexpr uint32_t kHasTrimLayer = 1u << 7;
constexpr uint32_t kHasOverlap = 1u << 8;
}  // namespace TechRoutingLef58AreaRuleFlag

// Parsed payload of one rule in:
//   PROPERTY LEF58_AREA "AREA minArea ... ;" ;
struct TechRoutingLef58AreaRule
{
  uint32_t flags = 0;
  int64_t min_area = 0;
  uint32_t mask = 0;
  int32_t except_min_width = 0;
  std::vector<TechRoutingLef58AreaExceptMinSize> except_min_sizes;
  int32_t except_min_edge_length = 0;
  int32_t except_max_edge_length = 0;
  int32_t except_step_x = 0;
  int32_t except_step_y = 0;
  int32_t rect_width = 0;
  std::string trim_layer_name;
  uint32_t overlap = 0;
};

// Parsed payload of:
//   PROPERTY LEF58_CORNERFILLSPACING
//     "CORNERFILLSPACING spacing ... ;" ;
struct TechRoutingLef58CornerFillSpacingRule
{
  int32_t spacing = 0;
  int32_t edge_length1 = 0;
  int32_t edge_length2 = 0;
  int32_t eol_width = 0;
};

enum class TechRoutingLef58CornerType : uint8_t
{
  kConvex,
  kConcave
};

namespace TechRoutingLef58CornerSpacingRuleFlag {
constexpr uint32_t kHasExceptEol = 1u << 0;
constexpr uint32_t kCornerToCorner = 1u << 1;
}  // namespace TechRoutingLef58CornerSpacingRuleFlag

// Parsed payload of one rule in:
//   PROPERTY LEF58_CORNERSPACING
//     "CORNERSPACING {CONVEXCORNER | CONCAVECORNER}
//       [EXCEPTEOL eolWidth] WIDTH width SPACING spacing ... ;" ;
struct TechRoutingLef58CornerSpacingRule
{
  uint32_t flags = 0;
  TechRoutingLef58CornerType type = TechRoutingLef58CornerType::kConvex;
  int32_t except_eol = 0;
  std::vector<TechRoutingLef58CornerSpacingWidth> width_spacings;
};

namespace TechRoutingLef58MinimumCutRuleFlag {
constexpr uint32_t kHasNumCuts = 1u << 0;
constexpr uint32_t kHasWithinCutDistance = 1u << 1;
constexpr uint32_t kHasLength = 1u << 2;
constexpr uint32_t kHasArea = 1u << 3;
constexpr uint32_t kHasAreaWithinDistance = 1u << 4;
constexpr uint32_t kSameMetalOverlap = 1u << 5;
constexpr uint32_t kFullyEnclosed = 1u << 6;
constexpr uint32_t kHasCutClasses = 1u << 7;
}  // namespace TechRoutingLef58MinimumCutRuleFlag

// Parsed payload of one rule in:
//   PROPERTY LEF58_MINIMUMCUT "MINIMUMCUT ... ;" ;
struct TechRoutingLef58MinimumCutRule
{
  uint32_t flags = 0;
  int32_t num_cuts = 0;
  std::vector<TechRoutingLef58MinimumCutClass> cutclasses;
  int32_t width = 0;
  int32_t within_cut_distance = 0;
  TechRoutingMinimumCutOrient orient = TechRoutingMinimumCutOrient::kNone;
  int32_t length = 0;
  int32_t length_distance = 0;
  int64_t area = 0;
  int32_t area_within_distance = 0;
};

namespace TechRoutingLef58MinStepRuleFlag {
constexpr uint32_t kHasMaxEdges = 1u << 0;
constexpr uint32_t kHasMinAdjacentLength = 1u << 1;
constexpr uint32_t kConvexCorner = 1u << 2;
constexpr uint32_t kHasExceptWithin = 1u << 3;
constexpr uint32_t kHasType = 1u << 4;
constexpr uint32_t kHasMaxLengthSum = 1u << 5;
constexpr uint32_t kExceptRectangle = 1u << 6;
constexpr uint32_t kHasSecondMinAdjacentLength = 1u << 7;
constexpr uint32_t kConcaveCorner = 1u << 8;
constexpr uint32_t kThreeConcaveCorners = 1u << 9;
constexpr uint32_t kHasCenterWidth = 1u << 10;
constexpr uint32_t kHasMinBetweenLength = 1u << 11;
constexpr uint32_t kExceptSameCorners = 1u << 12;
constexpr uint32_t kHasNoAdjacentEol = 1u << 13;
constexpr uint32_t kHasExceptAdjacentLength = 1u << 14;
constexpr uint32_t kHasFollowupMinAdjacentLength = 1u << 15;
constexpr uint32_t kConcaveCorners = 1u << 16;
constexpr uint32_t kHasNoBetweenEol = 1u << 17;
}  // namespace TechRoutingLef58MinStepRuleFlag

// Parsed payload of one rule in:
//   PROPERTY LEF58_MINSTEP "MINSTEP minStepLength ... ;" ;
struct TechRoutingLef58MinStepRule
{
  uint32_t flags = 0;
  TechRoutingMinStepType type = TechRoutingMinStepType::kNone;
  int32_t min_step_length = 0;
  int32_t max_length_sum = 0;
  uint32_t max_edges = 0;
  int32_t min_adjacent_length = 0;
  int32_t min_adjacent_length2 = 0;
  int32_t except_within = 0;
  int32_t center_width = 0;
  int32_t min_between_length = 0;
  int32_t no_adjacent_eol_width = 0;
  int32_t except_adjacent_length = 0;
  int32_t followup_min_adjacent_length = 0;
  int32_t no_between_eol_width = 0;
};

namespace TechRoutingLef58WidthTableRuleFlag {
constexpr uint32_t kWrongDirection = 1u << 0;
constexpr uint32_t kOrthogonal = 1u << 1;
}  // namespace TechRoutingLef58WidthTableRuleFlag

// Parsed payload of:
//   PROPERTY LEF58_WIDTHTABLE
//     "WIDTHTABLE [WRONGDIRECTION] [ORTHOGONAL] width ... ;" ;
struct TechRoutingLef58WidthTableRule
{
  uint32_t flags = 0;
  std::vector<int32_t> widths;

  [[nodiscard]] uint32_t widthCount() const noexcept { return static_cast<uint32_t>(widths.size()); }
};

namespace TechRoutingLef58SpacingEolRuleFlag {
constexpr uint64_t kHasEolWithin = 1ull << 0;
constexpr uint64_t kExactWidth = 1ull << 1;
constexpr uint64_t kHasWrongDirSpace = 1ull << 2;
constexpr uint64_t kHasOppositeWidth = 1ull << 3;
constexpr uint64_t kHasWrongDirWithin = 1ull << 4;
constexpr uint64_t kSameMask = 1ull << 5;
constexpr uint64_t kHasExceptExactWidth = 1ull << 6;
constexpr uint64_t kHasFillConcaveCorner = 1ull << 7;
constexpr uint64_t kHasWithCut = 1ull << 8;
constexpr uint64_t kHasWithCutClass = 1ull << 9;
constexpr uint64_t kWithCutAbove = 1ull << 10;
constexpr uint64_t kHasEnclosureEndWidth = 1ull << 11;
constexpr uint64_t kHasEnclosureEndWithin = 1ull << 12;
constexpr uint64_t kHasEndPrlSpacing = 1ull << 13;
constexpr uint64_t kHasEndToEnd = 1ull << 14;
constexpr uint64_t kHasOneCutSpace = 1ull << 15;
constexpr uint64_t kHasTwoCutSpace = 1ull << 16;
constexpr uint64_t kHasExtension = 1ull << 17;
constexpr uint64_t kHasWrongDirExtension = 1ull << 18;
constexpr uint64_t kHasOtherEndWidth = 1ull << 19;
constexpr uint64_t kHasAdjacentEdgeLength = 1ull << 20;
constexpr uint64_t kHasAdjacentMaxLength = 1ull << 21;
constexpr uint64_t kHasAdjacentMinLength = 1ull << 22;
constexpr uint64_t kTwoSides = 1ull << 23;
constexpr uint64_t kEqualRectWidth = 1ull << 24;
constexpr uint64_t kHasParallelEdge = 1ull << 25;
constexpr uint64_t kSubtractEolWidth = 1ull << 26;
constexpr uint64_t kHasParallelRunLength = 1ull << 27;
constexpr uint64_t kHasParallelMinLength = 1ull << 28;
constexpr uint64_t kTwoEdges = 1ull << 29;
constexpr uint64_t kSameMetal = 1ull << 30;
constexpr uint64_t kNonEolCornerOnly = 1ull << 31;
constexpr uint64_t kParallelSameMask = 1ull << 32;
constexpr uint64_t kHasEncloseCut = 1ull << 33;
constexpr uint64_t kAllCuts = 1ull << 34;
constexpr uint64_t kHasToConcaveCorner = 1ull << 35;
constexpr uint64_t kHasToConcaveCornerMinLength = 1ull << 36;
constexpr uint64_t kHasToConcaveCornerMinAdjacentLength = 1ull << 37;
constexpr uint64_t kHasToConcaveCornerTwoMinAdjacentLengths = 1ull << 38;
constexpr uint64_t kHasToNotchLength = 1ull << 39;
}  // namespace TechRoutingLef58SpacingEolRuleFlag

// Parsed payload of one rule in:
//   PROPERTY LEF58_SPACING "SPACING eolSpace ENDOFLINE eolWidth ... ;" ;
struct TechRoutingLef58SpacingEolRule
{
  uint64_t flags = 0;
  int32_t eol_space = 0;
  int32_t eol_width = 0;
  int32_t wrong_dir_space = 0;
  int32_t opposite_width = 0;
  int32_t eol_within = 0;
  int32_t wrong_dir_within = 0;
  int32_t except_exact_width1 = 0;
  int32_t except_exact_width2 = 0;
  int32_t fill_concave_corner_width = 0;
  std::string with_cut_class_name;
  int32_t with_cut_space = 0;
  int32_t enclosure_end_width = 0;
  int32_t enclosure_end_within = 0;
  int32_t end_prl_space = 0;
  int32_t end_prl = 0;
  int32_t end_to_end_space = 0;
  int32_t one_cut_space = 0;
  int32_t two_cut_space = 0;
  int32_t extension = 0;
  int32_t wrong_dir_extension = 0;
  int32_t other_end_width = 0;
  int32_t adjacent_max_length = 0;
  int32_t adjacent_min_length = 0;
  int32_t parallel_space = 0;
  int32_t parallel_within = 0;
  int32_t parallel_run_length = 0;
  int32_t parallel_min_length = 0;
  CutLayerSide enclose_cut_side = CutLayerSide::kUnknown;
  int32_t enclose_distance = 0;
  int32_t cut_to_metal_spacing = 0;
  int32_t to_concave_corner_min_length = 0;
  int32_t to_concave_corner_min_adjacent_length1 = 0;
  int32_t to_concave_corner_min_adjacent_length2 = 0;
  int32_t notch_length = 0;
};

namespace TechRoutingLef58SpacingNotchLengthRuleFlag {
constexpr uint32_t kHasConcaveEndsSideOfNotchWidth = 1u << 0;
}  // namespace TechRoutingLef58SpacingNotchLengthRuleFlag

// Parsed payload of one rule in:
//   PROPERTY LEF58_SPACING
//     "SPACING minSpacing NOTCHLENGTH minNotchLength ... ;" ;
struct TechRoutingLef58SpacingNotchLengthRule
{
  uint32_t flags = 0;
  int32_t min_spacing = 0;
  int32_t min_notch_length = 0;
  int32_t concave_ends_side_of_notch_width = 0;
};

// Parsed payload of:
//   PROPERTY LEF58_SPACINGTABLE
//     "SPACINGTABLE JOGTOJOGSPACING ... ;" ;
struct TechRoutingLef58SpacingTableJogToJogRule
{
  int32_t jog_to_jog_spacing = 0;
  int32_t jog_width = 0;
  int32_t short_jog_spacing = 0;
  std::vector<TechRoutingLef58JogToJogWidth> widths;

  [[nodiscard]] uint32_t widthCount() const noexcept { return static_cast<uint32_t>(widths.size()); }
};

}  // namespace eccdb
