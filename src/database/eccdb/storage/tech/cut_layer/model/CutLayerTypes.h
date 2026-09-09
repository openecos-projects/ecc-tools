#pragma once

#include <cstdint>

namespace eccdb {

// Horizontal/vertical orientation used by cut classes and selected LEF58 rules.
enum class CutDirection : uint8_t
{
  kUnknown,
  kHorizontal,
  kVertical
};

// Above/below side qualifier used by native and LEF58 cut-layer rules.
enum class CutLayerSide : uint8_t
{
  kUnknown,
  kAbove,
  kBelow
};

// SIDE/END qualifier on a LEF58_SPACINGTABLE CUTCLASS axis entry.
enum class CutClassEdge : uint8_t
{
  kUnspecified,
  kSide,
  kEnd
};

// Edge-only application variant used by PROPERTY LEF58_EOLENCLOSURE.
enum class Lef58EolEnclosureApplication : uint8_t
{
  kUnknown,
  kLongEdgeOnly,
  kShortEdgeOnly
};

}  // namespace eccdb
