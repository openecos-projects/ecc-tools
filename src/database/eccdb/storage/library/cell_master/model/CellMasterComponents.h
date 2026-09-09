#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "library/common/LibraryTypes.h"

namespace eccdb {

// The LEF MACRO CLASS variants currently represented by iDB.
enum class LibraryCellMasterType : uint8_t
{
  kNone = 0,
  kCover,
  kCoverBump,
  kRing,
  kBlock,
  kBlockBlackbox,
  kBlockSoft,
  kPad,
  kPadInput,
  kPadOutput,
  kPadInOut,
  kPadPower,
  kPadSpacer,
  kPadAreaIo,
  kCore,
  kCoreFeedThru,
  kCoreTieHigh,
  kCoreTieLow,
  kCoreSpacer,
  kCoreAntennaCell,
  kCoreWelltap,
  kEndcap,
  kEndcapPre,
  kEndcapPost,
  kEndcapTopLeft,
  kEndcapTopRight,
  kEndcapBottomLeft,
  kEndcapBottomRight
};

// LEF 5.8 MACRO scalar subset represented by this component:
//   MACRO name
//     [CLASS class [subclass] ;]
//     [ORIGIN x y ;]
//     [SIZE width BY height ;]
//     [SYMMETRY {X | Y | R90} ... ;]
//     [SITE siteName ;]
//     [PIN ... END pinName] ...
//     [OBS ... END] ...
//   END name
// PIN/PORT and OBS payloads live in separate entities/components.
struct LibraryCellMaster
{
  std::string name;
  LibraryCellMasterType type = LibraryCellMasterType::kNone;
  bool symmetry_x = false;
  bool symmetry_y = false;
  bool symmetry_r90 = false;
  bool core_filler = false;
  bool pad_filler = false;
  std::optional<LibrarySiteId> site = std::nullopt;
  int64_t origin_x = 0;
  int64_t origin_y = 0;
  uint32_t width = 0;
  uint32_t height = 0;

  // Storage-managed, ordered LEF PIN entities owned by this MACRO.
  std::vector<LibraryMasterTermId> terms;
};

}  // namespace eccdb
