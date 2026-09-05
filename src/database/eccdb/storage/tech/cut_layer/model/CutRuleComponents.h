#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tech/cut_layer/model/CutLayerTypes.h"
#include "tech/cut_layer/model/CutRuleItems.h"

namespace eccdb {

namespace TechCutSpacingRuleFlag {
constexpr uint32_t kHasAdjacentCuts = 1u << 0;
constexpr uint32_t kSameNet = 1u << 1;
constexpr uint32_t kCenterToCenter = 1u << 2;
constexpr uint32_t kSameNetPgOnly = 1u << 3;
constexpr uint32_t kHasSecondLayer = 1u << 4;
constexpr uint32_t kStack = 1u << 5;
constexpr uint32_t kExceptSamePgNet = 1u << 6;
constexpr uint32_t kParallelOverlap = 1u << 7;
constexpr uint32_t kHasCutArea = 1u << 8;
}  // namespace TechCutSpacingRuleFlag

// Implemented subset of the native CUT-layer clause:
//   SPACING cutSpacing
//     [SAMENET | ADJACENTCUTS {2 | 3 | 4} WITHIN cutWithin] ;
// Each complete rule below is attached to its own Rule entity. Its layer
// ownership and, where applicable, sibling order live in relation components.
struct TechCutSpacingRule
{
  uint32_t flags = 0;
  int32_t spacing = 0;
  uint32_t adjacent_cut_count = 0;
  int32_t adjacent_cut_within = 0;
  std::string second_layer_name;
  int64_t cut_area = 0;
};

namespace TechCutEnclosureRuleFlag {
constexpr uint32_t kHasMinWidth = 1u << 0;
constexpr uint32_t kExceptExtraCut = 1u << 1;
constexpr uint32_t kHasCutWithin = 1u << 2;
constexpr uint32_t kHasMinLength = 1u << 3;
}  // namespace TechCutEnclosureRuleFlag

// Native CUT ENCLOSURE is repeatable and its ABOVE/BELOW qualifier is optional.
struct TechCutEnclosureRule
{
  uint32_t flags = 0;
  CutLayerSide side = CutLayerSide::kUnknown;
  int32_t overhang1 = 0;
  int32_t overhang2 = 0;
  int32_t min_width = 0;
  int32_t cut_within = 0;
  int32_t min_length = 0;
};

namespace TechCutArraySpacingRuleFlag {
constexpr uint32_t kLongArray = 1u << 0;
constexpr uint32_t kHasViaWidth = 1u << 1;
}  // namespace TechCutArraySpacingRuleFlag

// Implemented subset of:
//   ARRAYSPACING [LONGARRAY] [WIDTH viaWidth]
//     CUTSPACING cutSpacing
//     {ARRAYCUTS arrayCuts SPACING arraySpacing} ... ;
// The repeated ARRAYCUTS rows are values in items.
struct TechCutArraySpacingRule
{
  uint32_t flags = 0;
  int32_t via_width = 0;
  int32_t cut_spacing = 0;
  std::vector<TechCutArraySpacingItem> items;
};

namespace TechCutOrthogonalSpacingTableRuleFlag {
// The same grammar may occur inside PROPERTY LEF58_SPACINGTABLE. Preserve its
// origin so canonical export does not duplicate a preserved PROPERTY value.
constexpr uint32_t kLef58Property = 1u << 0;
}  // namespace TechCutOrthogonalSpacingTableRuleFlag

struct TechCutOrthogonalSpacingTableRule
{
  uint32_t flags = 0;
  std::vector<TechCutOrthogonalSpacingTableItem> items;
};

namespace TechCutLef58CutClassRuleFlag {
constexpr uint32_t kHasViaLength = 1u << 0;
constexpr uint32_t kHasNumCut = 1u << 1;
}  // namespace TechCutLef58CutClassRuleFlag

// Parsed payload of:
//   PROPERTY LEF58_CUTCLASS
//     "CUTCLASS name WIDTH width [LENGTH length]
//        [CUTS count] [ORIENT direction] ;" ;
struct TechCutLef58CutClassRule
{
  uint32_t flags = 0;
  std::string name;
  int32_t via_width = 0;
  int32_t via_length = 0;
  uint32_t num_cut = 0;
  CutDirection orient = CutDirection::kUnknown;
};

namespace TechCutLef58EnclosureRuleFlag {
constexpr uint32_t kHasOverhang1 = 1u << 0;
constexpr uint32_t kHasOverhang2 = 1u << 1;
constexpr uint32_t kHasEndOverhang1 = 1u << 2;
constexpr uint32_t kHasSideOverhang2 = 1u << 3;
constexpr uint32_t kHasMinWidth = 1u << 4;
constexpr uint32_t kIncludeAbutted = 1u << 5;
constexpr uint32_t kExceptExtraCut = 1u << 6;
constexpr uint32_t kHasCutWithin = 1u << 7;
constexpr uint32_t kPrl = 1u << 8;
constexpr uint32_t kNoSharedEdge = 1u << 9;
constexpr uint32_t kHasMinLength = 1u << 10;
constexpr uint32_t kHasRedundantCut = 1u << 11;
}  // namespace TechCutLef58EnclosureRuleFlag

// Parsed payload of one rule in:
//   PROPERTY LEF58_ENCLOSURE "ENCLOSURE CUTCLASS class ... ;" ;
struct TechCutLef58EnclosureRule
{
  uint32_t flags = 0;
  std::string cutclass_name;
  CutLayerSide side = CutLayerSide::kUnknown;
  int32_t overhang1 = 0;
  int32_t overhang2 = 0;
  int32_t end_overhang1 = 0;
  int32_t side_overhang2 = 0;
  int32_t min_width = 0;
  int32_t cut_within = 0;
  int32_t min_length = 0;
  int32_t redundant_cut_within = 0;
};

namespace TechCutLef58EnclosureEdgeRuleFlag {
constexpr uint32_t kHasMinWidth = 1u << 0;
constexpr uint32_t kHasMaxWidth = 1u << 1;
constexpr uint32_t kHasParLength = 1u << 2;
constexpr uint32_t kHasParWithin = 1u << 3;
constexpr uint32_t kExceptExtraCut = 1u << 4;
constexpr uint32_t kHasCutWithin = 1u << 5;
constexpr uint32_t kExceptTwoEdges = 1u << 6;
constexpr uint32_t kHasExceptWithin = 1u << 7;
constexpr uint32_t kHasConvexCorners = 1u << 8;
}  // namespace TechCutLef58EnclosureEdgeRuleFlag

// Parsed payload of one rule in:
//   PROPERTY LEF58_ENCLOSUREEDGE
//     "ENCLOSUREEDGE CUTCLASS class ... ;" ;
struct TechCutLef58EnclosureEdgeRule
{
  uint32_t flags = 0;
  std::string cutclass_name;
  CutLayerSide side = CutLayerSide::kUnknown;
  int32_t overhang = 0;
  int32_t min_width = 0;
  int32_t max_width = 0;
  int32_t par_length = 0;
  int32_t par_within = 0;
  int32_t cut_within = 0;
  int32_t except_within = 0;
  int32_t convex_length = 0;
  int32_t adjacent_length = 0;
  int32_t convex_par_within = 0;
  int32_t convex_corner_length = 0;
};

namespace TechCutLef58EolEnclosureRuleFlag {
constexpr uint32_t kHasMinEolWidth = 1u << 0;
constexpr uint32_t kEqualRectWidth = 1u << 1;
constexpr uint32_t kHasExtractOverhang = 1u << 2;
constexpr uint32_t kHasParallelSpace = 1u << 3;
constexpr uint32_t kHasExtension = 1u << 4;
constexpr uint32_t kHasMinLength = 1u << 5;
constexpr uint32_t kAllSides = 1u << 6;
}  // namespace TechCutLef58EolEnclosureRuleFlag

// Parsed payload of one rule in:
//   PROPERTY LEF58_EOLENCLOSURE "EOLENCLOSURE ... ;" ;
struct TechCutLef58EolEnclosureRule
{
  uint32_t flags = 0;
  int32_t eol_width = 0;
  int32_t min_eol_width = 0;
  CutDirection edge_direction = CutDirection::kUnknown;
  std::string cutclass_name;
  CutLayerSide side = CutLayerSide::kUnknown;
  Lef58EolEnclosureApplication application = Lef58EolEnclosureApplication::kUnknown;
  int32_t overhang = 0;
  int32_t extract_overhang = 0;
  int32_t parallel_space = 0;
  int32_t backward_ext = 0;
  int32_t forward_ext = 0;
  int32_t min_length = 0;
};

// Parsed payload of one rule in:
//   PROPERTY LEF58_EOLSPACING "EOLSPACING ... ;" ;
// TOCLASS entries are nested values because they have no independent identity.
struct TechCutLef58EolSpacingRule
{
  std::string cutclass_name;
  int32_t cut_spacing1 = 0;
  int32_t cut_spacing2 = 0;
  std::vector<TechCutLef58EolSpacingToClass> to_classes;
  int32_t eol_width = 0;
  int32_t prl = 0;
  int32_t smaller_overhang = 0;
  int32_t equal_overhang = 0;
  int32_t side_ext = 0;
  int32_t backward_ext = 0;
  int32_t span_length = 0;
};

namespace TechCutLef58SpacingTableRuleFlag {
constexpr uint32_t kHasSecondLayer = 1u << 0;
constexpr uint32_t kHasPrl = 1u << 1;
constexpr uint32_t kMaxXY = 1u << 2;
constexpr uint32_t kHasDefault = 1u << 3;
constexpr uint32_t kSameMask = 1u << 4;
constexpr uint32_t kSameNet = 1u << 5;
constexpr uint32_t kSameMetal = 1u << 6;
constexpr uint32_t kSameVia = 1u << 7;
constexpr uint32_t kNoStack = 1u << 8;
constexpr uint32_t kPrlForAlignedCut = 1u << 9;
}  // namespace TechCutLef58SpacingTableRuleFlag

// Parsed payload of:
//   PROPERTY LEF58_SPACINGTABLE
//     "SPACINGTABLE [DEFAULT spacing] [SAMEMASK]
//        [SAMENET | SAMEMETAL | SAMEVIA]
//        [LAYER secondLayer [NOSTACK] [PRLFORALIGNEDCUT ...]]
//        [PRL value [HORIZONTAL | VERTICAL] [MAXXY] ...]
//        CUTCLASS ... ;" ;
// The class-name axes and cells form a row-major rectangular table.
struct TechCutLef58SpacingTableRule
{
  uint32_t flags = 0;
  int32_t default_spacing = 0;
  std::string second_layer_name;
  int32_t prl = 0;
  CutDirection prl_direction = CutDirection::kUnknown;
  std::vector<std::string> cutclass1_names;
  std::vector<std::string> cutclass2_names;
  std::vector<CutClassEdge> cutclass1_edges;
  std::vector<CutClassEdge> cutclass2_edges;
  std::vector<TechCutLef58SpacingTableClassPair> prl_for_aligned_cut;
  std::vector<TechCutLef58SpacingTablePrlEntry> prl_entries;
  std::vector<TechCutLef58SpacingTableCell> cells;

  [[nodiscard]] uint32_t class1Count() const noexcept { return static_cast<uint32_t>(cutclass1_names.size()); }
  [[nodiscard]] uint32_t class2Count() const noexcept { return static_cast<uint32_t>(cutclass2_names.size()); }
};

enum class TechCutCurrentDensitySignal : uint8_t
{
  kAc,
  kDc,
};

enum class TechCutCurrentDensityType : uint8_t
{
  kUnknown,
  kPeak,
  kAverage,
  kRms,
};

namespace TechCutCurrentDensityRuleFlag {
constexpr uint32_t kHasScalar = 1u << 0;
constexpr uint32_t kHasFrequencies = 1u << 1;
constexpr uint32_t kHasCutAreas = 1u << 2;
constexpr uint32_t kHasTableEntries = 1u << 3;
}  // namespace TechCutCurrentDensityRuleFlag

// Native CUT-layer current-density alternatives:
//   ACCURRENTDENSITY {PEAK | AVERAGE | RMS}
//     {scalar | FREQUENCY ... ; [CUTAREA ... ;] TABLEENTRIES ...} ;
//   DCCURRENTDENSITY AVERAGE
//     {scalar | CUTAREA ... ; TABLEENTRIES ...} ;
// AC table entries are frequency-major and cut-area-minor. When CUTAREA is
// absent, the table has one implicit cut-area column.
struct TechCutCurrentDensityRule
{
  TechCutCurrentDensitySignal signal = TechCutCurrentDensitySignal::kAc;
  TechCutCurrentDensityType type = TechCutCurrentDensityType::kUnknown;
  uint32_t flags = 0;
  double scalar = 0.0;
  std::vector<double> frequencies;
  std::vector<int64_t> cut_areas;  // DBU squared.
  std::vector<double> table_entries;

  [[nodiscard]] uint32_t frequencyCount() const noexcept { return static_cast<uint32_t>(frequencies.size()); }
  [[nodiscard]] uint32_t cutAreaCount() const noexcept { return static_cast<uint32_t>(cut_areas.size()); }
  [[nodiscard]] uint32_t tableEntryCount() const noexcept { return static_cast<uint32_t>(table_entries.size()); }
};

}  // namespace eccdb
