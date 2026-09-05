#pragma once

#include <cstdint>

namespace eccdb {

namespace TechCutLayerFlag {
constexpr uint32_t kHasWidth = 1u << 0;
constexpr uint32_t kHasResistance = 1u << 1;
}  // namespace TechCutLayerFlag

// LEF 5.8 CUT layer scalar subset represented by this component:
//   LAYER name
//     TYPE CUT ;
//     [WIDTH defaultWidth ;]
//     [RESISTANCE resistancePerCut ;]
//     ... CUT rules and PROPERTY clauses ...
//   END name
// Name/MASK/properties are common components on the same layer entity.
struct TechCutLayer
{
  uint32_t flags = 0;
  int32_t width = 0;
  double resistance_per_cut = 0.0;
};

}  // namespace eccdb
