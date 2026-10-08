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

#include <optional>
#include <span>

#include "context/DatabaseContext.h"
#include "design/constraint/model/ConstraintComponents.h"

namespace eccdb {

class DesignStore;

// Coordinates constraint lifecycle operations whose ownership crosses the
// constraint, instance, and geometry stores.  The native constraint storage
// remains the source of truth for individual Region/Group/Blockage payload
// validation; this service owns relationship cleanup and change boundaries.
class ConstraintService
{
 public:
  explicit ConstraintService(DatabaseContext& context) noexcept;

  [[nodiscard]] DesignRegionId createRegion(DesignRegionInput region);
  void updateRegion(DesignRegionId region, DesignRegionInput value);
  // Detaches named Region references from Instances and Groups before
  // deleting Region-owned rectangle geometry.
  [[nodiscard]] bool deleteRegion(DesignRegionId region);

  [[nodiscard]] DesignGroupId createGroup(DesignGroup group);
  void updateGroup(DesignGroupId group, DesignGroup value);
  // Removes the Group entity only. Its member Instances remain unchanged.
  [[nodiscard]] bool deleteGroup(DesignGroupId group);
  void replaceGroupMembers(DesignGroupId group, std::span<const DesignInstanceId> members);
  void assignGroupRegion(DesignGroupId group, std::optional<DesignRegionId> region);

  [[nodiscard]] DesignBlockageId createBlockage(DesignBlockageInput blockage);
  void updateBlockage(DesignBlockageId blockage, DesignBlockageInput value);
  // Removes blockage-owned geometry but preserves the referenced Instance.
  [[nodiscard]] bool deleteBlockage(DesignBlockageId blockage);

  // std::nullopt clears the named REGION relationship. A separate clear
  // operation reports whether there was state to remove.
  void assignInstanceRegion(DesignInstanceId instance, std::optional<DesignRegionId> region);
  [[nodiscard]] bool clearInstanceRegion(DesignInstanceId instance);

 private:
  [[nodiscard]] DesignStore& design() const noexcept;
  void ensureRegion(DesignRegionId region) const;
  void ensureGroup(DesignGroupId group) const;
  void ensureBlockage(DesignBlockageId blockage) const;
  void ensureInstance(DesignInstanceId instance) const;
  void notifyBefore(DatabaseChangeDomain domains,
                    std::span<const DesignInstanceId> instances,
                    std::span<const GeometryShapeId> shapes = {}) noexcept;
  void notifyAfter(DatabaseChangeDomain domains,
                   std::span<const DesignInstanceId> instances,
                   std::span<const GeometryShapeId> shapes = {},
                   std::span<const GeometryOwner> replacements = {}) noexcept;

  DatabaseContext& _context;
};

}  // namespace eccdb
