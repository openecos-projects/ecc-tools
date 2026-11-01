// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory
#pragma once

#include <memory>

namespace idrc {
class DRCInterface;
class DRCDesignSource;
}
namespace eccdb {
class DatabaseContext;
class DesignStore;
class TechStore;
class LibraryStore;

namespace adapters {

// The returned source borrows all three stores; callers must keep them alive
// for the complete iDRC session.
[[nodiscard]] std::shared_ptr<idrc::DRCDesignSource> makeDrcSource(DesignStore& design, TechStore& tech,
                                                                    LibraryStore& library);

// Stores are borrowed until detached. A null context (or three null stores)
// clears the source and restores the tool's legacy iDB path.
void bindDrcSource(idrc::DRCInterface& tool, DatabaseContext* context);
inline void bindDrcSource(idrc::DRCInterface& tool, DatabaseContext& context)
{
  bindDrcSource(tool, &context);
}
void bindDrcSource(idrc::DRCInterface& tool, DesignStore* design, TechStore* tech, LibraryStore* library);

}  // namespace adapters
}  // namespace eccdb
