// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>

namespace eccdb {
class PhysicalDesignService;
}

namespace odb {
class dbBlock;
}

namespace drt::io {

void writeNativePlacementSnapshot(const eccdb::PhysicalDesignService& physical,
                                  const std::filesystem::path& path);
void writeNativePlacementSnapshot(odb::dbBlock& block, const std::filesystem::path& path);

}  // namespace drt::io
