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
#include <span>
#include <string>
#include <vector>

#include "context/DatabaseContext.h"
#include "design/netlist/model/NetlistComponents.h"

namespace eccdb {

class DesignStore;

// Coordinates instance lifecycle changes that span the design netlist,
// connectivity, geometry-cache and pin-access stores.  Read-only instance and
// terminal traversal remains on DesignNetlistStorage; this service owns the
// cross-storage mutation contract.
class InstanceService
{
 public:
  explicit InstanceService(DatabaseContext& context) noexcept;

  [[nodiscard]] DesignInstanceId create(DesignInstance instance, std::span<const Rect> region_bounds = {});
  [[nodiscard]] bool destroy(DesignInstanceId instance);

  // Update the complete instance value.  The storage keeps inline region
  // geometry when region_bounds is empty; pass a non-empty span to replace it.
  void update(DesignInstanceId instance, DesignInstance value, std::span<const Rect> region_bounds = {});

  void place(DesignInstanceId instance,
             Point origin,
             DesignOrientation orientation,
             DesignPlacementStatus placement_status);
  void move(DesignInstanceId instance, int32_t dx, int32_t dy);
  void rename(DesignInstanceId instance, std::string name);

  // Retarget a compatible master while preserving every Term entity ID and
  // its regular/special Net relation.  Pin-access selections are derived from
  // the old master and are cleared after a successful retarget.
  void swapMasterCompatible(DesignInstanceId instance, LibraryCellMasterId master);

 private:
  [[nodiscard]] DesignStore& design() const noexcept;
  void ensureInstance(DesignInstanceId instance) const;
  [[nodiscard]] std::vector<DesignNetId> connectedNets(DesignInstanceId instance) const;
  void clearPinAccess(DesignInstanceId instance) const;
  void invalidateInstanceBounds(DesignInstanceId instance) const;
  void notifyBefore(DatabaseChangeDomain domains,
                    std::span<const DesignInstanceId> instances,
                    std::span<const DesignNetId> nets) noexcept;
  void notifyAfter(DatabaseChangeDomain domains,
                   std::span<const DesignInstanceId> instances,
                   std::span<const DesignNetId> nets,
                   std::span<const GeometryOwner> replacements = {}) noexcept;

  DatabaseContext& _context;
};

}  // namespace eccdb
