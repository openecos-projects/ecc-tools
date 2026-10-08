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

#include "library/common/LibraryGeometry.h"

#include <stdexcept>

#include "tech/TechRegistry.h"

namespace eccdb {

void validateLibraryViaPlacements(const TechRegistry& tech_registry, const std::vector<LibraryViaPlacement>& placements)
{
  const auto& registry = tech_registry.registry();
  for (const auto& placement : placements) {
    if (!placement.via || !registry.valid(placement.via.entity())
        || !registry.all_of<TechViaMaster, TechViaGeometry>(placement.via.entity())) {
      throw std::invalid_argument("library geometry references an invalid tech via master");
    }
    if (!registry.get<const TechViaGeometry>(placement.via.entity()).bounding_box.hasArea()) {
      throw std::invalid_argument("library geometry references a via master without geometry");
    }
  }
}

}  // namespace eccdb
