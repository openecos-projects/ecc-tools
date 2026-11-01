// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tech/common/AntennaRuleTypes.h"
#include "library/common/LibraryTypes.h"

namespace eccdb {

// LEF PIN attributes represented by the logical master-terminal entity.
enum class LibraryMasterTermDirection : uint8_t
{
  kNone = 0,
  kInput,
  kOutput,
  kOutputTriState,
  kInOut,
  kFeedThru
};

enum class LibraryMasterTermUse : uint8_t
{
  kNone = 0,
  kSignal,
  kAnalog,
  kPower,
  kGround,
  kClock,
  kTieOff,
  kScan,
  kReset
};

enum class LibraryMasterTermShape : uint8_t
{
  kNone = 0,
  kAbutment,
  kRing,
  kFeedThru
};

// LEF 5.8 MACRO PIN logical subset represented by this component:
//   PIN name
//     [DIRECTION direction [TRISTATE] ;]
//     [USE use ;]
//     [SHAPE shape ;]
//     [PORT ... END] ...
//   END name
// Each PORT is a separate LibraryMasterPort entity owned by this term.
struct LibraryMasterTerm
{
  std::string name;
  LibraryMasterTermDirection direction = LibraryMasterTermDirection::kNone;
  LibraryMasterTermUse use = LibraryMasterTermUse::kNone;
  LibraryMasterTermShape shape = LibraryMasterTermShape::kNone;

  // Storage-managed hierarchy. PORT order follows the source LEF order.
  LibraryCellMasterId master;
  std::vector<LibraryMasterPortId> ports;
};

namespace LibraryMasterTermAntennaFlag {
constexpr uint32_t kHasValues = 1u << 0;
constexpr uint32_t kHasLayerValues = 1u << 1;
constexpr uint32_t kHasModels = 1u << 2;
}  // namespace LibraryMasterTermAntennaFlag

// Optional LEF MACRO PIN antenna metadata owned by this master term.
struct LibraryMasterTermAntenna
{
  uint32_t flags = 0;
  std::vector<AntennaValue> values;
  std::vector<AntennaLayerValue> layer_values;
  std::vector<AntennaModel> models;
};

}  // namespace eccdb
