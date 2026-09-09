#pragma once

#include <cstdint>
#include <string>

#include "common/EnttId.h"
#include "design/schema/DesignEntitySchema.h"

namespace eccdb {

enum class DesignPropertyType : uint8_t
{
  kString,
  kInteger,
  kReal
};

// One DEF + PROPERTY name value occurrence. PROPERTYDEFINITIONS is a separate
// schema concern from this reusable value type.
struct DesignProperty
{
  std::string name;
  std::string value;
  DesignPropertyType type = DesignPropertyType::kString;
};

struct DesignRoot;
struct DesignRow;
struct DesignTrackGrid;
struct DesignGCellGrid;
struct DesignInstance;
struct DesignInstancePin;
struct DesignIoPin;
struct DesignNet;
struct DesignWire;
struct DesignVia;
struct DesignNonDefaultRule;
struct DesignRegion;
struct DesignGroup;
struct DesignBlockage;
struct DesignFill;

using DesignRootId = EnttId<DesignEntity, DesignRoot>;
using DesignRowId = EnttId<DesignEntity, DesignRow>;
using DesignTrackGridId = EnttId<DesignEntity, DesignTrackGrid>;
using DesignGCellGridId = EnttId<DesignEntity, DesignGCellGrid>;
using DesignInstanceId = EnttId<DesignEntity, DesignInstance>;
using DesignInstancePinId = EnttId<DesignEntity, DesignInstancePin>;
using DesignIoPinId = EnttId<DesignEntity, DesignIoPin>;
using DesignNetId = EnttId<DesignEntity, DesignNet>;
using DesignWireId = EnttId<DesignEntity, DesignWire>;
using DesignViaId = EnttId<DesignEntity, DesignVia>;
using DesignNonDefaultRuleId = EnttId<DesignEntity, DesignNonDefaultRule>;
using DesignRegionId = EnttId<DesignEntity, DesignRegion>;
using DesignGroupId = EnttId<DesignEntity, DesignGroup>;
using DesignBlockageId = EnttId<DesignEntity, DesignBlockage>;
using DesignFillId = EnttId<DesignEntity, DesignFill>;

enum class DesignOrientation : uint8_t
{
  kN,
  kS,
  kE,
  kW,
  kFN,
  kFS,
  kFE,
  kFW,
};

[[nodiscard]] inline bool isValidDesignOrientation(DesignOrientation orientation) noexcept
{
  switch (orientation) {
    case DesignOrientation::kN:
    case DesignOrientation::kS:
    case DesignOrientation::kE:
    case DesignOrientation::kW:
    case DesignOrientation::kFN:
    case DesignOrientation::kFS:
    case DesignOrientation::kFE:
    case DesignOrientation::kFW:
      return true;
  }
  return false;
}

[[nodiscard]] inline bool isQuarterTurnOrientation(DesignOrientation orientation) noexcept
{
  return orientation == DesignOrientation::kE || orientation == DesignOrientation::kW || orientation == DesignOrientation::kFE
         || orientation == DesignOrientation::kFW;
}

enum class DesignAxis : uint8_t
{
  kX,
  kY
};

[[nodiscard]] inline bool isValidDesignAxis(DesignAxis axis) noexcept
{
  return axis == DesignAxis::kX || axis == DesignAxis::kY;
}

}  // namespace eccdb
