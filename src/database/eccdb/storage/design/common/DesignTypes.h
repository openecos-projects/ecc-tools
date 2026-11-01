// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <string>

#include "common/EnttId.h"
#include "design/schema/DesignEntitySchema.h"
#include "geometry/GeometryEntity.h"

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
struct DesignInstanceTerm;
struct DesignInstanceTerms;
struct DesignBlockTerm;
struct DesignBlockPin;
struct DesignNet;
struct DesignWire;
struct DesignRouteShape;
struct DesignVia;
struct DesignNonDefaultRule;
struct DesignRegion;
struct DesignGroup;
struct DesignBlockage;
struct DesignFill;

// Legacy storage spellings remain aliases while callers migrate to the
// OpenDB-aligned Term/Pin vocabulary.
using DesignInstancePin = DesignInstanceTerm;
using DesignInstancePins = DesignInstanceTerms;
using DesignIoPin = DesignBlockTerm;
using DesignIoPinPort = DesignBlockPin;

using DesignRootId = EnttId<DesignEntity, DesignRoot>;
using DesignRowId = EnttId<DesignEntity, DesignRow>;
using DesignTrackGridId = EnttId<DesignEntity, DesignTrackGrid>;
using DesignGCellGridId = EnttId<DesignEntity, DesignGCellGrid>;
using DesignInstanceId = EnttId<DesignEntity, DesignInstance>;
// OpenDB dbITerm equivalent. The ID is a normal DesignRegistry entity ID with
// EnTT generation semantics; each live ID carries a DesignInstanceTerm
// component.
using DesignInstanceTermId = EnttId<DesignEntity, DesignInstanceTerm>;
using DesignInstancePinId = DesignInstanceTermId;
// OpenDB dbBTerm and dbBPin are separate design entities. A block term owns
// its ordered block-pin IDs; a block pin owns one physical geometry group.
using DesignBlockTermId = EnttId<DesignEntity, DesignBlockTerm>;
using DesignIoPinId = DesignBlockTermId;
using DesignBlockPinId = EnttId<DesignEntity, DesignBlockPin>;
using DesignNetId = EnttId<DesignEntity, DesignNet>;
using DesignWireId = EnttId<DesignEntity, DesignWire>;
// Compatibility spelling only. Route geometry IDs belong to GeometryEntity's
// independent registry; they are no longer DesignRegistry entities.
using DesignRouteShapeId = GeometryShapeId;
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
