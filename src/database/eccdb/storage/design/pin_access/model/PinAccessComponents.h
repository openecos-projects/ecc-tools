// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "library/common/LibraryTypes.h"
#include "tech/common/TechLayerIds.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

// The six legal OpenDB dbAccessPoint directions.  Keep these named bits
// instead of depending on either OpenDB's or TritonRoute's enum ordering.
namespace DesignPinAccessDirection {
constexpr uint32_t kNorth = 1u << 0;
constexpr uint32_t kEast = 1u << 1;
constexpr uint32_t kSouth = 1u << 2;
constexpr uint32_t kWest = 1u << 3;
constexpr uint32_t kUp = 1u << 4;
constexpr uint32_t kDown = 1u << 5;
constexpr uint32_t kAll = kNorth | kEast | kSouth | kWest | kUp | kDown;
}  // namespace DesignPinAccessDirection

// Matches odb::dbAccessType exactly by meaning, without exposing ODB types
// through EccDB's storage model.
enum class DesignPinAccessType : uint8_t
{
  kOnGrid,
  kHalfGrid,
  kCenter,
  kEncOpt,
  kNearbyGrid
};

namespace DesignPinAccessPathSegmentFlag {
constexpr uint32_t kBeginTruncated = 1u << 0;
constexpr uint32_t kEndTruncated = 1u << 1;
}  // namespace DesignPinAccessPathSegmentFlag

// A candidate route segment attached to an access point.  Its rectangle is
// permitted to be horizontal or vertical (one dimension may be zero), as in
// odb::dbAccessPoint::addSegment().
struct DesignPinAccessPathSegment
{
  Rect rectangle;
  uint32_t flags = 0;
};

// One alternative via usable from a candidate access point.  A reference is
// either a technology VIA or a design-local VIA; cut_count corresponds to the
// one-based bucket used by dbAccessPoint::getVias().
struct DesignPinAccessVia
{
  uint32_t cut_count = 0;
  TechViaMasterId tech_via;
  DesignViaId design_via;
};

// Candidate access point payload shared by every instance of a library
// master port. `point` is master-local for catalog entries; when this value
// appears on a DesignBlockPin it is block-absolute instead.
struct DesignPinAccessPoint
{
  Point point;
  TechRoutingLayerId layer;
  uint32_t access_directions = 0;
  DesignPinAccessType low_type = DesignPinAccessType::kOnGrid;
  DesignPinAccessType high_type = DesignPinAccessType::kOnGrid;
  std::vector<DesignPinAccessVia> vias;
  std::vector<DesignPinAccessPathSegment> path_segments;
};

// The index is intentionally explicit instead of relying on vector position:
// OpenDB and TritonRoute select candidate groups by this number.
struct DesignPinAccessCandidateGroup
{
  uint32_t group_index = 0;
  std::vector<DesignPinAccessPoint> points;
};

// Candidate groups owned by one LEF PORT / ODB dbMPin.
struct DesignMasterPortPinAccessGroups
{
  LibraryMasterPortId master_port;
  std::vector<DesignPinAccessCandidateGroup> groups;
};

// Optional component attached to DesignRoot.  It holds only master-port
// candidates, whose locations are in master-local coordinates.
struct DesignPinAccessCatalog
{
  std::vector<DesignMasterPortPinAccessGroups> master_port_groups;
};

// Optional component attached directly to one OpenDB dbBPin / DesignBlockPin
// entity. Unlike catalog entries, these points are block-absolute because a
// block pin is a design object rather than a reusable library object.
struct DesignBlockPinAccessGroups
{
  std::vector<DesignPinAccessCandidateGroup> groups;
};

// Optional component attached to DesignInstance.  Its presence means this
// instance selected one candidate-group index for its master ports.
struct DesignInstancePinAccessGroupIndex
{
  uint32_t group_index = 0;
};

// One selected candidate for a physical instance pin.  ODB has one preferred
// dbAccessPoint per dbMPin, so master_port is unique within the vector.
struct DesignPreferredPinAccessRef
{
  LibraryMasterPortId master_port;
  uint32_t group_index = 0;
  uint32_t point_index = 0;
};

// One sparse preferred-access entry owned by an instance. A pin has no entry
// until a preferred candidate has been selected for it.
struct DesignInstancePreferredPinAccessEntry
{
  DesignInstancePinId pin;
  std::vector<DesignPreferredPinAccessRef> values;
};

// Optional component attached to DesignInstance. Keeping the sparse entries
// with their owner avoids a separate ECS entity and component allocation for
// every physical instance pin.
struct DesignInstancePreferredPinAccess
{
  std::vector<DesignInstancePreferredPinAccessEntry> entries;
};

struct DesignBlockPinAccessUpdate
{
  DesignBlockPinId pin;
  DesignBlockPinAccessGroups candidates;
};

struct DesignInstancePinAccessUpdate
{
  DesignInstanceId instance;
  std::optional<uint32_t> group_index;
  DesignInstancePreferredPinAccess preferences;
};

// Complete values for explicit owners. Unmentioned ports/pins/instances stay
// unchanged; an empty value clears that owner's derived state.
struct DesignPinAccessUpdate
{
  std::vector<DesignMasterPortPinAccessGroups> master_ports;
  std::vector<DesignBlockPinAccessUpdate> block_pins;
  std::vector<DesignInstancePinAccessUpdate> instances;
};

}  // namespace eccdb
