// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>

#include "common/EnttId.h"

namespace eccdb {

// Every complete object loaded from one cell library is allocated by this
// registry-local EnTT entity domain.
enum class LibraryEntity : uint32_t
{
};

struct LibrarySite;
struct LibraryCellMaster;
struct LibraryMasterTerm;
struct LibraryMasterPin;
// Compatibility spelling for LEF PORT-facing code.
using LibraryMasterPort = LibraryMasterPin;

using LibrarySiteId = EnttId<LibraryEntity, LibrarySite>;
using LibraryCellMasterId = EnttId<LibraryEntity, LibraryCellMaster>;
using LibraryMasterTermId = EnttId<LibraryEntity, LibraryMasterTerm>;
// OpenDB dbMPin: one LEF PORT / strongly-connected physical geometry group.
using LibraryMasterPinId = EnttId<LibraryEntity, LibraryMasterPin>;
using LibraryMasterPortId = LibraryMasterPinId;

}  // namespace eccdb
