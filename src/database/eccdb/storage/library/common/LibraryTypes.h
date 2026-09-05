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
struct LibraryMasterPort;

using LibrarySiteId = EnttId<LibraryEntity, LibrarySite>;
using LibraryCellMasterId = EnttId<LibraryEntity, LibraryCellMaster>;
using LibraryMasterTermId = EnttId<LibraryEntity, LibraryMasterTerm>;
using LibraryMasterPortId = EnttId<LibraryEntity, LibraryMasterPort>;

}  // namespace eccdb
