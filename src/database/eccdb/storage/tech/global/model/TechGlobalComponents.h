#pragma once

#include <cstdint>

#include "tech/common/TechLayerTypes.h"

namespace eccdb {

// Marker for the one technology root entity in a TechRegistry. It has no LEF
// statement of its own; global LEF statements are optional components on it.
struct TechRoot
{
};

// LEF UNITS. A missing component means the complete statement was absent;
// flags distinguish clauses that are absent inside a present statement.
namespace TechGlobalUnitsFlag {
constexpr uint32_t kHasNanoseconds = 1u << 0;
constexpr uint32_t kHasPicofarads = 1u << 1;
constexpr uint32_t kHasOhms = 1u << 2;
constexpr uint32_t kHasMilliwatts = 1u << 3;
constexpr uint32_t kHasMilliamps = 1u << 4;
constexpr uint32_t kHasVolts = 1u << 5;
constexpr uint32_t kHasDatabaseUnitsPerMicron = 1u << 6;
constexpr uint32_t kHasMegahertz = 1u << 7;
}  // namespace TechGlobalUnitsFlag

// LEF 5.8:
//   UNITS
//     [TIME NANOSECONDS value ;]
//     [CAPACITANCE PICOFARADS value ;]
//     [RESISTANCE OHMS value ;]
//     [POWER MILLIWATTS value ;]
//     [CURRENT MILLIAMPS value ;]
//     [VOLTAGE VOLTS value ;]
//     [DATABASE MICRONS value ;]
//     [FREQUENCY MEGAHERTZ value ;]
//   END UNITS
struct TechGlobalUnits
{
  uint32_t flags = 0;
  int32_t nanoseconds = 0;
  int32_t picofarads = 0;
  int32_t ohms = 0;
  int32_t milliwatts = 0;
  int32_t milliamps = 0;
  int32_t volts = 0;
  int32_t database_units_per_micron = 0;
  int32_t megahertz = 0;
};

// LEF 5.8:
//   MANUFACTURINGGRID value ;
struct TechManufacturingGrid
{
  int32_t value = 0;
};

namespace TechMaxViaStackFlag {
constexpr uint32_t kNoSingle = 1u << 0;
constexpr uint32_t kHasRange = 1u << 1;
}  // namespace TechMaxViaStackFlag

// LEF 5.8:
//   MAXVIASTACK maxStack [RANGE bottomLayer topLayer] ;
// TechGlobalStorage validates the layer references and their order before
// attaching this component to TechRoot.
struct TechMaxViaStack
{
  uint32_t flags = 0;
  uint32_t max_stack_count = 0;
  TechRoutingLayerId bottom_layer;
  TechRoutingLayerId top_layer;
};

}  // namespace eccdb
